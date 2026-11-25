# Módulo GPU (`src/gpu/`)

Implementación paralela de Sobel 3x3 RGBA32 en GPU mediante **OpenCL 1.2** con carga dinámica (`OpenCL.dll` en Windows / `libOpenCL.so` en Linux), diseñada para el entorno objetivo **Intel Iris Xe** (con fallback automático a cualquier otra GPU OpenCL disponible).

## Estado actual (Fase 2 completada)

* **Ciclo de vida persistente (`SobelGpuContext` — Fase 1):**
  * Encapsula de forma RAII la carga de OpenCL, selección de dispositivo (prioridad Intel Iris Xe o `--device <idx>`), `cl_context`, `cl_command_queue` con `CL_QUEUE_PROFILING_ENABLE` y compilación JIT única de ambos kernels.
  * Reutiliza buffers de dispositivo (`ensure_buffers(w, h)`) y cronometra por eventos de hardware `H2D`, `Kernel`, `D2H` y tiempo `Total` de host (`steady_clock`).
* **Dos variantes de kernel coalescentes con `uchar4` (`GpuKernelVariant` — Fase 2):**
  * **`GpuKernelVariant::Naive` (`sobel_rgba_naive`):** acceso directo a memoria global con punteros `__global const uchar4*` / `__global uchar4*` (1 transacción alineada de 32 bits por píxel), work-groups `16x16` y redondeo seguro de *Global Work Size*.
  * **`GpuKernelVariant::Tiled18x18` (`sobel_rgba_tiled`):** carga cooperativa de mosaicos `__local uchar4 tile[18][18]` (1296 bytes en SLM) sin `return` temprano (`lid = ly * 16 + lx`, bucle `for (idx = lid; idx < 324; idx += 256)` con *zero-padding* en el halo), sincronización segura `barrier(CLK_LOCAL_MEM_FENCE)` y cómputo Sobel 3x3 post-barrera para dimensiones arbitrarias (incluyendo `18x18` y `513x513`).
  * Expone `build_log()` para verificar compilación limpia sin *warnings* en el driver OpenCL de Intel.
* **Pendiente en próximas fases:** batería de validación unificada `--validate` (Fase 3), harness `--benchmark` con exportación CSV (Fase 4) y modo `--profile` para Intel VTune (Fase 5).

## Compilación y ejecución (desde `proyecto_2/`)

### Windows (MinGW / PowerShell)

```powershell
cd proyecto_2
mingw32-make test-gpu                         # Compila y corre las 7 pruebas unitarias (Naive + Tiled 18x18)
mingw32-make gpu                              # Compila y compara Naive vs Tiled en 18x18, 64x64, 512x512 y 513x513
.\bin\sobel_gpu.exe --variant naive --runs 10 # Ejecuta solo kernel Naive (uchar4)
.\bin\sobel_gpu.exe --variant tiled --runs 10 # Ejecuta solo kernel Tiled 18x18 (SLM)
.\bin\sobel_gpu.exe --list-devices            # Lista GPUs OpenCL detectadas
mingw32-make gpu-save                         # Guarda PNGs en bin/demo_gpu_*
```

### Linux (`make`)

```bash
cd proyecto_2
make test-gpu
make gpu
./bin/sobel_gpu --variant both --runs 10
```
