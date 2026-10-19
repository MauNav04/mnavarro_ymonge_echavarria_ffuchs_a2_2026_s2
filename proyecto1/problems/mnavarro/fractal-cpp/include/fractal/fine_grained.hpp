#pragma once
#include "fractal/execution.hpp"

namespace fractal {
// Usar execute_encode como punto de entrada público. channel identifica el plano en la traza.
Encoded encode_fine_grained(const Image& image, Config config,
                            const ExecutionConfig& execution, std::size_t channel = 0);
}
