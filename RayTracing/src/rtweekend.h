#ifndef RTWEEKEND_H
#define RTWEEKEND_H

#include <cmath>
#include <iostream>
#include <limits>
#include <memory>


// Se utilizan para simplificar el código y evitar la necesidad de prefijar con std::.
using std::make_shared;
using std::shared_ptr;

// Constantes de utilidad para el trazado de rayos.
const double infinity = std::numeric_limits<double>::infinity();
const double pi = 3.1415926535897932385;

// función de utilidad para convertir grados a radianes.

inline double degrees_to_radians(double degrees) {
    return degrees * pi / 180.0;
}

// Headers comunes
#include "color.h"
#include "interval.h"
#include "ray.h"
#include "vec3.h"


#endif