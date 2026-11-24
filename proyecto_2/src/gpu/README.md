# Módulo GPU (`src/gpu/`)

Implementación paralela de Sobel 3x3 RGBA32 en GPU mediante **OpenCL 1.2** con carga dinámica (`OpenCL.dll` en Windows / `libOpenCL.so` en Linux), diseñada para el entorno objetivo **Intel Iris Xe** (con fallback automático a cualquier otra GPU OpenCL disponible).

## Estado actual (Fase 1 completada)

* **Ciclo de vida persistente (`SobelGpuContext`):**
  * Encapsula de forma RAII la carga de OpenCL, creación de `cl_context`, `cl_command_queue` con `CL_QUEUE_PROFILING_ENABLE` y compilación JIT única (`clBuildProgram`).
  * Libera todos los recursos en `cleanup()` / destructor, eliminando el *overhead* de reinicialización y recompilación entre ejecuciones.
* **Selección e inspección de dispositivo:**
  * Enumera todas las GPUs OpenCL entre plataformas (`SobelGpuContext::list_gpu_devices()`).
  * Selecciona automáticamente priorizando **Intel Iris Xe** (o permite elegir por índice con `--device <idx>`).
  * Reporta nombre del dispositivo, plataforma, vendor, versión OpenCL, *Compute Units*, tamaño máximo de *work-group* y memoria local (`CL_DEVICE_LOCAL_MEM_SIZE`).
* **Buffers reutilizables y profiling por eventos de hardware:**
  * `ensure_buffers(width, height)` reutiliza `buf_in`/`buf_out` mientras las dimensiones no cambien.
  * Separa y cronometra vía `clGetEventProfilingInfo` las etapas **Host -> Device (`upload`)**, **Kernel (`execute_kernel`)** y **Device -> Host (`download`)**, además del tiempo total de host (`std::chrono::steady_clock`).
* **Kernel activo:** `sobel_rgba_naive` (memoria global, work-groups `16x16`, validación exacta `max |O_seq - O_gpu| == 0`).
* **Pendiente en próximas fases:** coalescencia con `uchar4` y kernel *tiled* `18x18` en memoria local (Fase 2), batería `--validate` (Fase 3), harness `--benchmark` con CSV (Fase 4) y modo `--profile` para Intel VTune (Fase 5).

## Compilación y ejecución (desde `proyecto_2/`)

### Windows (MinGW / PowerShell)

```powershell
cd proyecto_2
mingw32-make test-gpu              # Compila y corre las 7 pruebas unitarias GPU
mingw32-make gpu                   # Compila y corre el ejecutable GPU (64x64 y 512x512, 5 runs)
.\bin\sobel_gpu.exe --list-devices # Lista las GPUs OpenCL detectadas y sus índices
.\bin\sobel_gpu.exe --device 0 --runs 10
mingw32-make gpu-save              # Guarda PNGs de entrada/salida en bin/demo_gpu_*
```

### Linux (`make`)

```bash
cd proyecto_2
make test-gpu
make gpu
./bin/sobel_gpu --list-devices
./bin/sobel_gpu --device 0 --runs 10
```
