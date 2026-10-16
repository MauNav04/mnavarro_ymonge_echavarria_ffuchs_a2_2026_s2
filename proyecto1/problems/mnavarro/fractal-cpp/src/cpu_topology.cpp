#include "fractal/cpu_topology.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>
#ifdef __linux__
#include <sched.h>
#include <sys/utsname.h>
#endif

namespace fractal {
const char* cmp_profile_name(CmpProfile profile) {
    switch(profile) {
        case CmpProfile::Local: return "local";
        case CmpProfile::Target: return "target";
    }
    throw std::invalid_argument("Perfil CMP invalido.");
}
std::vector<CpuCore> unique_physical_cores(std::vector<CpuCore> cpus) {
    std::sort(cpus.begin(),cpus.end(),[](const CpuCore& a,const CpuCore& b){return a.cpu<b.cpu;});
    std::set<int> logical;
    std::set<std::pair<int,int>> physical;
    std::vector<CpuCore> result;
    for(const auto& cpu:cpus) {
        if(cpu.cpu<0 || cpu.socket<0 || cpu.core<0 || !logical.insert(cpu.cpu).second)
            throw std::invalid_argument("Topologia de CPU invalida.");
        if(physical.emplace(cpu.socket,cpu.core).second) result.push_back(cpu);
    }
    return result;
}
CpuTopology detect_cpu_topology() {
#ifndef __linux__
    throw std::runtime_error("CMP requiere Linux con afinidad pthreads y sysfs.");
#else
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    if(sched_getaffinity(0,sizeof(allowed),&allowed)!=0)
        throw std::system_error(errno,std::generic_category(),"sched_getaffinity");
    std::ifstream onlineFile("/sys/devices/system/cpu/online");
    std::string online;
    if(!(onlineFile>>online)) throw std::runtime_error("No se pudo leer CPUs online de sysfs.");
    CpuTopology topology;
    std::vector<CpuCore> permitted;
    std::istringstream list(online);
    std::string entry;
    while(std::getline(list,entry,',')) {
        const auto dash=entry.find('-');
        const int first=std::stoi(entry.substr(0,dash));
        const int last=dash==std::string::npos?first:std::stoi(entry.substr(dash+1));
        if(first<0 || last<first || last>=CPU_SETSIZE)
            throw std::runtime_error("Lista de CPU no soportada por la mascara de afinidad.");
        for(int cpu=first;cpu<=last;++cpu) {
            const auto path="/sys/devices/system/cpu/cpu"+std::to_string(cpu)+"/topology/";
            CpuCore info{cpu,-1,-1};
            std::ifstream socket(path+"physical_package_id"),core(path+"core_id");
            if(!(socket>>info.socket) || !(core>>info.core) || info.socket<0 || info.core<0)
                throw std::runtime_error("Topologia fisica desconocida para CPU "+std::to_string(cpu));
            topology.online_cpus.push_back(info);
            if(CPU_ISSET(cpu,&allowed)) permitted.push_back(info);
        }
    }
    std::ifstream osRelease("/etc/os-release");
    std::string osLine;
    while(std::getline(osRelease,osLine)) if(osLine.rfind("ID=",0)==0) {
        topology.distribution=osLine.substr(3);
        if(topology.distribution.size()>=2 && topology.distribution.front()=='"' && topology.distribution.back()=='"')
            topology.distribution=topology.distribution.substr(1,topology.distribution.size()-2);
    }
    topology.available_cores=unique_physical_cores(permitted);
    if(topology.available_cores.empty()) throw std::runtime_error("No hay nucleos permitidos para CMP.");
    utsname system{};
    if(uname(&system)!=0) throw std::system_error(errno,std::generic_category(),"uname");
    std::string release=system.release;
    std::transform(release.begin(),release.end(),release.begin(),[](unsigned char c){return std::tolower(c);});
    topology.environment="native-or-unknown";
    std::ifstream cpuinfo("/proc/cpuinfo");
    std::string line;
    bool hypervisor=false;
    while(std::getline(cpuinfo,line)) {
        if(line.find("hypervisor")!=std::string::npos) hypervisor=true;
        if(topology.processor.empty() && line.rfind("model name",0)==0) {
            const auto colon=line.find(':');
            if(colon!=std::string::npos) topology.processor=line.substr(colon+1);
        }
    }
    std::ifstream hypervisorFile("/sys/hypervisor/type");
    std::string hypervisorName;
    if(hypervisorFile>>hypervisorName) hypervisor=true;
    if(release.find("microsoft")!=std::string::npos || release.find("wsl")!=std::string::npos)
        topology.environment="wsl";
    else if(hypervisor) topology.environment="virtualized";
    else if(std::ifstream("/.dockerenv").good() || std::ifstream("/run/.containerenv").good())
        topology.environment="container";
    return topology;
#endif
}
std::vector<CpuCore> select_cmp_cpus(const CpuTopology& topology,std::size_t workers,CmpProfile profile) {
    (void)cmp_profile_name(profile);
    if(profile==CmpProfile::Target) {
        if(topology.environment!="native-or-unknown")
            throw std::invalid_argument("El perfil target requiere Linux nativo; use fractal_cmp_local en este entorno.");
        if(topology.distribution!="ubuntu" || topology.processor.find("AMD Ryzen")==std::string::npos)
            throw std::invalid_argument("El perfil target requiere Ubuntu y CPU AMD Ryzen, segun el destino de evaluacion.");
        // Contrato extraído de los scripts RayTracing: 8 cores, SMT ON=16 lógicos/OFF=8.
        const auto cores=unique_physical_cores(topology.online_cpus);
        if(cores.size()!=8 || (topology.online_cpus.size()!=8 && topology.online_cpus.size()!=16))
            throw std::invalid_argument("El perfil target espera 8 nucleos y 8/16 CPUs online segun SMT; verificar hardware RayTracing.");
    }
    if(workers==0 || workers>topology.available_cores.size())
        throw std::invalid_argument("CMP solicita "+std::to_string(workers)+" trabajadores, pero solo hay "+
                                    std::to_string(topology.available_cores.size())+" nucleos distintos permitidos.");
    return {topology.available_cores.begin(),topology.available_cores.begin()+workers};
}
}
