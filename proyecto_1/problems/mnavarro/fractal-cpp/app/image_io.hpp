#pragma once
#include "fractal/color.hpp"
#include <string>
fractal::MultiImage readImage(const std::string& path,bool gray=false);
void writeImage(const std::string& path,const fractal::MultiImage& image);
