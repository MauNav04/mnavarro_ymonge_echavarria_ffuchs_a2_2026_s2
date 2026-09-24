#include "fractal/color.hpp"
#include "color_io.hpp"
#include "image_io.hpp"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
namespace {
using Clock=std::chrono::steady_clock;
double seconds(Clock::time_point start) {return std::chrono::duration<double>(Clock::now()-start).count();}
int number(const char* s) {std::size_t n; int v=std::stoi(s,&n); if(n!=std::string(s).size()) throw std::invalid_argument("Numero invalido."); return v;}
double psnr(double e) {return e==0?std::numeric_limits<double>::infinity():10*std::log10(1/e);}
void distinct(const char* in,const char* out) {
    namespace fs=std::filesystem;
    if(fs::weakly_canonical(in)==fs::weakly_canonical(out) || (fs::exists(out) && fs::equivalent(in,out)))
        throw std::invalid_argument("Entrada y salida deben ser archivos distintos.");
}
void usage() {
    std::cerr<<"Uso:\n  fractal compress entrada.png salida.frc [range=8] [stride=8]\n"
             <<"  fractal compress-gray entrada.png salida.frc [range=8] [stride=8]\n"
             <<"  fractal decompress entrada.frc salida.png [iteraciones=40]\n"
             <<"  fractal compare original.png reconstruida.png\n"
             <<"  fractal compare-gray original.png reconstruida.png\n";
}
}
int main(int argc,char** argv) {
 try {
    if(argc<4) {usage(); return 1;}
    const std::string cmd=argv[1]; auto total=Clock::now();
    if((cmd=="compress" || cmd=="compress-gray") && argc<=6) {
        distinct(argv[2],argv[3]); fractal::Config cfg;
        if(argc>=5) cfg.range=number(argv[4]);
        if(argc>=6) cfg.stride=number(argv[5]);
        auto a=readImage(argv[2],cmd=="compress-gray");
        auto start=Clock::now(); auto c=fractal::encode(a,cfg); double elapsed=seconds(start);
        saveMultiCode(argv[3],c);
        auto size=std::filesystem::file_size(argv[3]);
        std::cout<<"Canales: "<<c.planes.size()<<"\nReglas por canal: "<<c.planes[0].rules.size()
                 <<"\nBytes FRC: "<<size<<"\nRelacion contra pixeles sin comprimir: "
                 <<double(a.planes[0].pixels.size()*a.planes.size())/size<<":1\nCodificacion (sin E/S): "<<elapsed<<" s\n";
    } else if(cmd=="decompress" && argc<=5) {
        distinct(argv[2],argv[3]); auto c=loadMultiCode(argv[2]);
        int iterations=argc==5?number(argv[4]):40;
        auto start=Clock::now(); auto a=fractal::decode(c,iterations); double elapsed=seconds(start);
        writeImage(argv[3],a);
        std::cout<<"Canales: "<<c.planes.size()<<"\nDecodificacion (sin E/S): "<<elapsed<<" s\n";
    } else if((cmd=="compare" || cmd=="compare-gray") && argc==4) {
        bool gray=cmd=="compare-gray";
        auto a=readImage(argv[2],gray),b=readImage(argv[3],gray);
        auto e=fractal::mse(a,b); std::cout<<"MSE global [0,1]: "<<e<<"\nPSNR global: "<<psnr(e)<<" dB\n";
        const char* names[]={"R","G","B"};
        for(std::size_t c=0;c<a.planes.size();++c) {auto m=fractal::mse(a.planes[c],b.planes[c]); std::cout<<(gray?"Gris":names[c])<<": MSE="<<m<<", PSNR="<<psnr(m)<<" dB\n";}
    } else {usage(); return 1;}
    std::cout<<"Tiempo total (con E/S): "<<seconds(total)<<" s\n"; return 0;
 } catch(const std::exception& e) {std::cerr<<"Error: "<<e.what()<<'\n'; return 1;}
}
