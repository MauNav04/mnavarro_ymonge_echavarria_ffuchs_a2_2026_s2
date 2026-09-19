#ifndef SPHERE_H
#define SPHERE_H

#include "hittable.h"
#include "rtweekend.h"

// Clase que representa una esfera en 3D, definida por un centro y un radio.
class sphere : public hittable {
  public:
    // Constructor de la esfera, que asegura que el radio sea no negativo.
    sphere(const point3& center, double radius) : center(center), radius(std::fmax(0,radius)) {}

    // Implementación del método hit para determinar si un rayo impacta con la esfera.
    bool hit(const ray& r, interval ray_t, hit_record& rec) const override {
        vec3 oc = center - r.origin();
        auto a = r.direction().length_squared();
        auto h = dot(r.direction(), oc);
        auto c = oc.length_squared() - radius*radius;

        auto discriminant = h*h - a*c;
        if (discriminant < 0)
            return false;

        auto sqrtd = std::sqrt(discriminant);

        // Calcula la raíz más cercana que se encuentra en el rango aceptable.
        auto root = (h - sqrtd) / a;
        if (!ray_t.surrounds(root)) {
            root = (h + sqrtd) / a;
            if (!ray_t.surrounds(root))
                return false;
        }
        
        // Si se encuentra un impacto válido, se actualiza el registro de impacto.
        rec.t = root;
        rec.p = r.at(rec.t);
        vec3 outward_normal = (rec.p - center) / radius;
        rec.set_face_normal(r, outward_normal);

        return true;
    }

  private:
    point3 center;
    double radius;
};

#endif