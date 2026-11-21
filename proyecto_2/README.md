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

## Uso rápido con Makefile

Desde `proyecto_2/` (en Linux usa `make`, en Windows con MinGW usa `mingw32-make`):

```powershell
cd proyecto_2

mingw32-make test       # Compila y corre las pruebas unitarias
mingw32-make seq        # Compila y corre el baseline (512x512, 1920x1080, 3840x2160)
mingw32-make seq-save   # Corre 512x512 y guarda PNGs en bin/demo_512x512_*.png
mingw32-make clean      # Limpia archivos compilados
```

*(También puedes correrlo desde la raíz agregando `-C proyecto_2`, por ejemplo: `mingw32-make -C proyecto_2 seq`).*
