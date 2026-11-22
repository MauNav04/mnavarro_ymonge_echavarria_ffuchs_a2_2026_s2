# Proyecto Grupal 2 — Framework Experimental DLP (Sobel RGBA32)

**Curso:** CE 4302 — Arquitectura de Computadores II (TEC)

## Estructura de `proyecto_2/`

```text
proyecto_2/
├── include/          # Encabezados compartidos (ImageRGBA, sobel_sequential, validacion)
├── src/
│   ├── image_rgba.cpp        # Contenedor RGBA32, generadores, compare_images, save_png/ppm
│   ├── sobel_sequential.cpp  # Baseline secuencial de referencia
│   ├── main.cpp              # CLI de ejecucion y benchmark del baseline
│   ├── simd/                 # Implementacion CPU SIMD (AVX2 / NEON)
│   └── gpu/                  # Implementacion GPU (CUDA / OpenCL / tiling 18x18)
├── fpga/             # Acelerador FPGA (DE1-SoC Cyclone V, ISA minimo, JTAG)
├── tests/            # Pruebas unitarias de correctitud bit a bit
├── scripts/          # Benchmarking, profiling (perf/Nsight) y graficas
├── docs/             # Diagramas y documentacion tecnica
├── Makefile          # Compilacion y ejecucion rapida (make seq, make test, ...)
└── CMakeLists.txt    # Build multiplataforma con CMake + CTest
```

## Compilación y ejecución

### En Linux (`make`)

Asegúrate de tener instalados `g++` (con soporte C++17) y `make`. Desde la carpeta `proyecto_2/`:

```bash
cd proyecto_2

make clean       # Limpia binarios previos (importante si vienes de Windows)
make test        # Compila y corre pruebas unitarias (secuencial + SIMD bit a bit)
make seq         # Corre solo el baseline secuencial (512x512, 1920x1080, 3840x2160)
make simd        # Corre SIMD (AVX2) y compara tiempo, speedup y exactitud vs secuencial
make simd-save   # Corre SIMD 512x512 y guarda PNGs en bin/demo_*
```

> **Nota para presentaciones / benchmarks formales:** Puedes aumentar las repeticiones y el calentamiento ejecutando el binario directamente:
> ```bash
> ./bin/sobel_simd --warmup 5 --runs 50
> ```

### Cómo generar las imágenes PNG de entrada y salida

Por defecto, `make test`, `make seq` y `make simd` ejecutan todo en memoria (**no** escriben imágenes en disco para no alterar las mediciones de tiempo). Para guardar las imágenes resultantes en `.png` dentro de `bin/`:

* **En Linux:**
  ```bash
  make simd-save   # Genera bin/demo_512x512_input.png y bin/demo_512x512_simd_output.png
  make seq-save    # Genera bin/demo_512x512_input.png y bin/demo_512x512_output.png
  ```
* **En Windows (PowerShell / MinGW):**
  ```powershell
  mingw32-make simd-save
  # O ejecutando el binario directamente desde proyecto_2/:
  .\bin\sobel_simd.exe --width 512 --height 512 --save bin\demo
  ```
* **Resolución personalizada con `--save`:**
  ```bash
  ./bin/sobel_simd --width 1920 --height 1080 --save bin/prueba
  ```

### Alternativa con CMake (Linux o Windows)

```bash
cd proyecto_2
cmake -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/sobel_simd
```

### En Windows (MinGW / PowerShell)

Desde `proyecto_2/` (requiere `g++` y `mingw32-make` en el `PATH`; el `Makefile` enlaza con `-static` en Windows):

```powershell
cd proyecto_2

mingw32-make test       # Compila y corre las pruebas unitarias (secuencial + SIMD)
mingw32-make seq        # Compila y corre el baseline secuencial
mingw32-make simd       # Compila y corre el benchmark SIMD vs secuencial
mingw32-make simd-save  # Corre SIMD 512x512 y guarda PNGs en bin/demo_*
mingw32-make clean      # Limpia archivos compilados
```

*(También puedes correrlo desde la raíz del repositorio agregando `-C proyecto_2`, por ejemplo: `mingw32-make -C proyecto_2 simd`).*

---

## Cómo verificar la aceleración (Speedup) y correctitud

Al ejecutar `make simd` (o `./bin/sobel_simd`), el programa ejecuta tanto la versión **SIMD** como la **secuencial** sobre las mismas imágenes y muestra una tabla comparativa en consola:

```text
Resolucion     SIMD (ms)    Min (ms)    Std (ms)    Q (MPix/s)    Seq (ms)    Speedup     Match   Checksum (FNV-1a)
------------------------------------------------------------------------------------------------------------------
512x512            0.251       0.241       0.007      1044.565       2.355      9.39x     EXACT   0x82d6d127d3b02c72
1920x1080          1.880       1.860       0.018      1103.073      12.949      6.89x     EXACT   0x77b9d8541ea14075
3840x2160          7.705       6.924       0.496      1076.429      56.152      7.29x     EXACT   0x4c19d0c76c155d64
```

* **`SIMD (ms)` vs `Seq (ms)`:** Tiempo promedio de ejecución en milisegundos.
* **`Speedup`:** Factor de aceleración ($T_{\text{seq}} / T_{\text{simd}}$). Un valor mayor a `1.00x` demuestra cuánto más rápido es SIMD.
* **`Q (MPix/s)`:** Rendimiento en megapíxeles procesados por segundo.
* **`Match`:** Debe decir `EXACT` (indica que $\max |O_{\text{seq}} - O_{\text{simd}}| = 0$, es decir, salida idéntica bit a bit).

### Perfilado de hardware en Linux con `perf` (opcional)

Para demostrar a nivel de microarquitectura por qué SIMD es más rápido (reducción drástica de instrucciones ejecutadas y ciclos), puedes comparar ambos ejecutables en Linux con `perf stat`:

```bash
perf stat -e cycles,instructions,L1-dcache-load-misses ./bin/sobel_seq --runs 20
perf stat -e cycles,instructions,L1-dcache-load-misses ./bin/sobel_simd --simd-only --runs 20
```
