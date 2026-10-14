#pragma once
#include "fractal/color.hpp"
#include <cstddef>
#include <string>
#include <vector>

namespace fractal {
enum class Model { Sequential, FineGrained, CoarseGrained, CMP, SMT };
// Un evento por cuantum realmente ejecutado; no representa un contador de hardware.
struct ScheduleEvent {
    std::size_t channel, worker, team, range, domain_begin, domain_end;
};
struct ExecutionConfig {
    Model model = Model::Sequential;
    std::size_t teams = 1;
    std::size_t threads_per_team = 1;
    std::size_t quantum_domains = 1;
    // Diagnóstico opcional: eventos añadidos en orden de ejecución. El llamador conserva el vector.
    std::vector<ScheduleEvent>* schedule_trace = nullptr;
};
Model parse_model(const std::string& name);
const char* model_name(Model model);
// Modelos disponibles: sequential y fine-grained (codificación cooperativa).
void validate_execution(const ExecutionConfig& execution);
Encoded execute_encode(const Image& image, Config config = {}, ExecutionConfig execution = {});
MultiEncoded execute_encode(const MultiImage& image, Config config = {}, ExecutionConfig execution = {});
Image execute_decode(const Encoded& code, int iterations = 40, ExecutionConfig execution = {});
MultiImage execute_decode(const MultiEncoded& code, int iterations = 40, ExecutionConfig execution = {});
}
