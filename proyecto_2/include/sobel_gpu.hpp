#ifndef SOBEL_DLP_SOBEL_GPU_HPP
#define SOBEL_DLP_SOBEL_GPU_HPP

#include "image_rgba.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sobel {

/**
 * @brief Metadatos de un dispositivo GPU OpenCL detectado en el sistema.
 */
struct GpuDeviceInfo {
    int index = -1;
    std::string platform_name;
    std::string device_name;
    std::string device_vendor;
    std::string opencl_version;
    uint32_t compute_units = 0;
    std::size_t max_work_group_size = 0;
    uint64_t local_mem_size_bytes = 0;
};

/**
 * @brief Tiempos medidos para una ejecución en GPU (en milisegundos).
 *
 * - h2d_ms, kernel_ms y d2h_ms provienen de eventos de hardware OpenCL (clGetEventProfilingInfo).
 * - total_ms proviene del reloj del host (std::chrono::steady_clock) cubriendo Write + Kernel + Read + clFinish.
 */
struct GpuTimingResult {
    double h2d_ms = 0.0;
    double kernel_ms = 0.0;
    double d2h_ms = 0.0;
    double total_ms = 0.0;
};

/**
 * @brief Contexto persistente RAII para ejecución de Sobel RGBA32 en GPU vía OpenCL.
 *
 * Centraliza la carga de OpenCL, selección de dispositivo, creación del contexto,
 * cola de comandos con CL_QUEUE_PROFILING_ENABLE, compilación JIT única de kernels
 * y reutilización de buffers de dispositivo entre múltiples ejecuciones.
 */
class SobelGpuContext {
public:
    /**
     * @brief Inicializa el contexto OpenCL seleccionando un dispositivo GPU.
     *
     * @param device_index Índice global de GPU (según list_gpu_devices()). Si es < 0,
     *                     selecciona automáticamente priorizando GPUs Intel (Iris Xe)
     *                     y haciendo fallback a la primera GPU OpenCL disponible.
     * @throws std::runtime_error si no se puede cargar OpenCL, no existe la GPU solicitada
     *                            o falla la compilación del kernel.
     */
    explicit SobelGpuContext(int device_index = -1);
    ~SobelGpuContext();

    SobelGpuContext(const SobelGpuContext&) = delete;
    SobelGpuContext& operator=(const SobelGpuContext&) = delete;
    SobelGpuContext(SobelGpuContext&&) noexcept;
    SobelGpuContext& operator=(SobelGpuContext&&) noexcept;

    /**
     * @brief Enumera todas las GPUs OpenCL disponibles a través de todas las plataformas.
     */
    static std::vector<GpuDeviceInfo> list_gpu_devices();

    /**
     * @brief Retorna la información del dispositivo GPU activo en este contexto.
     */
    const GpuDeviceInfo& device_info() const;

    /**
     * @brief Asegura que los buffers de entrada/salida en GPU estén asignados para (width, height).
     *
     * Si las dimensiones coinciden con las ya asignadas, reutiliza los buffers existentes sin realocar.
     */
    void ensure_buffers(int width, int height);

    /**
     * @brief Transfiere la imagen del host al buffer de entrada en GPU (Host -> Device).
     * @return Tiempo de transferencia medido por evento OpenCL (ms).
     */
    double upload(const ImageRGBA& input);

    /**
     * @brief Ejecuta el kernel Sobel sobre los buffers ya asignados para (width, height).
     * @return Tiempo de ejecución del kernel medido por evento OpenCL (ms).
     */
    double execute_kernel(int width, int height);

    /**
     * @brief Lee el resultado desde el buffer de salida en GPU al host (Device -> Host).
     * @return Tiempo de transferencia medido por evento OpenCL (ms).
     */
    double download(ImageRGBA& output);

    /**
     * @brief Ejecuta el flujo completo (Write -> Kernel -> Read -> clFinish) reutilizando buffers.
     *
     * @param input  Imagen de entrada válida en formato RGBA32.
     * @param output Buffer de salida prealocado con las mismas dimensiones que @p input.
     * @param timing Opcional: si no es nullptr, se rellenan h2d_ms, kernel_ms, d2h_ms y total_ms.
     */
    void run(const ImageRGBA& input, ImageRGBA& output, GpuTimingResult* timing = nullptr);

    /**
     * @brief Libera explícitamente todos los recursos OpenCL asociados al contexto.
     */
    void cleanup() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * @brief Aplica el operador Sobel 3x3 multicanal (R, G, B) ejecutando en GPU vía OpenCL.
 *
 * Función de conveniencia compatible con llamadas simples (crea un contexto temporal).
 * Para múltiples ejecuciones o benchmarking, reutilizar una instancia de SobelGpuContext.
 *
 * @param input  Imagen de entrada válida en formato RGBA32.
 * @param output Buffer de salida prealocado con las mismas dimensiones que @p input.
 * @throws std::invalid_argument si @p input o @p output no tienen dimensiones válidas/coincidentes.
 * @throws std::runtime_error si falla la inicialización o ejecución en el dispositivo OpenCL.
 */
void sobel_gpu_into(const ImageRGBA& input, ImageRGBA& output);

/**
 * @brief Sobrecarga que reutiliza un SobelGpuContext existente sin recompilar kernels.
 */
void sobel_gpu_into(SobelGpuContext& ctx, const ImageRGBA& input, ImageRGBA& output,
                    GpuTimingResult* timing = nullptr);

/**
 * @brief Variante de conveniencia que reserva y retorna la imagen resultante.
 */
ImageRGBA sobel_gpu(const ImageRGBA& input);

}  // namespace sobel

#endif  // SOBEL_DLP_SOBEL_GPU_HPP
