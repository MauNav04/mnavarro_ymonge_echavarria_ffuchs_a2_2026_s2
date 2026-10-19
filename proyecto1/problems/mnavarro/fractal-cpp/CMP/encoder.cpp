#include "fractal/cmp.hpp"
#include "../src/parallel_encoder.hpp"
#include <stdexcept>
namespace fractal {
Encoded encode_cmp(const Image& image,Config config,const ExecutionConfig& execution,std::size_t channel) {
    if(execution.model!=Model::CMP) throw std::invalid_argument("Se requiere CMP.");
    return encode_parallel(image,config,execution,channel);
}
}
