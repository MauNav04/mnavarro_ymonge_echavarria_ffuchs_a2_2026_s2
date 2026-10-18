#include "grid.hpp"

#include <algorithm>
#include <cassert>
#include <stdexcept>

namespace heat2d {

/**
 * @brief Constructs a densely packed 2D grid initialized to a baseline scalar.
 *
 * Allocates the entire thermodynamic surface as a single, contiguous 1D block 
 * of memory (`std::vector<double>`). This structure intentionally avoids 
 * "array of pointers" (e.g., `double**` or `std::vector<std::vector<double>>`) 
 * to eliminate pointer-chasing, prevent memory fragmentation, and guarantee 
 * predictable hardware prefetching.
 *
 * @param rows The vertical dimension of the computational domain.
 * @param cols The horizontal dimension of the computational domain.
 * @param initialTemperature The baseline scalar applied to all cells.
 * 
 * @throws std::invalid_argument If the requested grid has zero area.
 */
Grid::Grid(
    std::size_t rows,
    std::size_t cols,
    double initialTemperature
)
    : rows_(rows),
      cols_(cols),
      data_(rows * cols, initialTemperature) {

    // Fast-fail to prevent 0-byte allocations or division-by-zero 
    // downstream during domain partitioning.
    if (rows == 0 || cols == 0) {
        throw std::invalid_argument(
            "Grid dimensions must be greater than zero."
        );
    }
}

/**
 * @brief Grants mutable access to a specific cell in the grid.
 * 
 * Invokes the internal 2D-to-1D index mapping. Designed to be inlined 
 * by the compiler in release builds for zero-overhead element access.
 */
double& Grid::operator()(
    std::size_t row,
    std::size_t col
) {
    return data_[index(row, col)];
}

/**
 * @brief Grants read-only access to a specific cell in the grid.
 */
const double& Grid::operator()(
    std::size_t row,
    std::size_t col
) const {
    return data_[index(row, col)];
}

/**
 * @brief Retrieves the vertical dimension (number of rows).
 */
std::size_t Grid::rows() const {
    return rows_;
}

/**
 * @brief Retrieves the horizontal dimension (number of columns).
 */
std::size_t Grid::cols() const {
    return cols_;
}

/**
 * @brief Computes the absolute physical capacity of the grid in elements.
 */
std::size_t Grid::size() const {
    return data_.size();
}

/**
 * @brief Uniformly resets the entire grid to a specific scalar value.
 * 
 * Delegates to `std::fill` which is typically optimized by the standard 
 * library to utilize vectorized memory-set instructions (e.g., `memset` 
 * or AVX stores) for contiguous containers.
 */
void Grid::fill(double temperature) {
    std::fill(data_.begin(), data_.end(), temperature);
}

/**
 * @brief Converts 2D logical coordinates into a 1D physical memory offset.
 *
 * Enforces a strict row-major layout (`row * cols_ + col`). This is arguably 
 * the most performance-critical design decision in the framework, as traversing 
 * columns within the same row guarantees sequential memory access, maximizing 
 * L1-D cache line utilization and hardware prefetcher efficiency.
 *
 * Bounds checking is deliberately performed using `assert` rather than throwing 
 * exceptions (e.g., `std::vector::at()`). This guarantees zero validation overhead 
 * in release builds (`NDEBUG`), which is mandatory since this function is invoked 
 * hundreds of millions of times per second inside the innermost stencil loops.
 */
std::size_t Grid::index(
    std::size_t row,
    std::size_t col
) const {
    assert(row < rows_);
    assert(col < cols_);

    return row * cols_ + col;
}

}  // namespace heat2d