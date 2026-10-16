#include "fractal/cmp.hpp"
#include <algorithm>
#include <iostream>
#include <set>
#include <stdexcept>
#include <utility>

void require(bool ok) {if(!ok) throw std::runtime_error("Prueba CMP fallida.");}
template<class F> void rejects(F operation) {
    bool rejected=false;
    try {operation();} catch(const std::invalid_argument&) {rejected=true;}
    require(rejected);
}
bool same(const fractal::Rule& a,const fractal::Rule& b) {
    return a.x==b.x && a.y==b.y && a.symmetry==b.symmetry &&
           a.contrast==b.contrast && a.brightness==b.brightness;
}
void equivalent(const fractal::Encoded& a,const fractal::Encoded& b) {
    require(a.rules.size()==b.rules.size());
    for(std::size_t i=0;i<a.rules.size();++i) require(same(a.rules[i],b.rules[i]));
    require(fractal::mse(fractal::decode(a,40),fractal::decode(b,40))==0);
}
int main(int argc,char**) {
    try {
        // Fixtures de selección: validan contratos, no constituyen mediciones físicas.
        auto sockets=fractal::unique_physical_cores({{3,1,0},{1,0,0},{0,0,0},{2,1,0}});
        require(sockets.size()==2 && sockets[0].cpu==0 && sockets[1].cpu==2);
        auto restricted=fractal::unique_physical_cores({{1,0,0},{3,0,1}});
        require(restricted.size()==2 && restricted[0].cpu==1);
        rejects([]{fractal::unique_physical_cores({{0,0,0},{0,1,0}});});
        fractal::CpuTopology target;
        target.environment="native-or-unknown";
        target.distribution="ubuntu";
        target.processor="AMD Ryzen (fixture 8/16, no hardware medido)";
        for(int core=0;core<8;++core) {
            target.online_cpus.push_back({2*core,0,core});
            target.online_cpus.push_back({2*core+1,0,core});
        }
        target.available_cores=fractal::unique_physical_cores(target.online_cpus);
        require(fractal::select_cmp_cpus(target,8).size()==8);
        rejects([&]{fractal::select_cmp_cpus(target,9);});
        auto wrongProcessor=target; wrongProcessor.processor="Intel (fixture)";
        rejects([&]{fractal::select_cmp_cpus(wrongProcessor,1);});
        auto wrongSystem=target; wrongSystem.distribution="other";
        rejects([&]{fractal::select_cmp_cpus(wrongSystem,1);});
        auto virtualTarget=target; virtualTarget.environment="wsl";
        rejects([&]{fractal::select_cmp_cpus(virtualTarget,1);});
        auto noSmt=target; noSmt.online_cpus=noSmt.available_cores;
        require(fractal::select_cmp_cpus(noSmt,8).size()==8);
        auto limited=target; limited.available_cores.resize(2);
        require(fractal::select_cmp_cpus(limited,2).size()==2);
        rejects([&]{fractal::select_cmp_cpus(limited,3);});
        auto wrong=target; wrong.online_cpus.resize(12);
        rejects([&]{fractal::select_cmp_cpus(wrong,2);});
        if(argc==1) {std::cout<<"OK CMP: contratos de topologia de evaluacion (fixtures).\n";return 0;}
        fractal::CpuTopology topology;
        try {topology=fractal::detect_cpu_topology();(void)fractal::select_cmp_cpus(topology,1);}
        catch(const std::exception& e) {std::cout<<"SKIP ejecucion CMP: "<<e.what()<<'\n'; return 77;}
        const auto available=topology.available_cores.size();
        rejects([&]{fractal::select_cmp_cpus(topology,available+1);});
        std::size_t cases=0,maxWorkers=0;
        for(int pattern=0;pattern<3;++pattern) {
            fractal::Image image{16,24,std::vector<float>(384,0.37f)};
            if(pattern) for(std::size_t i=0;i<image.pixels.size();++i)
                image.pixels[i]=pattern==1?float(i%16)/15:float((i*17+i/7)%101)/100;
            for(fractal::Config codec:{fractal::Config{4,3},fractal::Config{8,16},fractal::Config{2,4}}) {
                const auto reference=fractal::encode(image,codec);
                for(auto layout:{std::pair<std::size_t,std::size_t>{1,1},{2,1},{1,2},{2,2},{3,2},{6,1}}) {
                    const auto workers=layout.first*layout.second;
                    if(workers>available) continue;
                    std::vector<fractal::WorkerPlacement> placements;
                    fractal::ExecutionConfig execution{fractal::Model::CMP,layout.first,layout.second};
                    execution.worker_placements=&placements;
                    const auto result=fractal::execute_encode(image,codec,execution);
                    equivalent(reference,result);
                    require(placements.size()==workers);
                    std::set<std::pair<int,int>> cores;
                    for(std::size_t id=0;id<workers;++id) {
                        const auto& w=placements[id];
                        require(w.worker==id && w.team==id/layout.second && w.channel==0);
                        require(w.cpu==w.observed_before && w.cpu==w.observed_after);
                        require(cores.emplace(w.socket,w.core).second);
                        require(std::any_of(topology.available_cores.begin(),topology.available_cores.end(),
                            [&](const fractal::CpuCore& c){return c.cpu==w.cpu;}));
                    }
                    maxWorkers=std::max(maxWorkers,workers); ++cases;
                }
            }
        }
        // Equipos sin ranges cuando hay más núcleos disponibles que bloques.
        fractal::Image small{8,8,std::vector<float>(64,0.25f)};
        equivalent(fractal::encode(small,{4,4}),fractal::execute_encode(small,{4,4},{fractal::Model::CMP,available,1}));
        fractal::MultiImage rgb;
        for(float value:{0.2f,0.5f,0.8f}) rgb.planes.push_back({16,24,std::vector<float>(384,value)});
        std::vector<fractal::WorkerPlacement> placements;
        fractal::ExecutionConfig execution{fractal::Model::CMP,1,std::min<std::size_t>(2,available)};
        execution.worker_placements=&placements;
        const auto result=fractal::execute_encode(rgb,{4,3},execution),reference=fractal::encode(rgb,{4,3});
        for(std::size_t channel=0;channel<3;++channel) equivalent(result.planes[channel],reference.planes[channel]);
        require(placements.size()==3*execution.threads_per_team);
        rejects([&]{fractal::execute_encode(small,{4,4},{fractal::Model::CMP,available+1,1});});
        rejects([&]{fractal::execute_decode(result,40,execution);});
        execution.quantum_domains=2;
        rejects([&]{fractal::validate_execution(execution);});
        execution.quantum_domains=1;
        std::vector<fractal::ScheduleEvent> trace; execution.schedule_trace=&trace;
        rejects([&]{fractal::validate_execution(execution);});
        std::cout<<"OK CMP evaluacion: "<<cases<<" casos mono + RGB/equipos vacios; afinidad y reglas exactas, hasta "
                 <<maxWorkers<<" trabajadores; entorno="<<topology.environment<<". Hardware de destino verificado.\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
