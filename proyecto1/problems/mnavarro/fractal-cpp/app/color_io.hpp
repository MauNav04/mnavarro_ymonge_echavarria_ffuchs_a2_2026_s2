#pragma once
#include "fractal/color.hpp"
#include <string>
void saveMultiCode(const std::string& path,const fractal::MultiEncoded& code);
fractal::MultiEncoded loadMultiCode(const std::string& path);
