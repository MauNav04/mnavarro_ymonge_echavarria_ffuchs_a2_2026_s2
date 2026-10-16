#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace fractal {
enum class CmpProfile { Local, Target };
enum class SmtState { On, Off };
const char* smt_state_name(SmtState state);
SmtState parse_smt_state(const std::string& name);
struct CpuCore {int cpu, socket, core;};
struct CpuTopology {
    std::vector<CpuCore> online_cpus;
    std::vector<CpuCore> available_cores;
    std::string environment, processor, distribution;
    std::vector<CpuCore> allowed_cpus;
    std::string smt_control = "unknown";
    int smt_active = -1;
};
// Un representante por (socket,core), conservando el primer CPU lógico permitido.
std::vector<CpuCore> unique_physical_cores(std::vector<CpuCore> cpus);
CpuTopology detect_cpu_topology();
std::vector<CpuCore> select_cmp_cpus(const CpuTopology& topology,std::size_t workers,CmpProfile profile);
const char* cmp_profile_name(CmpProfile profile);
std::vector<CpuCore> select_smt_cpus(const CpuTopology& topology,std::size_t workers,SmtState expected);
}
