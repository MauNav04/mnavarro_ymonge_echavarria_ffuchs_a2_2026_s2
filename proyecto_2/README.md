# Proyecto Grupal 2 — Framework Experimental DLP (Sobel RGBA32)

**Curso:** CE 4302 — Arquitectura de Computadores II (TEC)

## Estructura

* `include/` — Encabezados públicos (estructura de imagen, kernels, validación).
* `src/` — Implementaciones (secuencial, y posteriormente SIMD CPU / GPU).
* `tests/` — Pruebas unitarias de correctitud bit a bit (`secuencial == paralelo`).
* `docs/` — Diagramas y documentación técnica.

## Compilación

```bash
cmake -S proyecto_2 -B proyecto_2/build -DCMAKE_BUILD_TYPE=Release
cmake --build proyecto_2/build -j
```

En Windows con MinGW-W64 si no hay `make` en el PATH, usar el generador `"MinGW Makefiles"` o `"Ninja"`, o especificar `CMAKE_MAKE_PROGRAM` si aplica.
