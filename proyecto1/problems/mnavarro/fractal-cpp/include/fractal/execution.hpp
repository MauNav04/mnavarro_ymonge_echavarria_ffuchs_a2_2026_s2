#pragma once
#include "fractal/color.hpp"
#include <cstddef>
#include <string>

namespace fractal {
enum class Model { Sequential, FineGrained, CoarseGrained, CMP, SMT };
struct ExecutionConfig {
    Model model = Model::Sequential;
    std::size_t teams = 1;
    std::size_t threads_per_team = 1;
};
Model parse_model(const std::string& name);
const char* model_name(Model model);
// En etapa 1 solo sequential con un equipo de un hilo está disponible.
void validate_execution(const ExecutionConfig& execution);
Encoded execute_encode(const Image& image, Config config = {}, ExecutionConfig execution = {});
MultiEncoded execute_encode(const MultiImage& image, Config config = {}, ExecutionConfig execution = {});
Image execute_decode(const Encoded& code, int iterations = 40, ExecutionConfig execution = {});
MultiImage execute_decode(const MultiEncoded& code, int iterations = 40, ExecutionConfig execution = {});
}
