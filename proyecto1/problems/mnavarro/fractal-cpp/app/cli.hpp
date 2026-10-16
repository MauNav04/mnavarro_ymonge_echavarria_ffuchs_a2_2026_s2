#pragma once
#include "fractal/execution.hpp"

namespace fractal::app {
// Cada main fija su modelo; compartir argumentos, E/S y métricas evita duplicaciones.
int run(int argc, char** argv, Model model);
}
