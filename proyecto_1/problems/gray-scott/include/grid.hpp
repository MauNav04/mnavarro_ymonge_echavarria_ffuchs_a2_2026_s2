#ifndef GRID_HPP
#define GRID_HPP

#include <vector>
#include <cstddef>

struct Grid2D {
    int width;
    int height;
    std::vector<float> u;
    std::vector<float> v;

    Grid2D(int w, int h) 
        : width(w), height(h), u(w * h, 1.0f), v(w * h, 0.0f) {}

    inline size_t index(int x, int y) const {
        return static_cast<size_t>(y * width + x);
    }
};

#endif // GRID_HPP