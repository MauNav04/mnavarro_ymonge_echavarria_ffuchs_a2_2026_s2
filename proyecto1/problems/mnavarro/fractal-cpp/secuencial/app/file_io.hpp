#pragma once
#include "fractal/codec.hpp"
#include <string>
void saveCode(const std::string& path, const fractal::Encoded& code);
fractal::Encoded loadCode(const std::string& path);
