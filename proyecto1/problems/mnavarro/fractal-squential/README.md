# Compresor fractal RGB — C++17 y OpenCV

Esta versión extiende el proyecto gris con tres búsquedas independientes, una
por canal R, G y B. Continúa siendo secuencial: sirve como base para medir y
paralelizar posteriormente. El núcleo de cada canal es el algoritmo original.

## Compilar

Ubuntu, Linux Mint o Ubuntu en WSL:

```bash
sudo apt update
sudo apt install build-essential cmake libopencv-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_APP=ON
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

Descomprime esta versión en una carpeta nueva y ejecuta desde ella. No copies un
build anterior: sus rutas de CMake apuntan a la carpeta de origen. El -j2 de la
compilación no paraleliza el algoritmo del compresor.

## Primera prueba RGB

```bash
./build/fractal compress examples/color.png prueba_color.frc 8 8
./build/fractal decompress prueba_color.frc reconstruida_color.png 80
./build/fractal compare examples/color.png reconstruida_color.png
```

Para tu propia imagen, cópiala a examples y sustituye el nombre completo:

```bash
./build/fractal compress examples/foreman128.png foreman_color.frc 8 8
./build/fractal decompress foreman_color.frc foreman_color.png 80
./build/fractal compare examples/foreman128.png foreman_color.png
```

foreman128.png no viene incluido. Usa una fuente realmente a color: si el
original es gris, trabajar con tres canales no inventa colores.

Argumentos: compress entrada salida [range=8] [stride=8]. Dominio=2*range.
Decompress: entrada salida [iteraciones=40]. El 80 aplica 80 iteraciones a cada
canal. Los tres planos reconstruidos se reúnen al guardar la imagen. No se
necesita el original para descomprimir. Compare calcula MSE y PSNR por canal,
y global (PSNR calculado desde MSE global, no promedio de los PSNR individuales).

## Cómo se implementó

1. app/image_io.cpp carga IMREAD_COLOR. OpenCV devuelve BGR y lo convertimos
   explícitamente a tres matrices en orden RGB con intensidades entre 0 y 1.
2. MultiImage contiene una colección de Image (uno o tres planos).
3. encode(MultiImage) llama a encode(Image) por cada plano. Se busca dominio,
   simetría, contraste y brillo independientemente para cada canal y range.
4. MultiEncoded contiene una colección de Encoded con todas esas reglas.
5. color_io escribe RGB en FRC2 y gris en el formato previo FRC1.
6. decode(MultiEncoded) reconstruye cada plano desde gris uniforme 0.5 usando
   dos buffers por plano. image_io reordena RGB a BGR al entregar datos a OpenCV.

| Archivo | Papel |
|---|---|
| include/fractal/codec.hpp, src/codec.cpp | Núcleo original para un canal, sin E/S |
| include/fractal/color.hpp, src/color.cpp | Coordinación de los canales en memoria |
| app/file_io.hpp y .cpp | Lectura/escritura FRC1 (gris) |
| app/color_io.hpp y .cpp | Lectura FRC1/FRC2 y escritura multicanal |
| app/image_io.hpp y .cpp | Adaptación OpenCV ↔ matrices propias |
| app/main.cpp | Argumentos, llamadas, tamaños, tiempos y métricas |
| tests/test_codec.cpp | Regresión del núcleo gris |
| tests/test_color.cpp | RGB, independencia de canales, FRC2, FRC1 y errores |
| tests/test_image_io.cpp | Orden de canales RGB/BGR y PNG con OpenCV |

La biblioteca sigue sin conocer rutas de archivos ni imprimir en consola.
CMake incorpora src/color.cpp en fractal_codec y los adaptadores en el ejecutable.
Los tiempos de codificación/decodificación excluyen E/S; también se informa el
tiempo total. Compilar con Release importa para una comparación de rendimiento.

## Comparación controlada gris vs. RGB

```bash
./build/fractal compress-gray examples/color.png gris_nuevo.frc 8 8
./build/fractal decompress gris_nuevo.frc gris_nuevo.png 80
./build/fractal compare-gray examples/color.png gris_nuevo.png
```

compress ahora significa RGB; compress-gray reproduce el modo gris. El nuevo
decodificador detecta automáticamente FRC1 y FRC2. El ejecutable anterior no
puede leer FRC2. La extensión de ambos sigue siendo .frc.

Para igual tamaño, range y stride, RGB evalúa tres veces tantos candidatos como
un solo canal: costo O(3*N_ranges*N_domains*8*range²). No garantiza exactamente
triple tiempo de reloj; mide con igual compilación y parámetros. El bucle por
canales y los ranges son oportunidades de paralelización. No se comparte una
búsqueda geométrica de luminancia entre canales en esta versión.

## Formato binario y tamaños

FRC1: cabecera 20 bytes y 17 bytes por regla, un canal.
FRC2: magic ASCII FRC2, width, height, range, channels=3, rules_per_channel.
Cabecera total de 24 bytes. Campos numéricos de cabecera uint32 little-endian.
Luego todas las reglas R, después G, después B; dentro de cada canal, ranges
por filas. Cada regla: x uint32, y uint32, symmetry uint8, contrast float32,
brightness float32. Son 17 bytes, con enteros y bits IEEE754 little-endian.

Para 128x128 y range=8 hay 256 ranges por canal:
- RGB original de 8 bits por canal: 128*128*3 = 49152 bytes de píxeles.
- FRC2: 24 + 256*3*17 = 13080 bytes.
- Relación contra píxeles sin comprimir: aproximadamente 3.76:1.

El tamaño no depende del stride porque solo se guarda la mejor regla por range
por canal. No se promete mejorar el tamaño de un PNG o JPEG ya comprimido.

## Alcance

Escala de grises o RGB, 8 bits por canal al cargar/guardar; no conserva alfa,
metadatos ni precisión original de 16 bits. Los lados deben ser múltiplos del
range y al menos 2*range. Range 2..64; dimensiones <=4096. No agrega relleno.
El límite máximo no garantiza tiempos prácticos: comienza con 128x128.
La salida existente puede sobrescribirse: usa nombres nuevos.

Dominio reducido con promedio 2x2, ocho simetrías, ajuste r ≈ s*d+b por mínimos
cuadrados y s en [-0.9,0.9]. Decodificación con recorte [0,1] y número fijo de
iteraciones. Es un método con pérdida; puede producir bordes de bloques y errores
de color. Una mayor cantidad de iteraciones no recupera detalles que las reglas
no representan. La búsqueda exhaustiva en esta base es deliberadamente sencilla.

## Validación real de esta entrega

Se compilaron con g++ C++17, -O2 -Wall -Wextra -Wpedantic el núcleo gris, RGB,
serializadores y sus pruebas. Pasaron ambas suites. Se verificaron canales
constantes distintos, gradientes, convergencia, equivalencia con búsquedas
monocanal, round-trip FRC2, lectura FRC1 y rechazo de entradas inválidas.

También se ejecutó codificación, guardado, lectura y reconstrucción RGB de una
imagen sintética 64x64. examples/color.png y color_reconstruida.png muestran el
original y resultado; examples/color.frc es el código correspondiente (3288 bytes).
MSE antes del redondeo a 8 bits: 0.000174575; PSNR: 37.5802 dB, con 80 iteraciones.
Los PNG de referencia se exportaron desde PPM con Pillow; no se usó OpenCV aquí.
Compare mide después del redondeo y puede reportar otra cifra.

Este entorno no dispone de CMake ni de las dependencias C++ de OpenCV. Por ello
el ejecutable completo, la configuración CMake y test_image_io todavía deben
compilarse/ejecutarse en tu equipo. La prueba de canales del núcleo pasó; la
prueba específica de conversión OpenCV está incluida pero no ejecutada aquí.

Para construir solamente el núcleo con CMake y sin OpenCV:

```bash
cmake -S . -B build-core -DBUILD_APP=OFF
cmake --build build-core
ctest --test-dir build-core --output-on-failure
```

Referencias:
- https://janosmeny.com/blog/fractal-compression/index.html
- https://docs.opencv.org/4.x/d4/da8/group__imgcodecs.html
