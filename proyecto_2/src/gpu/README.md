# Módulo GPU (`src/gpu/`)

Implementación paralela de Sobel 3x3 RGBA32 en GPU mediante **OpenCL 1.2** con carga dinámica (`OpenCL.dll` en Windows / `libOpenCL.so` en Linux), diseñada para el entorno objetivo **Intel Iris Xe** (con fallback automático a cualquier otra GPU OpenCL disponible).

## Estado actual (Fase 3 completada)

* **Ciclo de vida persistente (`SobelGpuContext` — Fase 1):**
  * Encapsula de forma RAII la carga de OpenCL, selección de dispositivo (prioridad Intel Iris Xe o `--device <idx>`), `cl_context`, `cl_command_queue` con `CL_QUEUE_PROFILING_ENABLE` y compilación JIT única de ambos kernels.
  * Reutiliza buffers de dispositivo (`ensure_buffers(w, h)`) y cronometra por eventos de hardware `H2D`, `Kernel`, `D2H` y tiempo `Total` de host (`steady_clock`).
* **Dos variantes de kernel coalescentes con `uchar4` (`GpuKernelVariant` — Fase 2):**
  * **`GpuKernelVariant::Naive` (`sobel_rgba_naive`):** acceso directo a memoria global con `__global const uchar4*` / `__global uchar4*`, work-groups `16x16` y redondeo seguro de *Global Work Size*.
  * **`GpuKernelVariant::Tiled18x18` (`sobel_rgba_tiled`):** carga cooperativa de mosaicos `__local uchar4 tile[18][18]` (1296 bytes en SLM) sin `return` temprano, `barrier(CLK_LOCAL_MEM_FENCE)` y cómputo Sobel 3x3 post-barrera.
* **Batería de validación de correctitud (`--validate` — Fase 3):**
  * `run_validation_suite()` compara `sobel_sequential` contra `SIMD (AVX2)`, `gpu_naive` y `gpu_tiled` reportando `max_abs_diff` y `MAE` en 14 casos (42 verificaciones):
    1. Imagen constante (`32x32`, interior en `0` y bordes *zero-padding*).
    2. Borde vertical puro y borde horizontal puro (`32x32`).
    3. Impulso unitario central (`31x31`).
    4. Canales aislados R, G y B (`24x24`, sin mezcla entre canales).
    5. Las 4 esquinas `(0,0)`, `(W-1,0)`, `(0,H-1)`, `(W-1,H-1)` en `25x19`.
    6. Dimensiones no múltiplos de 16 ni del ancho SIMD: `18x18`, `100x77` y `513x513`.
  * Los ejecutables `sobel_seq` y `sobel_simd` siguen siendo completamente independientes (sin dependencia de GPU/OpenCL).
* **Pendiente en próximas fases:** harness `--benchmark` con exportación CSV (Fase 4) y modo `--profile` para Intel VTune (Fase 5).

## Compilación y ejecución (desde `proyecto_2/`)

### Windows (MinGW / PowerShell)

```powershell
cd proyecto_2
mingw32-make validate                         # Compila y corre --validate (14 casos x 3 variantes = 42 checks)
mingw32-make test-gpu                         # Pruebas unitarias GPU (Naive + Tiled 18x18)
mingw32-make gpu                              # Compara Naive vs Tiled en 18x18, 64x64, 512x512 y 513x513
.\bin\sobel_gpu.exe --validate --device 0     # Ejecuta --validate eligiendo GPU especifica
.\bin\sobel_gpu.exe --variant tiled --runs 10 # Ejecuta solo kernel Tiled 18x18 (SLM)
.\bin\sobel_gpu.exe --list-devices            # Lista GPUs OpenCL detectadas
```

### Linux (`make`)

```bash
cd proyecto_2
make validate
make test-gpu
make gpu
```
