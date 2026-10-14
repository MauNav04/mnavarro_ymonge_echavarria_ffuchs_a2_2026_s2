#include "fractal/execution.hpp"
#include "fractal/search.hpp"
#include <iostream>
#include <stdexcept>
#include <limits>

void require(bool ok) { if(!ok) throw std::runtime_error("Prueba de ejecucion fallida."); }
template<class F> void rejects(F f) {
    bool rejected=false;
    try { f(); } catch(const std::invalid_argument&) { rejected=true; }
    require(rejected);
}
bool same(const fractal::Rule& a, const fractal::Rule& b) {
    return a.x==b.x && a.y==b.y && a.symmetry==b.symmetry &&
           a.contrast==b.contrast && a.brightness==b.brightness;
}
int main() {
    try {
        // Constante fuerza empates; patrón no simétrico ejerce búsqueda y selección real.
        for(bool constant : {true,false}) {
            fractal::Image image{16,24,std::vector<float>(16*24,0.37f)};
            if(!constant) for(int y=0;y<24;++y) for(int x=0;x<16;++x)
                image.at(x,y)=static_cast<float>((x*13+y*7+x*y)%101)/100;
            const auto problem=fractal::prepare_encoding(image,{4,3});
            const auto encoded=fractal::execute_encode(image,{4,3});
            require(problem.range_count()==24 && problem.domains.size()==18);
            require(fractal::mse(fractal::execute_decode(encoded,40),fractal::decode(encoded,40))==0);
            for(std::size_t range=0;range<problem.range_count();++range) {
                const auto target=fractal::prepare_range(image,problem,range);
                const auto full=fractal::search_domains(problem,target,0,problem.domains.size());
                require(same(full.rule,encoded.rules[range]));
                if(constant) require(full.order==0 && full.rule.symmetry==0 && full.rule.x==0 && full.rule.y==0);
                // Incluye más particiones que dominios y combina en orden inverso.
                for(std::size_t partitions : {1,2,5,23}) {
                    fractal::Candidate combined;
                    for(std::size_t worker=partitions;worker>0;--worker) {
                        const std::size_t i=worker-1;
                        const auto local=fractal::search_domains(problem,target,
                            i*problem.domains.size()/partitions,(i+1)*problem.domains.size()/partitions);
                        combined=fractal::best_candidate(combined,local);
                    }
                    require(full.error==combined.error && full.order==combined.order && same(full.rule,combined.rule));
                }
                rejects([&]{fractal::search_domains(problem,target,2,1);});
                rejects([&]{fractal::search_domains(problem,target,0,problem.domains.size()+1);});
            }
            rejects([&]{fractal::prepare_range(image,problem,problem.range_count());});
            const fractal::MultiImage multi{{image,image,image}};
            const auto result=fractal::execute_encode(multi,{4,3});
            const auto decoded=fractal::execute_decode(result,40);
            require(fractal::mse(decoded,fractal::decode(fractal::encode(multi,{4,3}),40))==0);
        }
        for(auto name : {"coarse-grained","CMP","SMT"}) {
            const auto model=fractal::parse_model(name);
            rejects([&]{fractal::validate_execution({model,5,2});});
        }
        require(fractal::parse_model("secuencial")==fractal::Model::Sequential);
        rejects([]{fractal::parse_model("unknown");});
        rejects([]{fractal::validate_execution({fractal::Model::Sequential,0,1});});
        rejects([]{fractal::validate_execution({fractal::Model::Sequential,5,2});});
        rejects([]{fractal::validate_execution({fractal::Model::Sequential,1,2});});
        rejects([]{fractal::validate_execution({fractal::Model::Sequential,std::numeric_limits<std::size_t>::max(),2});});
        std::cout<<"OK: minimos parciales, empates deterministas, RGB, ejecucion y configuraciones invalidas.\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
