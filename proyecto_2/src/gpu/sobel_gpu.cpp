#include "sobel_gpu.hpp"

#include "opencl_loader.hpp"

#include <stdexcept>
#include <string>

namespace sobel {

void sobel_gpu_into(const ImageRGBA& input, ImageRGBA& output) {
    if (!input.is_valid()) {
        throw std::invalid_argument("Input image is not valid");
    }
    if (output.width != input.width || output.height != input.height || !output.is_valid()) {
        throw std::invalid_argument("Output image dimensions must match input image");
    }

    // Paso 2: verificación de disponibilidad del runtime OpenCL antes de implementar el kernel en Paso 3.
    gpu::OpenCLApi cl;
    std::string err;
    if (!cl.load(err)) {
        throw std::runtime_error(err);
    }
    cl.unload();

    throw std::runtime_error("sobel_gpu_into: kernel OpenCL pendiente (Paso 3)");
}

ImageRGBA sobel_gpu(const ImageRGBA& input) {
    if (!input.is_valid()) {
        throw std::invalid_argument("Input image is not valid");
    }
    ImageRGBA output(input.width, input.height);
    sobel_gpu_into(input, output);
    return output;
}

}  // namespace sobel
