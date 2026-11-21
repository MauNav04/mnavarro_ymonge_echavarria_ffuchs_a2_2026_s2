#pragma once
#include "fractal/execution.hpp"
namespace fractal {
Encoded encode_smt(const Image&,Config,const ExecutionConfig&,std::size_t channel=0);
}
