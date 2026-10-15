#include "cli.hpp"
#include "color_io.hpp"
#include "image_io.hpp"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <set>
#include <vector>
#include <algorithm>
#include <fstream>
namespace {
using Clock=std::chrono::steady_clock;
double seconds(Clock::time_point start) {return std::chrono::duration<double>(Clock::now()-start).count();}
int number(const std::string& s) {
    if(s.empty() || !std::all_of(s.begin(),s.end(),[](char c){return c>='0' && c<='9';}))
        throw std::invalid_argument("Se requiere un entero positivo: "+s);
    int v=std::stoi(s);
    if(v<1) throw std::invalid_argument("Se requiere un entero positivo: "+s);
    return v;
}
double psnr(double e) {return e==0?std::numeric_limits<double>::infinity():10*std::log10(1/e);}
void distinct(const char* in,const char* out) {
    namespace fs=std::filesystem;
    if(fs::weakly_canonical(in)==fs::weakly_canonical(out) || (fs::exists(out) && fs::equivalent(in,out)))
        throw std::invalid_argument("Entrada y salida deben ser archivos distintos.");
}
void usage(std::ostream& out, fractal::Model model,fractal::CmpProfile profile) {
    const char* binary=model==fractal::Model::Sequential?"fractal_seq":(model==fractal::Model::FineGrained?"fractal_fine":(model==fractal::Model::CoarseGrained?"fractal_coarse":"fractal_cmp"));
    if(model==fractal::Model::CMP && profile==fractal::CmpProfile::Local) binary="fractal_cmp_local";
    out<<"Uso:\n  "<<binary<<" compress entrada.png salida.frc [range=8] [stride=8]\n"
       <<"  "<<binary<<" compress-gray entrada.png salida.frc [range=8] [stride=8]\n";
    if(model==fractal::Model::Sequential)
        out<<"  fractal_seq decompress entrada.frc salida.png [iteraciones=40]\n"
           <<"Opciones decompress: --iterations N\n";
    out<<"  "<<binary<<" compare original.png reconstruida.png\n"
       <<"  "<<binary<<" compare-gray original.png reconstruida.png\n"
       <<"Opciones compress/ compress-gray: --range N --stride N\n"
       <<"Equipos: --teams N --threads-per-team N\n"
       <<"Modelo fijo: "<<fractal::model_name(model)<<"\n";
    if(model==fractal::Model::CMP)
        out<<"CMP perfil "<<fractal::cmp_profile_name(profile)<<": --list-cpus | --report-affinity salida.csv\n"
           <<"Un trabajador por nucleo distinto permitido. Reconstruccion: fractal_seq decompress.\n";
    if(model==fractal::Model::CoarseGrained)
        out<<"Coarse-Grained: stall tras >2/3 de dominios locales consecutivos sin mejorar.\n"
           <<"Diagnostico: --trace-schedule salida.csv. Reconstruccion: fractal_seq decompress.\n";
    if(model==fractal::Model::FineGrained)
        out<<"Fine-Grained: --quantum-domains N [--trace-schedule salida.csv]\n"
           <<"Reconstruccion: usar fractal_seq decompress.\n";
}
struct Options {
    std::string command, input, output, trace_path, affinity_path;
    fractal::Config codec;
    fractal::ExecutionConfig execution;
    int iterations=40;
};
Options parse(int argc, char** argv, fractal::Model model,fractal::CmpProfile profile) {
    Options options;
    options.execution.model=model;
    options.execution.cmp_profile=profile;
    options.command=argv[1];
    const bool compress=options.command=="compress" || options.command=="compress-gray";
    const bool decompress=options.command=="decompress";
    if(!compress && !decompress && options.command!="compare" && options.command!="compare-gray")
        throw std::invalid_argument("Comando desconocido: "+options.command);
    std::vector<std::string> positional;
    std::set<std::string> seen;
    for(int i=2;i<argc;++i) {
        std::string arg=argv[i];
        if(arg.rfind("--",0)!=0) {positional.push_back(arg); continue;}
        if(!seen.insert(arg).second) throw std::invalid_argument("Opcion repetida: "+arg);
        const bool fineOption=arg=="--quantum-domains" || arg=="--trace-schedule" || arg=="--report-affinity";
        const bool codecOption=arg=="--range" || arg=="--stride";
        const bool executionOption=arg=="--model" || arg=="--teams" || arg=="--threads-per-team";
        if((fineOption && !compress) || (codecOption && !compress) || (arg=="--iterations" && !decompress) ||
           (executionOption && !compress && !decompress) ||
           (!fineOption && !codecOption && !executionOption && arg!="--iterations"))
            throw std::invalid_argument("Opcion no admitida para este comando: "+arg);
        if(i+1>=argc || std::string(argv[i+1]).rfind("--",0)==0)
            throw std::invalid_argument("Falta valor de opcion: "+arg);
        std::string value=argv[++i];
        if(arg=="--model") options.execution.model=fractal::parse_model(value);
        else if(arg=="--teams") options.execution.teams=number(value);
        else if(arg=="--threads-per-team") options.execution.threads_per_team=number(value);
        else if(arg=="--quantum-domains") options.execution.quantum_domains=number(value);
        else if(arg=="--trace-schedule") options.trace_path=value;
        else if(arg=="--report-affinity") options.affinity_path=value;
        else if(arg=="--range") options.codec.range=number(value);
        else if(arg=="--stride") options.codec.stride=number(value);
        else options.iterations=number(value);
    }
    const std::size_t maximum=compress?4:(decompress?3:2);
    if(positional.size()<2 || positional.size()>maximum)
        throw std::invalid_argument("Cantidad de argumentos incorrecta.");
    options.input=positional[0]; options.output=positional[1];
    if(positional.size()>2) {
        const std::string key=compress?"--range":"--iterations";
        if(seen.count(key)) throw std::invalid_argument("Parametro duplicado: "+key);
        if(compress) options.codec.range=number(positional[2]);
        else options.iterations=number(positional[2]);
    }
    if(positional.size()>3) {
        if(seen.count("--stride")) throw std::invalid_argument("Parametro duplicado: --stride");
        options.codec.stride=number(positional[3]);
    }
    if(compress && (options.codec.range<2 || options.codec.range>64 || options.codec.stride>2*options.codec.range))
        throw std::invalid_argument("Range fuera de 2..64 o stride fuera de 1..2*range.");
    if(decompress && options.iterations>10000)
        throw std::invalid_argument("Iteraciones fuera de 1..10000.");
    if(options.execution.model!=model)
        throw std::invalid_argument(std::string("Este ejecutable solo admite el modelo ")+fractal::model_name(model));
    if(seen.count("--quantum-domains") && model!=fractal::Model::FineGrained)
        throw std::invalid_argument("Cuantum requiere fine-grained.");
    if(seen.count("--trace-schedule") && model!=fractal::Model::FineGrained && model!=fractal::Model::CoarseGrained)
        throw std::invalid_argument("Traza requiere fine-grained o coarse-grained.");
    if(seen.count("--report-affinity") && model!=fractal::Model::CMP)
        throw std::invalid_argument("Reporte de afinidad requiere CMP.");
    if(decompress && options.execution.model!=fractal::Model::Sequential)
        throw std::invalid_argument(std::string("Decodificacion ")+fractal::model_name(model)+" no implementada; use sequential.");
    if(compress || decompress) fractal::validate_execution(options.execution);
    return options;
}
void report(const fractal::ExecutionConfig& execution) {
    std::cout<<"Modelo: "<<fractal::model_name(execution.model)
             <<"\nEquipos: "<<execution.teams<<"\nHilos por equipo: "<<execution.threads_per_team<<'\n';
    if(execution.model==fractal::Model::CMP)
        std::cout<<"CMP perfil: "<<fractal::cmp_profile_name(execution.cmp_profile)<<" (paralelismo con afinidad)\n";
    if(execution.model==fractal::Model::CoarseGrained)
        std::cout<<"Planificacion: rafagas cooperativas con stall simulado (>2/3 sin mejora)\n";
    if(execution.model==fractal::Model::FineGrained)
        std::cout<<"Planificacion: Round-Robin cooperativo (software)\nCuantum: "
                 <<execution.quantum_domains<<" dominio(s)\n";
}

}
int fractal::app::run(int argc,char** argv, Model model,CmpProfile profile) {
 try {
    if(argc==2 && std::string(argv[1])=="--help") {usage(std::cout,model,profile); return 0;}
    if(argc==2 && std::string(argv[1])=="--list-cpus" && model==Model::CMP) {
        const auto topology=detect_cpu_topology();
        (void)select_cmp_cpus(topology,1,profile);
        std::cout<<"environment="<<topology.environment<<" profile="<<cmp_profile_name(profile)
                 <<" distribution="<<topology.distribution<<" online_cpus="<<topology.online_cpus.size()
                 <<" online_cores="<<unique_physical_cores(topology.online_cpus).size()
                 <<" allowed_cores="<<topology.available_cores.size()<<'\n'
                 <<"processor="<<topology.processor<<"\nlogical_cpu,socket,core\n";
        for(const auto& cpu:topology.available_cores)
            std::cout<<cpu.cpu<<','<<cpu.socket<<','<<cpu.core<<'\n';
        return 0;
    }
    if(argc<4) {usage(std::cerr,model,profile); return 1;}
    auto options=parse(argc,argv,model,profile);
    std::vector<WorkerPlacement> placements;
    if(model==Model::CMP) options.execution.worker_placements=&placements;
    if(!options.affinity_path.empty()) {
        distinct(options.input.c_str(),options.affinity_path.c_str());
        distinct(options.output.c_str(),options.affinity_path.c_str());
    }
    std::vector<fractal::ScheduleEvent> trace;
    if(!options.trace_path.empty()) {
        distinct(options.input.c_str(),options.trace_path.c_str());
        distinct(options.output.c_str(),options.trace_path.c_str());
        options.execution.schedule_trace=&trace;
    }
    const auto& cmd=options.command;
    const char* input=options.input.c_str();
    const char* output=options.output.c_str();
    auto total=Clock::now();
    if(cmd=="compress" || cmd=="compress-gray") {
        distinct(input,output);
        report(options.execution);
        auto a=readImage(input,cmd=="compress-gray");
        auto start=Clock::now(); auto c=fractal::execute_encode(a,options.codec,options.execution); double elapsed=seconds(start);
        if(model==Model::CMP) {
            std::cout<<"Afinidad CMP verificada: ";
            for(const auto& w:placements) if(w.channel==0) std::cout<<"[H"<<w.worker<<"->CPU"<<w.cpu<<"] ";
            std::cout<<'\n';
            if(!options.affinity_path.empty()) {
                std::ofstream file(options.affinity_path);
                if(!file) throw std::runtime_error("No se pudo abrir reporte de afinidad.");
                file<<"profile,channel,worker,team,cpu,socket,core,observed_before,observed_after\n";
                for(const auto& w:placements)
                    file<<cmp_profile_name(profile)<<','<<w.channel<<','<<w.worker<<','<<w.team<<','<<w.cpu<<','
                        <<w.socket<<','<<w.core<<','<<w.observed_before<<','<<w.observed_after<<'\n';
                file.flush();
                if(!file) throw std::runtime_error("Error escribiendo reporte de afinidad.");
            }
        }
        saveMultiCode(output,c);
        if(!options.trace_path.empty()) {
            std::ofstream file(options.trace_path);
            if(!file) throw std::runtime_error("No se pudo abrir traza.");
            const bool coarse=model==fractal::Model::CoarseGrained;
            file<<"channel,worker,team,range,domain_begin,domain_end";
            if(coarse) file<<",reason,stagnant_domains,stall_threshold";
            file<<'\n';
            for(const auto& event:trace) {
                file<<event.channel<<','<<event.worker<<','<<event.team<<','<<event.range<<','
                    <<event.domain_begin<<','<<event.domain_end;
                if(coarse) file<<','<<fractal::schedule_reason_name(event.reason)<<','
                               <<event.stagnant_domains<<','<<event.stall_threshold;
                file<<'\n';
            }
            file.flush();
            if(!file) throw std::runtime_error("Error escribiendo traza.");
            std::cout<<(coarse?"Rafagas registradas: ":"Cuantums registrados: ")<<trace.size()<<'\n';
        }
        auto size=std::filesystem::file_size(output);
        std::cout<<"Canales: "<<c.planes.size()<<"\nReglas por canal: "<<c.planes[0].rules.size()
                 <<"\nBytes FRC: "<<size<<"\nRelacion contra pixeles sin comprimir: "
                 <<double(a.planes[0].pixels.size()*a.planes.size())/size<<":1\nCodificacion (sin E/S): "<<elapsed<<" s\n";
    } else if(cmd=="decompress") {
        distinct(input,output); auto c=loadMultiCode(input);
        report(options.execution);
        auto start=Clock::now(); auto a=fractal::execute_decode(c,options.iterations,options.execution); double elapsed=seconds(start);
        writeImage(output,a);
        std::cout<<"Canales: "<<c.planes.size()<<"\nDecodificacion (sin E/S): "<<elapsed<<" s\n";
    } else if(cmd=="compare" || cmd=="compare-gray") {
        bool gray=cmd=="compare-gray";
        auto a=readImage(input,gray),b=readImage(output,gray);
        auto e=fractal::mse(a,b); std::cout<<"MSE global [0,1]: "<<e<<"\nPSNR global: "<<psnr(e)<<" dB\n";
        const char* names[]={"R","G","B"};
        for(std::size_t c=0;c<a.planes.size();++c) {auto m=fractal::mse(a.planes[c],b.planes[c]); std::cout<<(gray?"Gris":names[c])<<": MSE="<<m<<", PSNR="<<psnr(m)<<" dB\n";}
    } else {usage(std::cerr,model,profile); return 1;}
    std::cout<<"Tiempo total (con E/S): "<<seconds(total)<<" s\n"; return 0;
 } catch(const std::exception& e) {std::cerr<<"Error: "<<e.what()<<'\n'; return 1;}
}
