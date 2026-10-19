#pragma once  // Prevents this header file from being included more than once during compilation.

#include <cstddef>  // Provides std::size_t, used for sizes and indices.
#include <vector>   // Provides std::vector, which will store temperatures contiguously.

namespace heat2d {  // Groups all Heat Diffusion 2D problem structures within their own namespace.

/**
 * @brief Represents a rectangular two-dimensional surface of temperatures.
 *
 * The Grid class allows working conceptually with a matrix T[row][column],
 * but internally stores all values within a single std::vector<double>.
 *
 * Contiguous storage facilitates sequential memory access and will be useful
 * later for studying cache locality and parallelization.
 */
class Grid {

public:

    /**
     * @brief Constructs a new two-dimensional grid.
     *
     * @param rows Number of rows of the surface.
     * @param cols Number of columns of the surface.
     * @param initialTemperature Initial temperature assigned to all cells.
     */
    Grid(
        std::size_t rows,
        std::size_t cols,
        double initialTemperature = 0.0
    );

    /**
     * @brief Allows accessing and modifying a cell of the grid.
     *
     * @param row Row of the cell.
     * @param col Column of the cell.
     * @return Reference to the temperature value stored in the cell.
     */
    double& operator()(
        std::size_t row,
        std::size_t col
    );

    /**
     * @brief Allows querying a cell of a constant grid.
     *
     * This version avoids accidentally modifying the temperature when
     * the Grid object was received as constant.
     *
     * @param row Row of the cell.
     * @param col Column of the cell.
     * @return Constant reference to the stored temperature value.
     */
    const double& operator()(
        std::size_t row,
        std::size_t col
    ) const;

    /**
     * @brief Gets the number of rows of the grid.
     *
     * @return Number of rows.
     */
    std::size_t rows() const;

    /**
     * @brief Gets the number of columns of the grid.
     *
     * @return Number of columns.
     */
    std::size_t cols() const;

    /**
     * @brief Gets the total number of cells of the grid.
     *
     * @return Total number of stored elements.
     */
    std::size_t size() const;

    /**
     * @brief Assigns the same temperature to all cells.
     *
     * @param temperature Temperature that will be assigned to the entire surface.
     */
    void fill(double temperature);

private:

    /**
     * @brief Converts a two-dimensional position into a linear index.
     *
     * The conversion uses row-major storage:
     *
     * index = row * cols + col
     *
     * @param row Row of the cell.
     * @param col Column of the cell.
     * @return Index corresponding to the internal vector.
     */
    std::size_t index(
        std::size_t row,
        std::size_t col
    ) const;

    std::size_t rows_;       // Number of rows existing in the grid.

    std::size_t cols_;       // Number of columns existing in the grid.

    std::vector<double> data_;  // Contiguous vector that stores all temperatures.
};

}  // namespace heat2d