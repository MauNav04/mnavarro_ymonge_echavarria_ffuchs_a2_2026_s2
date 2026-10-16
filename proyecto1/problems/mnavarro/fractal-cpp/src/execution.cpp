#include "fractal/execution.hpp"
#include "fractal/fine_grained.hpp"
#include "fractal/coarse_grained.hpp"
#include "fractal/cmp.hpp"
#include "fractal/smt.hpp"
#include <limits>
#include <stdexcept>

namespace fractal {
Model parse_model(const std::string& name) {
    if (name=="sequential" || name=="secuencial") return Model::Sequential;
    if (name=="fine-grained") return Model::FineGrained;
    if (name=="coarse-grained") return Model::CoarseGrained;
    if (name=="CMP") return Model::CMP;
    if (name=="SMT") return Model::SMT;
    throw std::invalid_argument("Modelo desconocido: "+name);
}
const char* model_name(Model model) {
    switch(model) {
        case Model::Sequential: return "sequential";
        case Model::FineGrained: return "fine-grained";
        case Model::CoarseGrained: return "coarse-grained";
        case Model::CMP: return "CMP";
        case Model::SMT: return "SMT";
    }
    throw std::invalid_argument("Modelo invalido.");
}
const char* schedule_reason_name(ScheduleReason reason) {
    switch(reason) {
        case ScheduleReason::Quantum: return "quantum";
        case ScheduleReason::Stall: return "stall";
        case ScheduleReason::DomainComplete: return "domain_complete";
    }
    throw std::invalid_argument("Causa de turno invalida.");
}
void validate_execution(const ExecutionConfig& execution) {
    if (execution.teams==0 || execution.threads_per_team==0 ||
        execution.teams>std::numeric_limits<std::size_t>::max()/execution.threads_per_team)
        throw std::invalid_argument("Equipos e hilos por equipo deben ser positivos y su producto representable.");
    if (execution.model!=Model::Sequential && execution.model!=Model::FineGrained && execution.model!=Model::CoarseGrained && execution.model!=Model::CMP && execution.model!=Model::SMT)
        throw std::invalid_argument(std::string("Modelo aun no implementado: ")+model_name(execution.model));
    if (execution.model==Model::Sequential && (execution.teams!=1 || execution.threads_per_team!=1))
        throw std::invalid_argument("El modelo secuencial requiere 1 equipo de 1 hilo.");
    if(execution.model!=Model::CMP && execution.model!=Model::SMT && execution.worker_placements)
        throw std::invalid_argument("Reporte de afinidad requiere CMP o SMT.");
    if(execution.model!=Model::SMT && execution.smt_state!=SmtState::On)
        throw std::invalid_argument("Estado SMT requiere modelo SMT.");
    if(execution.model==Model::SMT) {
        if(execution.quantum_domains!=1 || execution.schedule_trace)
            throw std::invalid_argument("SMT no utiliza cuantums ni turnos cooperativos.");
        (void)select_smt_cpus(detect_cpu_topology(),execution.teams*execution.threads_per_team,execution.smt_state);
    }
    if(execution.model==Model::CMP) {
        if(execution.quantum_domains!=1 || execution.schedule_trace)
            throw std::invalid_argument("CMP no utiliza cuantums ni turnos cooperativos.");
        (void)select_cmp_cpus(detect_cpu_topology(),execution.teams*execution.threads_per_team);
    }
    if(execution.model==Model::CoarseGrained && execution.quantum_domains!=1)
        throw std::invalid_argument("Cuantum requiere fine-grained; coarse-grained cambia por eventos.");
    if(execution.quantum_domains==0) throw std::invalid_argument("Cuantum debe ser positivo.");
    if(execution.model==Model::Sequential && (execution.quantum_domains!=1 || execution.schedule_trace))
        throw std::invalid_argument("Cuantum y traza requieren fine-grained.");
}
Encoded execute_encode(const Image& image, Config config, ExecutionConfig execution) {
    validate_execution(execution);
    if(execution.model==Model::FineGrained) return encode_fine_grained(image,config,execution);
    if(execution.model==Model::CoarseGrained) return encode_coarse_grained(image,config,execution);
    if(execution.model==Model::CMP) return encode_cmp(image,config,execution);
    if(execution.model==Model::SMT) return encode_smt(image,config,execution);
    return encode(image,config);
}
MultiEncoded execute_encode(const MultiImage& image, Config config, ExecutionConfig execution) {
    validate_execution(execution);
    if(execution.model==Model::Sequential) return encode(image,config);
    if(image.planes.size()!=1 && image.planes.size()!=3)
        throw std::invalid_argument("Se requieren 1 canal gris o 3 canales RGB.");
    for(const auto& plane:image.planes) {
        if(plane.width!=image.planes[0].width || plane.height!=image.planes[0].height)
            throw std::invalid_argument("Los canales deben tener las mismas dimensiones.");
        (void)mse(plane,plane);
    }
    MultiEncoded result;
    for(std::size_t channel=0;channel<image.planes.size();++channel) {
        if(execution.model==Model::FineGrained)
            result.planes.push_back(encode_fine_grained(image.planes[channel],config,execution,channel));
        else if(execution.model==Model::CoarseGrained)
            result.planes.push_back(encode_coarse_grained(image.planes[channel],config,execution,channel));
        else if(execution.model==Model::CMP)
            result.planes.push_back(encode_cmp(image.planes[channel],config,execution,channel));
        else result.planes.push_back(encode_smt(image.planes[channel],config,execution,channel));
    }
    return result;
}
Image execute_decode(const Encoded& code, int iterations, ExecutionConfig execution) {
    validate_execution(execution);
    if(execution.model!=Model::Sequential)
        throw std::invalid_argument(std::string("Decodificacion ")+model_name(execution.model)+" no implementada; use sequential.");
    return decode(code,iterations);
}
MultiImage execute_decode(const MultiEncoded& code, int iterations, ExecutionConfig execution) {
    validate_execution(execution);
    if(execution.model!=Model::Sequential)
        throw std::invalid_argument(std::string("Decodificacion ")+model_name(execution.model)+" no implementada; use sequential.");
    return decode(code,iterations);
}
}
