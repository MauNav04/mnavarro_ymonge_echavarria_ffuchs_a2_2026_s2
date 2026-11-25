# Módulo GPU (`src/gpu/`)

Implementación paralela de Sobel 3x3 RGBA32 en GPU mediante **OpenCL 1.2** con carga dinámica (`OpenCL.dll` en Windows / `libOpenCL.so` en Linux), diseñada para el entorno objetivo **Intel Iris Xe** (con fallback automático a cualquier otra GPU OpenCL disponible).

## Estado actual (Fase 4 completada)

* **Ciclo de vida persistente (`SobelGpuContext` — Fase 1):**
  * Encapsula de forma RAII la carga de OpenCL, selección de dispositivo (prioridad Intel Iris Xe o `--device <idx>`), `cl_context`, `cl_command_queue` con `CL_QUEUE_PROFILING_ENABLE` y compilación JIT única de ambos kernels.
  * Reutiliza buffers de dispositivo (`ensure_buffers(w, h)`) y cronometra por eventos de hardware `H2D`, `Kernel`, `D2H` y tiempo `Total` de host (`steady_clock`).
* **Dos variantes de kernel coalescentes con `uchar4` (`GpuKernelVariant` — Fase 2):**
  * **`GpuKernelVariant::Naive` (`sobel_rgba_naive`):** acceso directo a memoria global con `__global const uchar4*` / `__global uchar4*`, work-groups `16x16` y redondeo seguro de *Global Work Size*.
  * **`GpuKernelVariant::Tiled18x18` (`sobel_rgba_tiled`):** carga cooperativa de mosaicos `__local uchar4 tile[18][18]` (1296 bytes en SLM) sin `return` temprano, `barrier(CLK_LOCAL_MEM_FENCE)` y cómputo Sobel 3x3 post-barrera.
* **Batería de validación de correctitud (`--validate` — Fase 3):**
  * `run_validation_suite()` compara `sobel_sequential` contra `SIMD (AVX2)`, `gpu_naive` y `gpu_tiled` en 14 casos (42 verificaciones) reportando `max_abs_diff = 0` y `MAE = 0.0`.
* **Harness de benchmarking y exportación CSV (`--benchmark` — Fase 4):**
  * Evalúa `seq`, `simd`, `gpu_naive` y `gpu_tiled` en **`512x512`**, **`1920x1080`** y **`3840x2160`** en la misma máquina.
  * Protocolo reproducible: `3` iteraciones de *warmup* + `30` repeticiones medidas (configurables con `--warmup` y `--iters`).
  * Separa tiempos de GPU por eventos OpenCL (`T_h2d`, `T_kernel`, `T_d2h`) frente a `T_total` (`std::chrono::steady_clock`).
  * Calcula **mediana**, **desviación estándar**, **speedup** (`S_total` y `S_kernel`), **throughput** (`MPx/s`) y **eficiencia paralela** (`E = S / P`, con `P = 8` carriles AVX2 en CPU y `P = 80` EUs en Intel Iris Xe).
  * Exporta dos archivos CSV limpios: `<prefijo>_summary.csv` (resumen estadístico) y `<prefijo>_raw.csv` (muestras crudas por iteración).
* **Pendiente en próxima fase:** modo `--profile` limpio para **Intel VTune Profiler** (Fase 5).

## Compilación y ejecución (desde `proyecto_2/`)

### Windows (MinGW / PowerShell)

```powershell
cd proyecto_2
mingw32-make benchmark                                        # Corre --benchmark (30 iters) y exporta CSVs en bin/
.\bin\sobel_gpu.exe --benchmark --iters 30 --warmup 3 --csv bin/bench_intel
mingw32-make validate                                         # Bateria --validate (42/42 checks)
mingw32-make test-gpu                                         # Pruebas unitarias GPU (7/7)
mingw32-make gpu                                              # Modo rapido Naive vs Tiled
.\bin\sobel_gpu.exe --list-devices                            # Lista GPUs OpenCL detectadas
```

### Linux (`make`)

```bash
cd proyecto_2
make benchmark
make validate
make test-gpu
```
