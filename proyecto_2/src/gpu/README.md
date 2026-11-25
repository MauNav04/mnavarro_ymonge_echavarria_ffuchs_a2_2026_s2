# Módulo GPU (`src/gpu/`)

Implementación paralela de Sobel 3x3 RGBA32 en GPU mediante **OpenCL 1.2** con carga dinámica (`OpenCL.dll` en Windows / `libOpenCL.so` en Linux), diseñada para el entorno objetivo **Intel Iris Xe** (con fallback automático a cualquier otra GPU OpenCL disponible).

## Estado actual (Fases 1 a 5 completadas)

* **Fase 1 — Ciclo de vida persistente (`SobelGpuContext`):**
  * Encapsula de forma RAII la carga de OpenCL, selección de dispositivo (prioridad Intel Iris Xe o `--device <idx>`), `cl_context`, `cl_command_queue` con `CL_QUEUE_PROFILING_ENABLE` y compilación JIT única de ambos kernels.
  * Reutiliza buffers de dispositivo (`ensure_buffers(w, h)`) y cronometra por eventos de hardware `H2D`, `Kernel`, `D2H` y tiempo `Total` de host (`steady_clock`).
* **Fase 2 — Kernels coalescentes `uchar4` (`GpuKernelVariant`):**
  * **`GpuKernelVariant::Naive` (`sobel_rgba_naive`):** acceso directo a memoria global con `__global const uchar4*` / `__global uchar4*`, work-groups `16x16` y redondeo seguro de *Global Work Size*.
  * **`GpuKernelVariant::Tiled18x18` (`sobel_rgba_tiled`):** carga cooperativa de mosaicos `__local uchar4 tile[18][18]` (1296 bytes en SLM) sin `return` temprano, `barrier(CLK_LOCAL_MEM_FENCE)` y cómputo Sobel 3x3 post-barrera.
* **Fase 3 — Batería de validación (`--validate`):**
  * `run_validation_suite()` compara `sobel_sequential` contra `SIMD (AVX2)`, `gpu_naive` y `gpu_tiled` en 14 casos (42 verificaciones) reportando `max_abs_diff = 0` y `MAE = 0.0`.
* **Fase 4 — Harness de benchmarking y CSV (`--benchmark`):**
  * Evalúa `seq`, `simd`, `gpu_naive` y `gpu_tiled` en `512x512`, `1920x1080` y `3840x2160` (`warmup=3`, `iters=30` por defecto), reportando mediana, desviación estándar, *speedup* (`S_total` y `S_kernel`), *throughput* (`MPx/s`) y eficiencia paralela (`E = S / P`), y exporta `<prefijo>_summary.csv` y `<prefijo>_raw.csv`.
* **Fase 5 — Modo de perfilado limpio (`--profile`):**
  * Bandera `--profile <seq|simd|gpu_naive|gpu_tiled> <width> <height> <iters>` sin validación ni escritura de PNGs, diseñada para **Intel VTune Profiler**.
  * En modos CPU (`seq`, `simd`), **no inicializa OpenCL**, evitando ruido del driver/JIT durante análisis de microarquitectura CPU.
  * En modos GPU (`gpu_naive`, `gpu_tiled`), sube la imagen una vez y ejecuta `<iters>` despachos limpios del kernel (opcionalmente `--profile-with-transfers` incluye `H2D + D2H` en cada iteración).
  * Los ejecutables `sobel_seq` y `sobel_simd` permanecen 100% independientes del módulo GPU.

## Compilación y ejecución (desde `proyecto_2/`)

### Windows (MinGW / PowerShell)

```powershell
cd proyecto_2
mingw32-make validate                                         # 42/42 checks bit a bit (Seq vs SIMD/GPU)
mingw32-make benchmark                                        # Benchmark 512x512, 1080p, 4K + CSVs en bin/
mingw32-make test-gpu                                         # Pruebas unitarias GPU (7/7)
mingw32-make gpu                                              # Modo rapido Naive vs Tiled
.\bin\sobel_gpu.exe --list-devices                            # Lista GPUs OpenCL detectadas
```

### Modo Perfilado (`--profile`) e Intel VTune Profiler (Windows Nativo)

Prueba rápida en consola antes de abrir VTune:

```powershell
.\bin\sobel_gpu.exe --profile simd 3840 2160 100
.\bin\sobel_gpu.exe --profile gpu_naive 3840 2160 100
.\bin\sobel_gpu.exe --profile gpu_tiled 3840 2160 100
```

Checklist en **Intel VTune Profiler** (Application = `proyecto_2\bin\sobel_gpu.exe`):

1. **CPU SIMD (AVX2):**
   * Análisis: **Microarchitecture Exploration** y/o **Memory Access**.
   * Application parameters: `--profile simd 3840 2160 100`
   * Registrar: fallos de caché **L1D**, ancho de banda de memoria y **vectorización efectiva** (instrucciones AVX2).
2. **GPU (Intel Iris Xe — Naive vs Tiled):**
   * Análisis: **GPU Compute/Media Hotspots**.
   * Corrida 1 parameters: `--profile gpu_naive 3840 2160 100`
   * Corrida 2 parameters: `--profile gpu_tiled 3840 2160 100`
   * Registrar y comparar: ocupación de GPU (*EU Array Active* vs. *Stalled* / *Idle*), ancho de banda de memoria global / L3 / DRAM (GB/s) y uso/ancho de banda de memoria local (**SLM**) en `sobel_rgba_tiled`.

### Linux (`make`)

```bash
cd proyecto_2
make validate
make benchmark
./bin/sobel_gpu --profile gpu_tiled 3840 2160 100
```
