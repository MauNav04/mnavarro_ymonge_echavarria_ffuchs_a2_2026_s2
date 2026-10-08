/**
 * ============================================================================
 * Referencia: Ray Tracing in One Weekend (v4.0)
 * Autores: Peter Shirley, Trevor David Black, Steve Hollasch
 * Enlace: https://raytracing.github.io/books/RayTracingInOneWeekend.html
 * Licencia original: CC0 1.0 Universal (Dominio Público)
 * ============================================================================
 */

#ifndef CAMERA_H
#define CAMERA_H

#include "hittable.h"
#include "material.h"
#include "framebuffer.h"
#include <chrono>

// Clase que representa una cámara en un espacio 3D.
class camera {
  public:
    double aspect_ratio = 1.0;  // Ratio of image width over height
    // --- VARIABLES CLAVE PARA PARALELIZACIÓN (Avance 2) ---
    int    image_width  = 100;  // [Variable 1]: Espacio total de tareas independientes (Resolución W x H)
    int    samples_per_pixel = 10;  // [Variable 2]: Granularidad de cómputo por píxel (Rayos/Anti-aliasing)
    int    max_depth         = 10;  // [Variable 3]: Desbalance de carga estocástico (Límite de rebotes)

    double vfov = 90.0; // Field of view in degrees
    point3 lookfrom = point3(0,0,0);   // Point camera is looking from
    point3 lookat   = point3(0,0,-1);  // Point camera is looking at
    vec3   vup      = vec3(0,1,0);     // Camera-relative "up" direction

    double defocus_angle = 0;  // Variation angle of rays through each pixel
    double focus_dist = 10;    // Distance from camera lookfrom point to plane of perfect focus

