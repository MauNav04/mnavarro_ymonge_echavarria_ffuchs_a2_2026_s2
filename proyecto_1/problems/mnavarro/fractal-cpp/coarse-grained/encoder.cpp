#include "fractal/coarse_grained.hpp"
#include "../src/cooperative_encoder.hpp"
#include <stdexcept>
namespace fractal {
Encoded encode_coarse_grained(const Image& image, Config config,
                            const ExecutionConfig& execution, std::size_t channel) {
    if(execution.model!=Model::CoarseGrained) throw std::invalid_argument("Se requiere coarse-grained.");
    return encode_cooperative(image,config,execution,channel);
}
}
