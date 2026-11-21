#include "fractal/execution.hpp"
#include "fractal/search.hpp"
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>

void require(bool ok) {if(!ok) throw std::runtime_error("Prueba Coarse-Grained fallida.");}
bool same(const fractal::Rule& a,const fractal::Rule& b) {
    return a.x==b.x && a.y==b.y && a.symmetry==b.symmetry &&
           a.contrast==b.contrast && a.brightness==b.brightness;
}
struct Statistics {std::size_t stalls=0, completed=0, improvement_resets=0;};
// Verifica la traza dominio a dominio usando los errores reales del codec, sin consultar
// estados internos del planificador: primer evento válido, cobertura, reanudación y barrera.
Statistics verify(const fractal::Image& image,fractal::Config codec,
                  const fractal::ExecutionConfig& execution,const std::vector<fractal::ScheduleEvent>& trace) {
    const auto problem=fractal::prepare_encoding(image,codec);
    struct State {
        std::size_t range,end,begin,domain_end,next,streak=0;
        double best=std::numeric_limits<double>::infinity();
    };
    const auto count=execution.teams*execution.threads_per_team;
    std::vector<State> states;
    // Enumerar asignaciones equilibradas sin usar la función de reparto de producción.
    std::vector<std::size_t> range_bounds{0}, domain_bounds{0};
    for(std::size_t t=0;t<execution.teams;++t)
        range_bounds.push_back(range_bounds.back()+problem.range_count()/execution.teams+(t<problem.range_count()%execution.teams));
    for(std::size_t m=0;m<execution.threads_per_team;++m)
        domain_bounds.push_back(domain_bounds.back()+problem.domains.size()/execution.threads_per_team+(m<problem.domains.size()%execution.threads_per_team));
    for(std::size_t id=0;id<count;++id) {
        const auto t=id/execution.threads_per_team, m=id%execution.threads_per_team;
        states.push_back({range_bounds[t],range_bounds[t+1],domain_bounds[m],domain_bounds[m+1],domain_bounds[m]});
    }
    auto ready=[](const State& s){return s.range<s.end && s.next<s.domain_end;};
    std::size_t previous=count-1;
    Statistics stats;
    for(const auto& event:trace) {
        std::size_t expected=previous;
        bool found=false;
        for(std::size_t n=0;n<count;++n) {
            expected=(expected+1)%count;
            if(ready(states[expected])) {found=true; break;}
        }
        require(found && event.worker==expected && event.team==expected/execution.threads_per_team);
        auto& state=states[expected];
        require(event.channel==0 && event.range==state.range && event.domain_begin==state.next);
        const auto assigned=state.domain_end-state.begin;
        const auto threshold=2*assigned/3+1; // Datos pequeños del test: sin riesgo de overflow.
        require(event.stall_threshold==threshold);
        const auto target=fractal::prepare_range(image,problem,state.range);
        require(event.domain_end>state.next && event.domain_end<=state.domain_end);
        for(;state.next<event.domain_end;++state.next) {
            const auto candidate=fractal::search_domains(problem,target,state.next,state.next+1);
            if(candidate.error<state.best) {
                if(state.streak) ++stats.improvement_resets;
                state.best=candidate.error;
                state.streak=0;
            } else ++state.streak;
            // Si se cruzó el umbral con trabajo pendiente, la ráfaga debía terminar ahí.
            if(state.streak>=threshold && state.next+1<state.domain_end)
                require(state.next+1==event.domain_end);
        }
        require(event.stagnant_domains==state.streak);
        if(state.next<state.domain_end) {
            require(event.reason==fractal::ScheduleReason::Stall && state.streak==threshold);
            state.streak=0;
            ++stats.stalls;
        } else {
            require(event.reason==fractal::ScheduleReason::DomainComplete);
            ++stats.completed;
            const auto first=event.team*execution.threads_per_team;
            bool pending=false;
            for(std::size_t id=first;id<first+execution.threads_per_team;++id) pending=pending || ready(states[id]);
            if(!pending) for(std::size_t id=first;id<first+execution.threads_per_team;++id) {
                auto& member=states[id];
                ++member.range;
                member.next=member.begin;
                member.streak=0;
                member.best=std::numeric_limits<double>::infinity();
            }
        }
        previous=expected;
    }
    for(const auto& state:states) require(state.range==state.end);
    return stats;
}
int main() {
    try {
        std::size_t cases=0,totalStalls=0,totalResets=0;
        for(bool constant:{true,false}) {
            fractal::Image image{16,24,std::vector<float>(384,0.37f)};
            if(!constant) for(std::size_t i=0;i<image.pixels.size();++i) image.pixels[i]=float((i*17+i/7)%101)/100;
            for(fractal::Config codec:{fractal::Config{4,3},fractal::Config{8,16},fractal::Config{2,2}}) {
                const auto reference=fractal::encode(image,codec);
                for(auto layout:{std::pair<std::size_t,std::size_t>{1,1},{1,3},{5,2},{7,4},{1,23}}) {
                    std::vector<fractal::ScheduleEvent> trace;
                    fractal::ExecutionConfig execution{fractal::Model::CoarseGrained,layout.first,layout.second,1,&trace};
                    const auto result=fractal::execute_encode(image,codec,execution);
                    require(result.rules.size()==reference.rules.size());
                    for(std::size_t i=0;i<result.rules.size();++i) require(same(result.rules[i],reference.rules[i]));
                    require(fractal::mse(fractal::decode(result,40),fractal::decode(reference,40))==0);
                    const auto stats=verify(image,codec,execution,trace);
                    totalStalls+=stats.stalls; totalResets+=stats.improvement_resets;
                    if(codec.range==8) require(stats.stalls==0); // Un solo dominio: finaliza, no simula stall.
                    if(constant && codec.range==4 && layout.first==5 && layout.second==2) {
                        // 18 dominios / 2 = 9 por miembro; >2/3 implica 7 SIN mejora.
                        // El primero mejora: la ráfaga termina tras 8 dominios, con uno pendiente.
                        require(trace.front().domain_begin==0 && trace.front().domain_end==8);
                        require(trace.front().stagnant_domains==7 && trace.front().stall_threshold==7);
                        require(trace.front().reason==fractal::ScheduleReason::Stall);
                        require(stats.stalls>0);
                    }
                    ++cases;
                }
            }
        }
        require(totalStalls>0 && totalResets>0);
        fractal::MultiImage rgb;
        for(float v:{0.1f,0.5f,0.9f}) rgb.planes.push_back({16,24,std::vector<float>(384,v)});
        std::vector<fractal::ScheduleEvent> trace;
        fractal::ExecutionConfig execution{fractal::Model::CoarseGrained,5,2,1,&trace};
        const auto result=fractal::execute_encode(rgb,{4,3},execution), reference=fractal::encode(rgb,{4,3});
        require(fractal::mse(fractal::decode(result),fractal::decode(reference))==0);
        for(std::size_t c=0;c<3;++c) {
            std::vector<fractal::ScheduleEvent> channel;
            for(auto e:trace) if(e.channel==c) {e.channel=0; channel.push_back(e);}
            require(!channel.empty()); verify(rgb.planes[c],{4,3},execution,channel);
        }
        bool rejected=false;
        try {fractal::validate_execution({fractal::Model::CoarseGrained,1,1,2});}
        catch(const std::invalid_argument&) {rejected=true;}
        require(rejected);
        rejected=false;
        try {fractal::execute_decode(result,40,execution);} catch(const std::invalid_argument&) {rejected=true;}
        require(rejected);
        std::cout<<"OK: "<<cases<<" casos mono + RGB, "<<totalStalls<<" stalls verificados, "<<totalResets
                 <<" reinicios por mejora; reglas, cobertura, turnos y barreras exactos.\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n'; return 1;}
}
