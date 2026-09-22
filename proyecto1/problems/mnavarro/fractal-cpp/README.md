# Compresor fractal en C++17 — primera versión didáctica

## Arquitectura

La biblioteca `fractal_codec` no abre archivos ni imprime mensajes. Recibe una
imagen en memoria y devuelve reglas; reconstruye una imagen a partir de reglas.
El controlador utiliza OpenCV para cargar/guardar imágenes y file_io para FRC.
La conversión cv::Mat -> fractal::Image permite probar la matemática sin OpenCV.

| Archivo | Responsabilidad |
|---|---|
| include/fractal/codec.hpp | Contrato público: Image, Config, Rule, Encoded |
| src/codec.cpp | Reducción, simetrías, ajuste, búsqueda, decodificación, MSE |
| app/file_io.hpp y .cpp | Serialización binaria FRC1 |
| app/main.cpp | Argumentos, OpenCV, tiempos y coordinación |
| tests/test_codec.cpp | Constante, gradiente, convergencia, serialización y errores |
| examples/ | Imagen sintética gris y resultado de referencia |

C++ y C son lenguajes distintos: este proyecto usa .cpp, g++ y C++17. Si el curso
exige C puro, es necesario modificar esta elección y la integración de OpenCV.

## 1. Dependencias (Ubuntu, Linux Mint o Ubuntu dentro de WSL)

```bash
sudo apt update
sudo apt install build-essential cmake libopencv-dev
```

## 2. Compilar, desde esta carpeta

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

CMake crea libfractal_codec.a, el programa fractal y codec_tests dentro de build.

## 3. Primera prueba completa

```bash
./build/fractal compress examples/gris.pgm prueba.frc 8 8
./build/fractal decompress prueba.frc reconstruida.png 80
./build/fractal compare examples/gris.pgm reconstruida.png
```

No hace falta la imagen original para ejecutar decompress. `compare` sí la usa.
La imagen PGM incluida es un mapa de píxeles gris de 64x64, generado para esta prueba.
Puedes cambiarla por un PNG; imread lo convierte a gris de 8 bits. Empieza con
64x64 o 128x128. Los lados deben ser múltiplos del range y al menos 2*range.
Se rechazan dimensiones incompatibles; no se recorta ni agrega relleno.

Argumentos opcionales de compress: lado del range (8 por defecto) y stride del
dominio (8 por defecto). El dominio siempre tiene lado 2*range. Si cambias range,
el stride no cambia automáticamente. Cada regla corresponde a un range en orden
por filas. Los dominios comienzan en múltiplos del stride mientras quepan completos.

## 4. Orden para estudiar y extender

1. Leer codec.hpp: define los datos compartidos por todos los componentes.
2. Leer reduce y transform en codec.cpp: promedio 2x2 y ocho simetrías.
3. Leer encode: targets, dominios, ajuste de contraste/brillo y mínimo error.
4. Leer decode: imagen inicial 0.5, dos buffers y aplicación repetida de reglas.
5. Leer file_io.cpp: el disco guarda campos individuales, nunca memoria de structs.
6. Leer main.cpp: el controlador conecta archivos y biblioteca.

Ejemplo conceptual de las llamadas:

```cpp
fractal::Config cfg{8, 8};
fractal::Encoded rules = fractal::encode(image, cfg);
fractal::Image recovered = fractal::decode(rules, 80);
```

## Matemática y límites

Promedio de cada grupo 2x2 para reducir el dominio. Simetrías 0..3: rotaciones
horarias de 0,90,180,270 grados. Simetrías 4..7: reflexión izquierda-derecha seguida
de esas rotaciones. Ajuste por mínimos cuadrados r ≈ s*d+b. Se restringe s a
[-0.9,0.9] y se recalcula b; dominios constantes usan s=0. Selección por SSE antes
del recorte; al decodificar se recorta a [0,1]. Iteraciones en float; parada por
número fijo (40 por defecto), no por tolerancia. La imagen reconstruida es con pérdida.

Versión secuencial con búsqueda exhaustiva. Precalcula dominios reducidos, pero
calcula simetrías dentro de la búsqueda por claridad. No incluye color, GPU,
paralelismo, quadtree ni cuantización compacta. El límite de 4096 por lado es de
validación, no una promesa de rendimiento: imágenes grandes pueden tardar mucho
y consumir mucha memoria, especialmente con stride pequeño.

## Formato FRC1

Todos los enteros son uint32 little-endian, salvo symmetry (uint8).
Parámetros float: IEEE754 binary32, bits little-endian.
Cabecera de 20 bytes: magic ASCII FRC1, ancho, alto, range, cantidad de reglas.
Cada regla de 17 bytes: x, y, symmetry, contrast, brightness.
Dominio=2*range y reducción por promedio fijados por FRC1. La posición del range
se deduce del índice. No se necesita stride para decodificar.
Para range 8: 64 bytes de píxeles gris por regla de 17 bytes, más cabecera.
No necesariamente ocupa menos que PNG/JPEG. Los archivos de salida se sobrescriben;
usa rutas nuevas y distintas de la entrada (también evita alias a la misma ruta).

## Validación realizada al preparar este proyecto

Se compiló la biblioteca, la serialización y las pruebas con g++ C++17 usando
-Wall -Wextra -Wpedantic. Todas las pruebas incluidas pasaron. También se ejecutó
el ciclo completo del núcleo con examples/gris.pgm (64x64), guardando y leyendo
examples/gris.frc y reconstruyendo examples/reconstruida.pgm con 80 iteraciones.
MSE normalizado antes de convertir a 8 bits: 0.000120174; PSNR: 39.2019 dB.
El archivo FRC mide 1108 bytes frente a 4096 bytes de intensidades originales.
El comando compare trabaja sobre imágenes de 8 bits y puede dar una cifra distinta
por redondeo. Estos resultados corresponden a una imagen sintética, no a una
comparación general frente a otros compresores.

El entorno de preparación no disponía de cabeceras/bibliotecas C++ de OpenCV ni
CMake y su instalación no fue posible. Por eso main.cpp y el proceso CMake deben
verificarse en tu equipo con las dependencias indicadas. No se afirma haberlos
compilado aquí. Para comprobar solo el núcleo sin OpenCV (con CMake instalado):

```bash
cmake -S . -B build-core -DBUILD_APP=OFF
cmake --build build-core
ctest --test-dir build-core --output-on-failure
```

## Referencias

- Explicación del método: https://janosmeny.com/blog/fractal-compression/index.html
- Lectura/escritura OpenCV: https://docs.opencv.org/4.x/d4/da8/group__imgcodecs.html

Esta implementación es propia y didáctica, inspirada en el procedimiento explicado.
