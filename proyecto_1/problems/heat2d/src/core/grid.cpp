#include "grid.hpp"

#include <algorithm>
#include <cassert>
#include <stdexcept>

namespace heat2d {

/**
 * @brief Constructs a rectangular grid initialized to a uniform temperature.
 *
 * Grid values are stored contiguously in row-major order using a single
 * `std::vector<double>`. Contiguous storage favors spatial locality when
 * traversing consecutive columns.
 *
 * @param rows Number of rows in the grid.
 * @param cols Number of columns in the grid.
 * @param initialTemperature Initial temperature assigned to every cell.
 *
 * @throws std::invalid_argument If either grid dimension is zero.
 */
Grid::Grid(
    std::size_t rows,
    std::size_t cols,
    double initialTemperature
)
    : rows_(rows),
      cols_(cols),
      data_(rows * cols, initialTemperature) {

    if (rows == 0 || cols == 0) {
        throw std::invalid_argument(
            "Grid dimensions must be greater than zero."
        );
    }
}

/**
 * @brief Provides mutable access to a grid cell.
 *
 * @param row Row index.
 * @param col Column index.
 * @return Reference to the requested cell.
 */
double& Grid::operator()(
    std::size_t row,
    std::size_t col
) {
    return data_[index(row, col)];
}

/**
 * @brief Provides read-only access to a grid cell.
 *
 * @param row Row index.
 * @param col Column index.
 * @return Constant reference to the requested cell.
 */
const double& Grid::operator()(
    std::size_t row,
    std::size_t col
) const {
    return data_[index(row, col)];
}

/**
 * @brief Returns the number of rows in the grid.
 */
std::size_t Grid::rows() const {
    return rows_;
}

/**
 * @brief Returns the number of columns in the grid.
 */
std::size_t Grid::cols() const {
    return cols_;
}

/**
 * @brief Returns the total number of cells stored in the grid.
 */
std::size_t Grid::size() const {
    return data_.size();
}

/**
 * @brief Assigns the same temperature to every grid cell.
 *
 * Uses `std::fill` over the contiguous storage, allowing the compiler and
 * standard library implementation to optimize the write operation.
 *
 * @param temperature Temperature assigned to every cell.
 */
void Grid::fill(double temperature) {
    std::fill(data_.begin(), data_.end(), temperature);
}

/**
 * @brief Converts a two-dimensional coordinate into a linear array index.
 *
 * The grid uses row-major storage:
 *
 *     index = row * cols_ + col
 *
 * Consecutive columns therefore occupy consecutive memory locations, which
 * favors spatial locality during row-wise traversal.
 *
 * Bounds are checked with `assert` in debug builds. When `NDEBUG` is defined,
 * these assertions are removed.
 *
 * @param row Row index.
 * @param col Column index.
 * @return Linear index corresponding to the requested grid position.
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