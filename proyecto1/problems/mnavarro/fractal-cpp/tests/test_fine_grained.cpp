#include "fractal/execution.hpp"
#include "fractal/search.hpp"
#include <iostream>
#include <stdexcept>
#include <algorithm>

void require(bool ok) {if(!ok) throw std::runtime_error("Prueba Fine-Grained fallida.");}
bool same(const fractal::Rule& a,const fractal::Rule& b) {
    return a.x==b.x && a.y==b.y && a.symmetry==b.symmetry &&
           a.contrast==b.contrast && a.brightness==b.brightness;
}
std::size_t boundary(std::size_t i,std::size_t total,std::size_t parts) {
    return i*(total/parts)+std::min(i,total%parts);
}
// Reproduce solo el protocolo esperado desde la traza: verifica turnos, cobertura,
// barrera de mínimos, saltos de inactivos y límites de cuantum sin inspeccionar el scheduler.
void verify_trace(const std::vector<fractal::ScheduleEvent>& trace, std::size_t ranges,
                  std::size_t domains,const fractal::ExecutionConfig& config,std::size_t channels) {
    struct State {std::size_t range,range_end,begin,end,next; bool ready;};
    const auto count=config.teams*config.threads_per_team;
    std::size_t eventIndex=0;
    for(std::size_t channel=0;channel<channels;++channel) {
        std::vector<State> state;
        for(std::size_t id=0;id<count;++id) {
            const auto team=id/config.threads_per_team,member=id%config.threads_per_team;
            const auto range=boundary(team,ranges,config.teams),rangeEnd=boundary(team+1,ranges,config.teams);
            const auto begin=boundary(member,domains,config.threads_per_team),end=boundary(member+1,domains,config.threads_per_team);
            state.push_back({range,rangeEnd,begin,end,begin,range<rangeEnd && begin<end});
        }
        std::size_t previous=count-1;
        while(true) {
            std::size_t expected=previous;
            bool found=false;
            for(std::size_t n=0;n<count;++n) {
                expected=(expected+1)%count;
                if(state[expected].ready) {found=true; break;}
            }
            if(!found) break;
            require(eventIndex<trace.size());
            const auto& event=trace[eventIndex++];
            auto& current=state[expected];
            const auto team=expected/config.threads_per_team;
            require(event.channel==channel && event.worker==expected && event.team==team && event.range==current.range);
            require(event.domain_begin==current.next);
            require(event.domain_end==current.next+std::min(config.quantum_domains,current.end-current.next));
            current.next=event.domain_end;
            current.ready=current.next<current.end;
            bool waiting=false;
            for(std::size_t id=team*config.threads_per_team;id<(team+1)*config.threads_per_team;++id)
                waiting=waiting || state[id].ready;
            if(!waiting) {
                for(std::size_t id=team*config.threads_per_team;id<(team+1)*config.threads_per_team;++id) {
                    auto& member=state[id];
                    ++member.range;
                    member.next=member.begin;
                    member.ready=member.range<member.range_end && member.begin<member.end;
                }
            }
            previous=expected;
        }
        for(const auto& worker:state) require(worker.range==worker.range_end);
    }
    require(eventIndex==trace.size());
}
int main() {
    try {
        std::size_t cases=0;
        for(bool constant:{true,false}) {
            fractal::Image image{16,24,std::vector<float>(16*24,0.37f)};
            if(!constant) for(std::size_t i=0;i<image.pixels.size();++i) image.pixels[i]=float((i*17+i/7)%101)/100;
            for(fractal::Config codec: {fractal::Config{4,3},fractal::Config{8,16}}) {
                const auto reference=fractal::encode(image,codec);
                const auto problem=fractal::prepare_encoding(image,codec);
                for(auto layout: {std::pair<std::size_t,std::size_t>{1,1},{1,3},{5,2},{7,4}}) {
                    for(std::size_t quantum: {1,2,100}) {
                        std::vector<fractal::ScheduleEvent> trace;
                        fractal::ExecutionConfig execution{fractal::Model::FineGrained,layout.first,layout.second,quantum,&trace};
                        const auto result=fractal::execute_encode(image,codec,execution);
                        require(result.rules.size()==reference.rules.size());
                        for(std::size_t i=0;i<result.rules.size();++i) require(same(result.rules[i],reference.rules[i]));
                        verify_trace(trace,problem.range_count(),problem.domains.size(),execution,1);
                        require(fractal::mse(fractal::decode(result,40),fractal::decode(reference,40))==0);
                        ++cases;
                    }
                }
            }
        }
        fractal::MultiImage rgb;
        for(float value:{0.1f,0.5f,0.9f}) rgb.planes.push_back({16,16,std::vector<float>(256,value)});
        std::vector<fractal::ScheduleEvent> trace;
        fractal::ExecutionConfig execution{fractal::Model::FineGrained,5,2,1,&trace};
        const auto result=fractal::execute_encode(rgb,{4,4},execution);
        const auto reference=fractal::encode(rgb,{4,4});
        require(fractal::mse(fractal::decode(result),fractal::decode(reference))==0);
        verify_trace(trace,16,9,execution,3);
        // Configuración inválida y decodificación fuera del alcance deben rechazarse.
        bool rejected=false;
        try {fractal::validate_execution({fractal::Model::FineGrained,1,1,0});}
        catch(const std::invalid_argument&) {rejected=true;}
        require(rejected);
        rejected=false;
        try {fractal::execute_decode(result,40,execution);} catch(const std::invalid_argument&) {rejected=true;}
        require(rejected);
        std::cout<<"OK: "<<cases<<" casos mono + RGB; reglas exactas, Round-Robin, cobertura de dominios, cuantums y barreras.\n";
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n'; return 1;}
}
