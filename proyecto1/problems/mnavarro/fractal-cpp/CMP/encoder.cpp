#include "fractal/cmp.hpp"
#include "fractal/search.hpp"
#include <pthread.h>
#include <algorithm>
#include <exception>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <system_error>
#ifdef __linux__
#include <sched.h>
#endif

namespace fractal {
namespace {
void check(int status,const char* operation) {
    if(status) throw std::system_error(status,std::generic_category(),operation);
}
// Barrera reutilizable y cancelable: ninguna excepción deja esperando a los demás miembros.
class Barrier {
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    const std::size_t parties;
    std::size_t arrived=0,generation=0;
    bool cancelled=false;
public:
    explicit Barrier(std::size_t count): parties(count) {
        check(pthread_mutex_init(&mutex,nullptr),"pthread_mutex_init");
        const int status=pthread_cond_init(&changed,nullptr);
        if(status) {pthread_mutex_destroy(&mutex);check(status,"pthread_cond_init");}
    }
    ~Barrier() {pthread_cond_destroy(&changed);pthread_mutex_destroy(&mutex);}
    Barrier(const Barrier&)=delete;
    Barrier& operator=(const Barrier&)=delete;
    bool wait() {
        check(pthread_mutex_lock(&mutex),"pthread_mutex_lock");
        try {
            if(!cancelled) {
                const auto phase=generation;
                if(++arrived==parties) {
                    arrived=0; ++generation;
                    check(pthread_cond_broadcast(&changed),"pthread_cond_broadcast");
                } else while(!cancelled && phase==generation)
                    check(pthread_cond_wait(&changed,&mutex),"pthread_cond_wait");
            }
            const bool result=!cancelled;
            check(pthread_mutex_unlock(&mutex),"pthread_mutex_unlock");
            return result;
        } catch(...) {pthread_mutex_unlock(&mutex);throw;}
    }
    void cancel() noexcept {
        if(pthread_mutex_lock(&mutex)!=0) std::terminate();
        cancelled=true;
        if(pthread_cond_broadcast(&changed)!=0) std::terminate();
        if(pthread_mutex_unlock(&mutex)!=0) std::terminate();
    }
};
std::size_t boundary(std::size_t i,std::size_t count,std::size_t parts) {
    return i*(count/parts)+std::min(i,count%parts);
}
void bind_cpu(int cpu) {
#ifdef __linux__
    cpu_set_t mask;
    CPU_ZERO(&mask); CPU_SET(cpu,&mask);
    check(pthread_setaffinity_np(pthread_self(),sizeof(mask),&mask),"pthread_setaffinity_np");
#else
    (void)cpu;
    throw std::runtime_error("CMP requiere Linux.");
#endif
}
int verify_cpu(int cpu) {
#ifdef __linux__
    cpu_set_t effective;
    CPU_ZERO(&effective);
    check(pthread_getaffinity_np(pthread_self(),sizeof(effective),&effective),"pthread_getaffinity_np");
    const int observed=sched_getcpu();
    if(CPU_COUNT(&effective)!=1 || !CPU_ISSET(cpu,&effective) || observed!=cpu)
        throw std::runtime_error("Afinidad CMP efectiva distinta de la solicitada.");
    return observed;
#else
    (void)cpu;
    throw std::runtime_error("CMP requiere Linux.");
#endif
}
struct Team {
    std::size_t begin,end;
    RangeTarget target;
    std::vector<Candidate> candidates;
    Barrier barrier;
    Team(std::size_t first,std::size_t last,std::size_t members)
        :begin(first),end(last),candidates(members),barrier(members) {}
};
class Pool {
    const Image& image;
    const EncodingProblem problem;
    const ExecutionConfig execution;
    const std::size_t channel;
    const std::vector<CpuCore> cpus;
    Encoded result;
    std::vector<std::unique_ptr<Team>> teams;
    std::vector<WorkerPlacement> placements;
    Barrier startup;
    std::mutex failure_mutex;
    std::exception_ptr failure;
    struct Context {Pool* pool;std::size_t id;};
    void cancel(std::exception_ptr error) noexcept {
        {std::lock_guard<std::mutex> guard(failure_mutex); if(!failure) failure=error;}
        startup.cancel();
        for(auto& team:teams) team->barrier.cancel();
    }
    void work(std::size_t id) {
        const auto teamId=id/execution.threads_per_team,member=id%execution.threads_per_team;
        const auto& cpu=cpus[id];
        bind_cpu(cpu.cpu);
        placements[id]={channel,id,teamId,cpu.cpu,cpu.socket,cpu.core,verify_cpu(cpu.cpu),-1};
        // No iniciar cómputo hasta que TODOS los pthreads hayan fijado/verificado su afinidad.
        if(!startup.wait()) return;
        auto& team=*teams[teamId];
        const auto first=boundary(member,problem.domains.size(),execution.threads_per_team);
        const auto last=boundary(member+1,problem.domains.size(),execution.threads_per_team);
        for(auto range=team.begin;range<team.end;++range) {
            if(member==0) team.target=prepare_range(image,problem,range);
            if(!team.barrier.wait()) return;
            // Cómputo concurrente SIN mutex global. Un slot privado por miembro del equipo.
            team.candidates[member]=search_domains(problem,team.target,first,last);
            if(!team.barrier.wait()) return;
            if(member==0) {
                Candidate best;
                for(const auto& candidate:team.candidates) best=best_candidate(best,candidate);
                result.rules[range]=best.rule; // Índice exclusivo del líder de este equipo.
            }
            if(!team.barrier.wait()) return; // No reutilizar buffers antes de publicar la regla.
        }
        placements[id].observed_after=verify_cpu(cpu.cpu);
    }
    static void* entry(void* pointer) noexcept {
        auto& context=*static_cast<Context*>(pointer);
        try {context.pool->work(context.id);}
        catch(...) {context.pool->cancel(std::current_exception());}
        return nullptr;
    }
public:
    Pool(const Image& a,Config config,const ExecutionConfig& e,std::size_t c)
        :image(a),problem(prepare_encoding(a,config)),execution(e),channel(c),
         cpus(select_cmp_cpus(detect_cpu_topology(),e.teams*e.threads_per_team,e.cmp_profile)),
         result{a.width,a.height,config.range,{}},placements(cpus.size()),startup(cpus.size()+1) {
        result.rules.resize(problem.range_count());
        for(std::size_t team=0;team<e.teams;++team)
            teams.push_back(std::make_unique<Team>(boundary(team,problem.range_count(),e.teams),
                            boundary(team+1,problem.range_count(),e.teams),e.threads_per_team));
    }
    Encoded run() {
        std::vector<pthread_t> threads(cpus.size());
        std::vector<Context> contexts(cpus.size());
        std::size_t created=0;
        for(;created<threads.size();++created) {
            contexts[created]={this,created};
            const int status=pthread_create(&threads[created],nullptr,entry,&contexts[created]);
            if(status) {
                cancel(std::make_exception_ptr(std::system_error(status,std::generic_category(),"pthread_create")));
                break;
            }
        }
        if(created==threads.size()) {
            try {(void)startup.wait();} catch(...) {cancel(std::current_exception());}
        }
        for(std::size_t id=0;id<created;++id)
            if(pthread_join(threads[id],nullptr)!=0) std::terminate();
        if(failure) std::rethrow_exception(failure);
        if(execution.worker_placements)
            execution.worker_placements->insert(execution.worker_placements->end(),placements.begin(),placements.end());
        return std::move(result);
    }
};
}
Encoded encode_cmp(const Image& image,Config config,const ExecutionConfig& execution,std::size_t channel) {
    validate_execution(execution);
    if(execution.model!=Model::CMP) throw std::invalid_argument("Se requiere CMP.");
    return Pool(image,config,execution,channel).run();
}
}
