# Módulo CPU SIMD (`src/simd/`)

Implementación vectorizada de Sobel RGBA32 usando extensiones SIMD del procesador objetivo (AVX2 en x86-64 o NEON en ARM64).

* **Contrato:** reutilizar `ImageRGBA` (`include/image_rgba.hpp`) y validar igualdad exacta bit a bit (`compare_images(...).exact_match() == true`) contra `sobel_sequential`.
* **Reglas sugeridas:** agregar los targets `simd` y `test-simd` en `proyecto_2/Makefile` / `CMakeLists.txt`.
