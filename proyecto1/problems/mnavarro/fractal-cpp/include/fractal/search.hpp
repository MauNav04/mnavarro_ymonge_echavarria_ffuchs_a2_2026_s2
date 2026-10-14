#pragma once
#include "fractal/codec.hpp"
#include <cstddef>
#include <limits>

namespace fractal {
struct Domain { int x, y; std::vector<float> reduced; };
// Construir mediante prepare_encoding; no modificar durante la búsqueda de los equipos.
struct EncodingProblem {
    int width, height, range;
    std::vector<Domain> domains;
    std::size_t range_count() const;
};
struct RangeTarget { std::vector<float> pixels; double mean; };
struct Candidate {
    Rule rule;
    double error = std::numeric_limits<double>::infinity();
    // Índice dominio*8+simetría; desempate idéntico al recorrido secuencial.
    std::size_t order = std::numeric_limits<std::size_t>::max();
};
EncodingProblem prepare_encoding(const Image& image, Config config = {});
// La imagen debe ser la misma validada por prepare_encoding y permanecer sin cambios.
RangeTarget prepare_range(const Image& image, const EncodingProblem& problem, std::size_t index);
// Intervalo semiabierto. Un intervalo vacío devuelve el candidato neutro (error infinito).
Candidate search_domains(const EncodingProblem& problem, const RangeTarget& range,
                         std::size_t begin, std::size_t end);
Candidate best_candidate(const Candidate& a, const Candidate& b);
}
