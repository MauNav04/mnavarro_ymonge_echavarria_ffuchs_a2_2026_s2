#include "fractal/smt.hpp"
#include <iostream>
#include <set>
#include <map>
#include <stdexcept>
#include <algorithm>
using namespace fractal;
void require(bool ok) {if(!ok) throw std::runtime_error("Prueba SMT fallida.");}
template<class F> void rejects(F operation) {
    bool failed=false;try {operation();} catch(const std::invalid_argument&) {failed=true;}require(failed);
}
CpuTopology fixture(bool on) {
    CpuTopology t;
    t.environment="native-or-unknown";t.distribution="ubuntu";t.processor="AMD Ryzen fixture";
    t.smt_control=on?"on":"off";t.smt_active=on?1:0;
    // Numeración dispersa: nunca inferir hermanos por paridad o proximidad de IDs.
    for(int i=0;i<8;++i) {
        t.online_cpus.push_back({3*i,0,i});
        if(on) t.online_cpus.push_back({40+3*i,0,i});
    }
    t.allowed_cpus=t.online_cpus;t.available_cores=unique_physical_cores(t.allowed_cpus);
    return t;
}
void equivalent(const Encoded& a,const Encoded& b) {
    require(a.rules.size()==b.rules.size());
    for(std::size_t i=0;i<a.rules.size();++i) {
        const auto& x=a.rules[i];const auto& y=b.rules[i];
        require(x.x==y.x && x.y==y.y && x.symmetry==y.symmetry && x.contrast==y.contrast && x.brightness==y.brightness);
    }
    require(mse(decode(a,40),decode(b,40))==0);
}
int hardware() {
    CpuTopology topology;SmtState state;
    try {
        topology=detect_cpu_topology();state=topology.smt_active==0?SmtState::Off:SmtState::On;
        (void)select_smt_cpus(topology,16,state);
        require(topology.available_cores.size()==8 && topology.allowed_cpus.size()==(state==SmtState::On?16u:8u));
    } catch(const std::exception& e) {std::cout<<"SKIP hardware Ubuntu AMD Ryzen 8/16: "<<e.what()<<'\n';return 77;}
    // Solo se llega aquí sobre el destino real. No se sustituye la topología detectada.
    for(auto layout:{std::pair<std::size_t,std::size_t>{1,16},{4,4},{16,1},{1,1},{2,2}}) {
        MultiImage image;
        for(int c=0;c<3;++c) {
            Image plane{16,24,std::vector<float>(384)};
            for(std::size_t i=0;i<plane.pixels.size();++i) plane.pixels[i]=float((i*17+i/7+c*11)%101)/100;
            image.planes.push_back(plane);
        }
        std::vector<WorkerPlacement> placements;
        ExecutionConfig e{Model::SMT,layout.first,layout.second};e.smt_state=state;e.worker_placements=&placements;
        for(auto config:{Config{4,3},Config{8,16},Config{2,4}}) {
            placements.clear();auto result=execute_encode(image,config,e),reference=encode(image,config);
            const auto count=layout.first*layout.second;
            require(placements.size()==3*count);
            for(std::size_t c=0;c<3;++c) {
                equivalent(result.planes[c],reference.planes[c]);
                std::map<int,int> used;
                for(std::size_t i=0;i<count;++i) {
                    const auto& w=placements[c*count+i];
                    require(w.channel==c && w.worker==i && w.team==i/layout.second);
                    require(w.cpu==w.observed_before && w.cpu==w.observed_after);++used[w.cpu];
                }
                if(count==16) {require(used.size()==(state==SmtState::On?16u:8u));for(auto v:used) require(v.second==(state==SmtState::On?1:2));}
            }
        }
    }
    std::cout<<"OK SMT fisico "<<smt_state_name(state)<<": 15 casos RGB, reglas/reconstruccion exactas y afinidad verificada.\n";return 0;
}
int main(int argc,char**) {
    try {
        if(argc>1) return hardware();
        require(parse_smt_state("on")==SmtState::On && parse_smt_state("off")==SmtState::Off);
        rejects([]{parse_smt_state("sw");});
        for(bool on:{false,true}) {
            const auto state=on?SmtState::On:SmtState::Off;
            auto t=fixture(on);
            auto selected=select_smt_cpus(t,16,state);
            std::map<int,int> used;
            for(auto cpu:selected) ++used[cpu.cpu];
            require(used.size()==(on?16u:8u));for(auto item:used) require(item.second==(on?1:2));
            if(on) {std::set<int> cores;for(int i=0;i<8;++i) cores.insert(selected[i].core);require(cores.size()==8);}
            for(std::size_t n=1;n<=16;++n) require(select_smt_cpus(t,n,state).size()==n);
            rejects([&]{select_smt_cpus(t,0,state);});rejects([&]{select_smt_cpus(t,17,state);});
            rejects([&]{select_smt_cpus(t,16,on?SmtState::Off:SmtState::On);});
            auto wrong=t;wrong.smt_active=-1;rejects([&]{select_smt_cpus(wrong,1,state);});
            wrong=t;wrong.smt_control="unknown";rejects([&]{select_smt_cpus(wrong,1,state);});
            wrong=t;wrong.environment="wsl";rejects([&]{select_smt_cpus(wrong,1,state);});
            wrong=t;wrong.processor="Intel";rejects([&]{select_smt_cpus(wrong,1,state);});
            wrong=t;wrong.distribution="other";rejects([&]{select_smt_cpus(wrong,1,state);});
            wrong=t;wrong.online_cpus.pop_back();rejects([&]{select_smt_cpus(wrong,1,state);});
            wrong=t;wrong.allowed_cpus.push_back(wrong.allowed_cpus[0]);rejects([&]{select_smt_cpus(wrong,1,state);});
            wrong=t;wrong.allowed_cpus[0].cpu=999;rejects([&]{select_smt_cpus(wrong,1,state);});
            auto limited=t;limited.allowed_cpus={t.allowed_cpus[0],t.allowed_cpus.back()};limited.available_cores=unique_physical_cores(limited.allowed_cpus);
            require(select_smt_cpus(limited,on?2:4,state).size()==(on?2u:4u));
            rejects([&]{select_smt_cpus(limited,on?3:5,state);});
        }
        auto bad=fixture(true);bad.online_cpus[1].core=1;
        rejects([&]{select_smt_cpus(bad,1,SmtState::On);});
        auto off=fixture(false);off.smt_control="forceoff";require(select_smt_cpus(off,16,SmtState::Off).size()==16);
        ExecutionConfig invalid{Model::Sequential};invalid.smt_state=SmtState::Off;rejects([&]{validate_execution(invalid);});
        std::cout<<"OK contratos SMT: fixtures ON/OFF, IDs dispersos, afinidad restringida, estados y destinos incompatibles. No es validacion fisica.\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
