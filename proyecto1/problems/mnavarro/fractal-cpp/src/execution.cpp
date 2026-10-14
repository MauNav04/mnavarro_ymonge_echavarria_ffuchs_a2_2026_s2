#include "fractal/execution.hpp"
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
void validate_execution(const ExecutionConfig& execution) {
    if (execution.teams==0 || execution.threads_per_team==0 ||
        execution.teams>std::numeric_limits<std::size_t>::max()/execution.threads_per_team)
        throw std::invalid_argument("Equipos e hilos por equipo deben ser positivos y su producto representable.");
    if (execution.model!=Model::Sequential)
        throw std::invalid_argument(std::string("Modelo aun no implementado: ")+model_name(execution.model));
    if (execution.teams!=1 || execution.threads_per_team!=1)
        throw std::invalid_argument("El modelo secuencial requiere 1 equipo de 1 hilo.");
}
Encoded execute_encode(const Image& image, Config config, ExecutionConfig execution) {
    validate_execution(execution);
    return encode(image,config);
}
MultiEncoded execute_encode(const MultiImage& image, Config config, ExecutionConfig execution) {
    validate_execution(execution);
    return encode(image,config);
}
Image execute_decode(const Encoded& code, int iterations, ExecutionConfig execution) {
    validate_execution(execution);
    return decode(code,iterations);
}
MultiImage execute_decode(const MultiEncoded& code, int iterations, ExecutionConfig execution) {
    validate_execution(execution);
    return decode(code,iterations);
}
}
