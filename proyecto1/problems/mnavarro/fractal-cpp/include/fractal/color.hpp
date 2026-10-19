#pragma once
#include "fractal/codec.hpp"
namespace fractal {
// Un plano gris o tres planos en orden R, G, B. Cada plano conserva Image.
struct MultiImage { std::vector<Image> planes; };
struct MultiEncoded { std::vector<Encoded> planes; };
void validate(const MultiEncoded& code);
MultiEncoded encode(const MultiImage& image, Config config = {});
MultiImage decode(const MultiEncoded& code, int iterations = 40);
double mse(const MultiImage& a, const MultiImage& b);
}
