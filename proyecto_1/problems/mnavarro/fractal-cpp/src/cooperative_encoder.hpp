#pragma once
#include "fractal/execution.hpp"
namespace fractal {
// Mecanismo de pthreads, equipos y mínimos compartido entre políticas de ejecución.
Encoded encode_cooperative(const Image& image, Config config,
                           const ExecutionConfig& execution, std::size_t channel);
}
