#ifndef RTWEEKEND_H
#define RTWEEKEND_H

#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <random>


// Se utilizan para simplificar el código y evitar la necesidad de prefijar con std::.
using std::make_shared;
using std::shared_ptr;

// Constantes de utilidad para el trazado de rayos.
const double infinity = std::numeric_limits<double>::infinity();
const double pi = 3.1415926535897932385;

// función de utilidad para convertir grados a radianes.

#include <thread>

inline double degrees_to_radians(double degrees) {
    return degrees * pi / 180.0;
}

inline double random_double() {
    static thread_local std::uniform_real_distribution<double> distribution(0.0, 1.0);
    static thread_local std::mt19937 generator(std::random_device{}());
    return distribution(generator);
}

inline double random_double(double min, double max) {
    // Returns a random real in [min,max).
    return min + (max-min)*random_double();
}

// Headers comunes
#include "color.h"
#include "interval.h"
#include "ray.h"
#include "vec3.h"


#endif