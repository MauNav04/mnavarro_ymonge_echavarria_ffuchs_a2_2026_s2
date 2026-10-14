#include "fractal/fine_grained.hpp"
#include "fractal/search.hpp"
#include <pthread.h>
#include <algorithm>
#include <exception>
#include <system_error>

namespace fractal {
namespace {
void check(int code, const char* operation) {
    if(code) throw std::system_error(code,std::generic_category(),operation);
}
// Reparto contiguo equilibrado sin multiplicar total*index (evita overflow).
std::size_t boundary(std::size_t index, std::size_t total, std::size_t parts) {
    return index*(total/parts)+std::min(index,total%parts);
}
struct Worker {
    std::size_t id, team, begin, end, next;
    Candidate best;
    bool ready=false;
};
struct Team {
    std::size_t range, end, pending=0;
    RangeTarget target;
};
class Scheduler {
    const Image& image;
    const EncodingProblem problem;
    const ExecutionConfig execution;
    const std::size_t channel;
    Encoded result;
    std::vector<Team> teams;
    std::vector<Worker> workers;
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    bool started=false, stopped=false;
    std::size_t turn=0;
    std::exception_ptr failure;
    struct Context { Scheduler* scheduler; std::size_t id; };

    void reset_team(std::size_t id) {
        auto& team=teams[id];
        team.pending=0;
        if(team.range<team.end) team.target=prepare_range(image,problem,team.range);
        for(std::size_t member=0;member<execution.threads_per_team;++member) {
            auto& worker=workers[id*execution.threads_per_team+member];
            worker.next=worker.begin;
            worker.best=Candidate{};
            worker.ready=team.range<team.end && worker.begin<worker.end;
            if(worker.ready) ++team.pending;
        }
    }
    bool select_next(std::size_t previous) {
        std::size_t next=previous;
        for(std::size_t checked=0;checked<workers.size();++checked) {
            next=(next+1)%workers.size();
            if(workers[next].ready) {turn=next; return true;}
        }
        return false;
    }
    void quantum(std::size_t id) {
        auto& worker=workers[id];
        auto& team=teams[worker.team];
        const auto begin=worker.next;
        const auto end=begin+std::min(execution.quantum_domains,worker.end-begin);
        // El mutex mantiene exclusión durante el cuantum: solo el trabajador con turno busca.
        // Los demás esperan en una condición, sin sondeo ni yield como sustituto de sincronización.
        worker.best=best_candidate(worker.best,search_domains(problem,team.target,begin,end));
        worker.next=end;
        if(execution.schedule_trace)
            execution.schedule_trace->push_back({channel,id,worker.team,team.range,begin,end});
        if(end==worker.end) {
            worker.ready=false;
            if(--team.pending==0) {
                Candidate best;
                for(std::size_t member=0;member<execution.threads_per_team;++member)
                    best=best_candidate(best,workers[worker.team*execution.threads_per_team+member].best);
                // Barrera cooperativa del equipo: publicar solo tras recibir todos los mínimos.
                result.rules[team.range]=best.rule;
                ++team.range;
                reset_team(worker.team);
            }
        }
        // Cambio incondicional tras cada cuantum; omite miembros sin dominios o equipos finalizados.
        if(!select_next(id)) stopped=true;
    }
    static void* entry(void* pointer) noexcept {
        auto& context=*static_cast<Context*>(pointer);
        auto& scheduler=*context.scheduler;
        // Estos objetos están inicializados y no se destruyen hasta reunir todos los hilos.
        if(pthread_mutex_lock(&scheduler.mutex)!=0) std::terminate();
        try {
            while(!scheduler.stopped) {
                while(!scheduler.stopped && (!scheduler.started || scheduler.turn!=context.id))
                    check(pthread_cond_wait(&scheduler.changed,&scheduler.mutex),"pthread_cond_wait");
                if(scheduler.stopped) break;
                scheduler.quantum(context.id);
                check(pthread_cond_broadcast(&scheduler.changed),"pthread_cond_broadcast");
            }
        } catch(...) {
            scheduler.failure=std::current_exception();
            scheduler.stopped=true;
            pthread_cond_broadcast(&scheduler.changed);
        }
        if(pthread_mutex_unlock(&scheduler.mutex)!=0) std::terminate();
        return nullptr;
    }
public:
    Scheduler(const Image& a, Config config, const ExecutionConfig& e, std::size_t c)
        : image(a), problem(prepare_encoding(a,config)), execution(e), channel(c),
          result{a.width,a.height,config.range,{}}, teams(e.teams),
          workers(e.teams*e.threads_per_team) {
        result.rules.resize(problem.range_count());
        for(std::size_t team=0;team<e.teams;++team) {
            teams[team]={boundary(team,problem.range_count(),e.teams),
                         boundary(team+1,problem.range_count(),e.teams),0,{}};
            for(std::size_t member=0;member<e.threads_per_team;++member) {
                const auto id=team*e.threads_per_team+member;
                const auto begin=boundary(member,problem.domains.size(),e.threads_per_team);
                const auto end=boundary(member+1,problem.domains.size(),e.threads_per_team);
                workers[id]={id,team,begin,end,begin,{},false};
            }
            reset_team(team);
        }
        check(pthread_mutex_init(&mutex,nullptr),"pthread_mutex_init");
        const int status=pthread_cond_init(&changed,nullptr);
        if(status) {pthread_mutex_destroy(&mutex); check(status,"pthread_cond_init");}
    }
    ~Scheduler() {pthread_cond_destroy(&changed); pthread_mutex_destroy(&mutex);}
    Scheduler(const Scheduler&)=delete;
    Scheduler& operator=(const Scheduler&)=delete;
    Encoded run() {
        std::vector<pthread_t> threads(workers.size());
        std::vector<Context> contexts(workers.size());
        std::size_t created=0;
        int creation_error=0;
        for(;created<threads.size();++created) {
            contexts[created]={this,created};
            creation_error=pthread_create(&threads[created],nullptr,entry,&contexts[created]);
            if(creation_error) break;
        }
        // Ningún trabajador busca hasta completar la creación; un fallo despierta y reúne
        // los ya creados antes de propagar el error, conservando vivos contextos y buffers.
        if(pthread_mutex_lock(&mutex)!=0) std::terminate();
        started=true;
        stopped=creation_error!=0 || !select_next(workers.size()-1);
        pthread_cond_broadcast(&changed);
        if(pthread_mutex_unlock(&mutex)!=0) std::terminate();
        for(std::size_t id=0;id<created;++id) {
            // Con hilos joinables propios y una única reunión, un error es una invariante rota.
            if(pthread_join(threads[id],nullptr)!=0) std::terminate();
        }
        check(creation_error,"pthread_create");
        if(failure) std::rethrow_exception(failure);
        return std::move(result);
    }
};
}
Encoded encode_fine_grained(const Image& image, Config config,
                            const ExecutionConfig& execution, std::size_t channel) {
    validate_execution(execution);
    if(execution.model!=Model::FineGrained) throw std::invalid_argument("Se requiere fine-grained.");
    return Scheduler(image,config,execution,channel).run();
}
}
