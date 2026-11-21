#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include "color.h"
#include <vector>
#include <string>
#include <fstream>
#include <iostream>
#include <cmath>
#include <algorithm>

/**
 * ============================================================================
 * Estructura de Datos: Framebuffer 2D en RAM Contigua
 * ============================================================================
 * Desacopla por completo el cómputo de la simulación gráfica del subsistema
 * de Entrada/Salida (std::cout / disco).
 * 
 * Beneficios Arquitectónicos:
 * 1. Acceso Row-Major: index(x, y) = y * width + x.
 * 2. Cero contención (Lock-Free): Cada hilo escribe exclusivamente en los
 *    índices de píxeles que le corresponden.
 * 3. Libre de False Sharing: Al procesar franjas horizontales de filas completas,
 *    las escrituras de diferentes hilos se ubican en líneas de caché L1/L2 separadas.
 * ============================================================================
 */
struct Framebuffer {
    int width;
    int height;
    std::vector<color> pixels;

    Framebuffer(int w, int h)
        : width(w), height(h), pixels(static_cast<size_t>(w * h), color(0, 0, 0)) {}

    inline size_t index(int x, int y) const {
        return static_cast<size_t>(y * width + x);
    }

    inline void set_pixel(int x, int y, const color& c) {
        pixels[index(x, y)] = c;
    }

    inline color get_pixel(int x, int y) const {
        return pixels[index(x, y)];
    }

    // Exporta el contenido del búfer al formato estándar PPM (P3 ASCII)
    bool save_ppm(const std::string& filepath) const {
        std::ofstream out(filepath);
        if (!out.is_open()) return false;

        out << "P3\n" << width << ' ' << height << "\n255\n";
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                write_color(out, pixels[index(x, y)]);
            }
        }
        return true;
    }

    // Exporta directamente a un ostream (por ejemplo std::cout)
    void write_ppm_stream(std::ostream& out) const {
        out << "P3\n" << width << ' ' << height << "\n255\n";
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                write_color(out, pixels[index(x, y)]);
            }
        }
    }

    // Calcula la diferencia máxima celda a celda entre dos framebuffers (para verificación)
    double diff_max(const Framebuffer& other) const {
        if (width != other.width || height != other.height) return 1e9;
        double max_d = 0.0;
        for (size_t i = 0; i < pixels.size(); ++i) {
            double dx = std::abs(pixels[i].x() - other.pixels[i].x());
            double dy = std::abs(pixels[i].y() - other.pixels[i].y());
            double dz = std::abs(pixels[i].z() - other.pixels[i].z());
            double d = std::max({dx, dy, dz});
            if (d > max_d) max_d = d;
        }
        return max_d;
    }
};

#endif // FRAMEBUFFER_H
