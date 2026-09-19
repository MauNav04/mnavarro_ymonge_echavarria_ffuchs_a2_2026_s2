#ifndef RAY_H
#define RAY_H

#include "vec3.h"

// Clase que representa un rayo en 3D, definido por un origen y una dirección.
class ray {
    public:
        ray() {}

        // Constructor que inicializa el rayo con un origen y una dirección.
        ray(const point3& origin, const vec3& direction)
            : orig(origin), dir(direction)
        {}

        // Devuelve el origen del rayo.
        const point3& origin() const { return orig; }
        const vec3& direction() const { return dir; }

        // Calcula un punto a lo largo del rayo en función del parámetro t.
        point3 at(double t) const {
            return orig + t*dir;
        }
    
    // 
    private:
        point3 orig;
        vec3 dir;
};

#endif