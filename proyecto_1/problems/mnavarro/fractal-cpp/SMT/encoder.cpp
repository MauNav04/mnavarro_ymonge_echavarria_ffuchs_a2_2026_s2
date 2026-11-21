#include "fractal/smt.hpp"
#include "../src/parallel_encoder.hpp"
#include <stdexcept>
namespace fractal {
Encoded encode_smt(const Image& image,Config config,const ExecutionConfig& execution,std::size_t channel) {
    if(execution.model!=Model::SMT) throw std::invalid_argument("Se requiere SMT.");
    return encode_parallel(image,config,execution,channel);
}
}
