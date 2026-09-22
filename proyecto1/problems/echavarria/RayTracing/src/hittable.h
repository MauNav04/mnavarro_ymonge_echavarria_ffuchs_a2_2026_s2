#ifndef HIITABLE_H
#define HIITABLE_H

#include "rtweekend.h"

class material;


// Clase que representa el registro de un impacto de un rayo con un objeto.
class hit_record {
    public:
    point3 p;
    vec3 normal;
    shared_ptr<material> mat;
    double t;
    bool front_face;

    // Determina si el impacto fue en la cara frontal o trasera del objeto y ajusta la normal en consecuencia.
    void set_face_normal(const ray& r, const vec3& outward_normal) {
        front_face = dot(r.direction(), outward_normal) < 0;
        normal = front_face ? outward_normal : -outward_normal;
    }
};

// Clase abstracta que representa un objeto que puede ser impactado por un rayo.
class hittable {
    public:
    virtual ~hittable() = default;

    virtual bool hit(const ray& r, interval ray_t, hit_record& rec) const = 0;
};

#endif