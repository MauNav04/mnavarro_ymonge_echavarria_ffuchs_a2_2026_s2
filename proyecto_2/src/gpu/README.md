# Módulo GPU (`src/gpu/`)

Implementación masivamente paralela de Sobel RGBA32 en GPU (CUDA / OpenCL / Vulkan Compute según hardware disponible).

* **Estrategia propuesta (Avance 1):** bloques de `16x16` hilos cargando cooperativamente mosaicos de `18x18` píxeles (incluyendo halo de 1 píxel) a memoria compartida.
* **Contrato:** reutilizar `ImageRGBA` y validar `max |O_seq - O_par| == 0` mediante `compare_images`.
