#pragma once
#include "fractal/execution.hpp"
namespace fractal {
Encoded encode_cmp(const Image& image, Config config,const ExecutionConfig& execution,std::size_t channel=0);
}
