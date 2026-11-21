#include "fractal/fine_grained.hpp"
#include "../src/cooperative_encoder.hpp"
#include <stdexcept>
namespace fractal {
Encoded encode_fine_grained(const Image& image, Config config,
                            const ExecutionConfig& execution, std::size_t channel) {
    if(execution.model!=Model::FineGrained) throw std::invalid_argument("Se requiere fine-grained.");
    return encode_cooperative(image,config,execution,channel);
}
}