    // Renderiza la escena y escribe los colores de los píxeles en la salida estándar.
    void render(const hittable& world) {
        initialize();

        std::clog << "\n======================================================\n";
        std::clog << "  EJECUCIÓN SISTEMA BASE (SECUENCIAL - SIN HILOS)    \n";
        std::clog << "======================================================\n";
        std::clog << "Resolución:        " << image_width << " x " << image_height << " píxeles\n";
        std::clog << "Muestras / píxel:  " << samples_per_pixel << "\n";
        std::clog << "Máx. profundidad:  " << max_depth << "\n";
        std::clog << "------------------------------------------------------\n";

        auto start_time = std::chrono::high_resolution_clock::now();

        std::cout << "P3\n" << image_width << ' ' << image_height << "\n255\n";

        // =====================================================================
        // [PUNTO 1 DE PARALELIZACIÓN - Descomposición 1D / CMP / Coarse-Grained]
        // Bucle exterior por filas (scanlines): Cada hilo procesa un bloque de filas
        // =====================================================================
        for (int j = 0; j < image_height; j++) {
            std::clog << "\rProgreso: " << (100 * j / image_height) << "% (Filas restantes: " << (image_height - j) << ")   " << std::flush;

            // =================================================================
            // [PUNTO 2 DE PARALELIZACIÓN - Descomposición 2D por Mosaicos / Tiles]
            // Combinación de j e i para dividir la imagen en bloques 2D (ej. 16x16)
            // distribuidos dinámicamente con una cola de tareas (Work Queue).
            // =================================================================
            for (int i = 0; i < image_width; i++) {
                color pixel_color(0,0,0);

                // =============================================================
                // [PUNTO 3 DE PARALELIZACIÓN - Fine-Grained Multithreading]
                // Bucle de muestras por píxel: Cada rayo es independiente.
                // Permite simulación de quantums.
                // =============================================================
                for (int sample = 0; sample < samples_per_pixel; sample++) {
                    ray r = get_ray(i, j);
                    pixel_color += ray_color(r, max_depth, world);
                }

                // =============================================================
                // [CUELLO DE BOTELLA SECUENCIAL A RESOLVER]
                // Escritura directa a std::cout (condición de carrera en I/O con hilos).
                // En multihilo se reemplaza por un Framebuffer en RAM:
                // =============================================================
                write_color(std::cout, pixel_samples_scale * pixel_color);
            }
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = end_time - start_time;

        std::clog << "\rProgreso: 100% completado.                                  \n";
        std::clog << "------------------------------------------------------\n";
        std::clog << "Tiempo total de ejecución (secuencial): " << elapsed.count() << " segundos.\n";
        std::clog << "======================================================\n\n";
    }

    int get_image_height() const { return image_height; }

    // Inicializa la cámara y la geometría del viewport (público para concurrencia)
    void initialize() {
        image_height = int(image_width / aspect_ratio);
        image_height = (image_height < 1) ? 1 : image_height;

        pixel_samples_scale = 1.0 / samples_per_pixel;
        center = lookfrom;

        // Calcula la longitud focal y las dimensiones del viewport.
        auto theta = degrees_to_radians(vfov);
        auto h = std::tan(theta/2);
        auto viewport_height = 2 * h * focus_dist;
        auto viewport_width = viewport_height * (double(image_width)/image_height);

        // Calcula los vectores horizontales y verticales del viewport.
        w = unit_vector(lookfrom - lookat);
        u = unit_vector(cross(vup, w));
        v = cross(w, u);

        // Calcula los vectores de desplazamiento de píxel a píxel en las direcciones horizontal y vertical.
        vec3 viewport_u = viewport_width * u;
        vec3 viewport_v = viewport_height * -v;

        pixel_delta_u = viewport_u / image_width;
        pixel_delta_v = viewport_v / image_height;

        auto viewport_upper_left = center - (focus_dist * w) - viewport_u/2 - viewport_v/2;
        pixel00_loc = viewport_upper_left + 0.5 * (pixel_delta_u + pixel_delta_v);

        auto defocus_radius = focus_dist * std::tan(degrees_to_radians(defocus_angle / 2));
        defocus_disk_u = u * defocus_radius;
        defocus_disk_v = v * defocus_radius;
    }

    // =========================================================================
    // NÚCLEO COMPUTACIONAL DESACOPLADO (Lock-Free y Thread-Safe)
    // =========================================================================
    // Calcula el color de un único píxel promediando samples_per_pixel rayos
    color render_pixel(int i, int j, const hittable& world) const {
        color pixel_color(0, 0, 0);
        for (int sample = 0; sample < samples_per_pixel; ++sample) {
            ray r = get_ray(i, j);
            pixel_color += ray_color(r, max_depth, world);
        }
        return pixel_samples_scale * pixel_color;
    }

    // Cómputo de píxel con conteo de eventos de bloqueo/rebotes profundos (Coarse-Grained)
    color render_pixel_counted(int i, int j, const hittable& world, int& stalls, int stall_threshold = 15) const {
        color pixel_color(0, 0, 0);
        for (int sample = 0; sample < samples_per_pixel; ++sample) {
            ray r = get_ray(i, j);
            int bounces = 0;
            pixel_color += ray_color_counted(r, max_depth, world, bounces);
            if (bounces >= stall_threshold) {
                stalls++;
            }
        }
        return pixel_samples_scale * pixel_color;
    }

    // Renderiza una franja continua de filas [start_y, end_y) directamente al Framebuffer en RAM
    void render_slice(const hittable& world, Framebuffer& fb, int start_y, int end_y) const {
        for (int j = start_y; j < end_y; ++j) {
            for (int i = 0; i < image_width; ++i) {
                fb.set_pixel(i, j, render_pixel(i, j, world));
            }
        }
    }

    // Renderiza una franja con instrumentación de stalls para Coarse-Grained
    void render_slice_counted(const hittable& world, Framebuffer& fb, int start_y, int end_y, int& stalls) const {
        for (int j = start_y; j < end_y; ++j) {
            for (int i = 0; i < image_width; ++i) {
                fb.set_pixel(i, j, render_pixel_counted(i, j, world, stalls));
            }
        }
    }

  private:
    int    image_height;   // Rendered image height
    double pixel_samples_scale;
    point3 center;         // Camera center
    point3 pixel00_loc;    // Location of pixel 0, 0
    vec3   pixel_delta_u;  // Offset to pixel to the right
    vec3   pixel_delta_v;  // Offset to pixel below
    vec3   u, v, w; // Camera coordinate system
    vec3   defocus_disk_u;       // Defocus disk horizontal radius
    vec3   defocus_disk_v;       // Defocus disk vertical radius


    ray get_ray(int i, int j) const {
        // Construct a camera ray originating from the origin and directed at randomly sampled
        // point around the pixel location i, j.

        auto offset = sample_square();
        auto pixel_sample = pixel00_loc
                          + ((i + offset.x()) * pixel_delta_u)
                          + ((j + offset.y()) * pixel_delta_v);

        auto ray_origin = (defocus_angle <= 0) ? center : defocus_disk_sample();
        auto ray_direction = pixel_sample - ray_origin;

        return ray(ray_origin, ray_direction);
    }

    vec3 sample_square() const {
        // Returns the vector to a random point in the [-.5,-.5]-[+.5,+.5] unit square.
        return vec3(random_double() - 0.5, random_double() - 0.5, 0);
    }

    point3 defocus_disk_sample() const {
        // Returns a random point in the camera defocus disk.
        auto p = random_in_unit_disk();
        return center + (p[0] * defocus_disk_u) + (p[1] * defocus_disk_v);
    }



    // Calcula el color de un píxel en función del rayo que lo atraviesa y del mundo.
    color ray_color(const ray& r, int depth, const hittable& world) const {
        // Si superamos el límite de rebotes del rayo, no se recolecta más luz.
        if (depth <= 0)
            return color(0,0,0);

        hit_record rec;

        // Se usa 0.001 en lugar de 0 para solucionar el shadow acne
        if (world.hit(r, interval(0.001, infinity), rec)) {
            ray scattered;
            color attenuation;
            if (rec.mat->scatter(r, rec, attenuation, scattered))
                return attenuation * ray_color(scattered, depth-1, world);
            return color(0,0,0);
        }

        vec3 unit_direction = unit_vector(r.direction());
        auto a = 0.5*(unit_direction.y() + 1.0);
        return (1.0-a)*color(1.0, 1.0, 1.0) + a*color(0.5, 0.7, 1.0);
    }

    // Variante que contabiliza rebotes profundos para cuantificar stalls en Coarse-Grained
    color ray_color_counted(const ray& r, int depth, const hittable& world, int& bounces) const {
        if (depth <= 0)
            return color(0,0,0);

        hit_record rec;
        if (world.hit(r, interval(0.001, infinity), rec)) {
            bounces++;
            ray scattered;
            color attenuation;
            if (rec.mat->scatter(r, rec, attenuation, scattered))
                return attenuation * ray_color_counted(scattered, depth-1, world, bounces);
            return color(0,0,0);
        }

        vec3 unit_direction = unit_vector(r.direction());
        auto a = 0.5*(unit_direction.y() + 1.0);
        return (1.0-a)*color(1.0, 1.0, 1.0) + a*color(0.5, 0.7, 1.0);
    }
};

#endif