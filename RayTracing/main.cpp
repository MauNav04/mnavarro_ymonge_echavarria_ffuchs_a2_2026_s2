/**
 * ============================================================================
 * Referencia: Ray Tracing in One Weekend (v4.0)
 * Autores: Peter Shirley, Trevor David Black, Steve Hollasch
 * Enlace: https://raytracing.github.io/books/RayTracingInOneWeekend.html
 * Licencia original: CC0 1.0 Universal (Dominio Público)
 * ============================================================================
 */

#include "src/rtweekend.h"
#include "src/hittable.h"
#include "src/hittable_list.h"
#include "src/sphere.h"
#include "src/camera.h"
#include "src/material.h"



int main() {
    hittable_list world;

    auto ground_material = make_shared<lambertian>(color(0.5, 0.5, 0.5));
    world.add(make_shared<sphere>(point3(0,-1000,0), 1000, ground_material));

    for (int a = -11; a < 11; a++) {
        for (int b = -11; b < 11; b++) {
            auto choose_mat = random_double();
            point3 center(a + 0.9*random_double(), 0.2, b + 0.9*random_double());

            if ((center - point3(4, 0.2, 0)).length() > 0.9) {
                shared_ptr<material> sphere_material;

                if (choose_mat < 0.8) {
                    // diffuse
                    auto albedo = color::random() * color::random();
                    sphere_material = make_shared<lambertian>(albedo);
                    world.add(make_shared<sphere>(center, 0.2, sphere_material));
                } else if (choose_mat < 0.95) {
                    // metal
                    auto albedo = color::random(0.5, 1);
                    auto fuzz = random_double(0, 0.5);
                    sphere_material = make_shared<metal>(albedo, fuzz);
                    world.add(make_shared<sphere>(center, 0.2, sphere_material));
                } else {
                    // glass
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

    camera cam;

    cam.aspect_ratio      = 16.0 / 9.0;
    // [Variables clave del problema que escalan el cómputo e influyen en el speedup]:
    cam.image_width       = 1200; // Resolución total W x H (cantidad de tareas independientes)
    cam.samples_per_pixel = 10;   // Granularidad de cómputo por píxel
    cam.max_depth         = 50;   // Cadena de rebotes (variabilidad de carga entre hilos)

    cam.vfov     = 20;
    cam.lookfrom = point3(13,2,3);
    cam.lookat   = point3(0,0,0);
    cam.vup      = vec3(0,1,0);

    cam.defocus_angle = 0.6;
    cam.focus_dist    = 10.0;

    // Ejecución secuencial base (aquí se disparará el despachador de hilos multithreading):
    cam.render(world);
}