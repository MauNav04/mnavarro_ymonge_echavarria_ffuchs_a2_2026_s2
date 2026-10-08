#ifndef SCENE_H
#define SCENE_H

#include "rtweekend.h"
#include "hittable.h"
#include "hittable_list.h"
#include "sphere.h"
#include "material.h"
#include <random>

/**
 * ============================================================================
 * Generador de Escena Determinista (Ray Tracing in One Weekend - Final Scene)
 * ============================================================================
 * Utiliza una semilla pseudoaleatoria fija (seed = 1337) para garantizar que
 * todos los modelos de ejecución concurrente (Secuencial, CMP, SMT, Coarse, Fine)
 * evalúen EXACTAMENTE la misma escena con las mismas 480+ esferas, posiciones
 * y coeficientes ópticos.
 * ============================================================================
 */
inline hittable_list create_scene() {
    hittable_list world;

    auto ground_material = make_shared<lambertian>(color(0.5, 0.5, 0.5));
    world.add(make_shared<sphere>(point3(0, -1000, 0), 1000, ground_material));

    // Generador con semilla determinista para reproducibilidad experimental
    std::mt19937 scene_rng(1337);
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    auto rng = [&]() { return dist(scene_rng); };

    for (int a = -11; a < 11; a++) {
        for (int b = -11; b < 11; b++) {
            auto choose_mat = rng();
            point3 center(a + 0.9 * rng(), 0.2, b + 0.9 * rng());

            if ((center - point3(4, 0.2, 0)).length() > 0.9) {
                shared_ptr<material> sphere_material;

                if (choose_mat < 0.8) {
                    // Difuso / Lambertiano
                    auto albedo = color(rng() * rng(), rng() * rng(), rng() * rng());
                    sphere_material = make_shared<lambertian>(albedo);
                    world.add(make_shared<sphere>(center, 0.2, sphere_material));
                } else if (choose_mat < 0.95) {
                    // Metálico
                    auto albedo = color(0.5 + 0.5 * rng(), 0.5 + 0.5 * rng(), 0.5 + 0.5 * rng());
                    auto fuzz = 0.5 * rng();
                    sphere_material = make_shared<metal>(albedo, fuzz);
                    world.add(make_shared<sphere>(center, 0.2, sphere_material));
                } else {
                    // Vidrio / Dieléctrico
                    sphere_material = make_shared<dielectric>(1.5);
                    world.add(make_shared<sphere>(center, 0.2, sphere_material));
                }
            }
        }
    }

    auto material1 = make_shared<dielectric>(1.5);
    world.add(make_shared<sphere>(point3(0, 1, 0), 1.0, material1));

    auto material2 = make_shared<lambertian>(color(0.4, 0.2, 0.1));
    world.add(make_shared<sphere>(point3(-4, 1, 0), 1.0, material2));

    auto material3 = make_shared<metal>(color(0.7, 0.6, 0.5), 0.0);
    world.add(make_shared<sphere>(point3(4, 1, 0), 1.0, material3));

    return world;
}

#endif // SCENE_H
