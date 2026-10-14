# Revisión de la implementación secuencial

Fecha: 2026-10-13. Alcance: revisión y extracción de la etapa 1; sin cambios en el algoritmo matemático.

## Flujo y responsabilidades

- `sequential/main.cpp` valida argumentos, carga imágenes, llama al codec y mide tiempo
  de cómputo separado del tiempo total. Comandos: `compress`, `compress-gray`, `decompress`,
  `compare` y `compare-gray`; la interfaz común permite seleccionar modelo y configuración; en etapa 1 solo admite `sequential`, un equipo y un hilo.
- `app/image_io.cpp` convierte BGR de OpenCV a planos RGB normalizados y viceversa.
- `src/codec.cpp` valida geometría y muestras, precalcula dominios de lado `2*range`
  reducidos por promedio 2×2 y busca exhaustivamente una regla por range.
- Cada candidato prueba ocho simetrías, ajusta `R ≈ s*D+b` por mínimos cuadrados, limita
  `s` a `[-0.9, 0.9]` y compara suma de errores cuadrados. En empates conserva el primer
  candidato del recorrido dominio/simetría mediante comparación estricta `<`.
- `src/color.cpp` procesa uno o tres canales secuencialmente con la misma configuración.
- La reconstrucción parte de intensidad 0.5, utiliza doble buffer y un número fijo de iteraciones;
  limita los valores reconstruidos a `[0,1]`, sin criterio de parada por convergencia.
- `app/file_io.cpp` serializa gris FRC1; `color_io.cpp` escribe RGB FRC2 y lee ambos.
  Las reglas conservan orden por filas, enteros little endian y flotantes IEEE754 binary32.

## Trabajo paralelizable y restricciones

Para un canal, `N_R=(W/r)*(H/r)` y
`N_D=(floor((W-2*r)/stride)+1)*(floor((H-2*r)/stride)+1)`.
La búsqueda cuesta `O(N_R*N_D*8*r²)`; RGB realiza tres búsquedas.

Los ranges son independientes y los dominios precalculados pueden compartirse para lectura.
Los temporales, el mejor error y la regla deben pertenecer a cada trabajador. El vector `rules` se dimensiona una vez; cada range publica una regla por índice exclusivo. Dividir dominios exige combinar mínimos preservando el
orden de desempate; cambiar ese orden puede cambiar los archivos aun con errores iguales.

Los canales ofrecen hasta tres tareas independientes. En decodificación se pueden repartir
ranges leyendo `current` y escribiendo regiones disjuntas de `next`, con sincronización antes
del intercambio y de la siguiente iteración. Las iteraciones dependen unas de otras.

La búsqueda exhaustiva es el cuello de botella esperado, pendiente de profiling. `transform`
crea un vector por candidato y repite cálculos; no se han optimizado ni medido estos costos.

## Estado del diseño y limitaciones

La biblioteca única `fractal_codec` reside en `src/` e `include/`, sin dependencia de OpenCV.
`search.hpp` expone preparación del catálogo, preparación de ranges, búsqueda sobre un
intervalo semiabierto de dominios y reducción determinista de candidatos. El modelo secuencial
utiliza estas mismas funciones; la etapa 1 no incorpora pthreads ni planificación concurrente.

`execution.hpp` define modelos, equipos e hilos por equipo y una interfaz común de codificación
mono/RGB y reconstrucción RGB/gris. Los modelos futuros se reconocen y rechazan explícitamente
como pendientes; no ejecutan silenciosamente el modelo secuencial.

La contradicción queda resuelta por decisión del usuario: equipos de hilos por range,
con dominios repartidos y reducción de mínimos antes de publicar la regla. Ejemplo: 5 equipos
de 2 hilos. La propuesta vigente se encuentra en [Propuestas.md](Propuestas.md).

La compresión es con pérdida; no garantiza reducir el tamaño respecto de PNG/JPEG. El ratio
actual compara bytes FRC contra muestras originales de 8 bits, no contra el PNG ni los float
en memoria. El límite 4096×4096 permite geometría válida, pero no garantiza tiempo práctico.

## Validación disponible

- `codec_tests`: constante, gradiente, convergencia, serialización FRC1, truncado y coordenadas.
- `color_tests`: canales distintos, gradientes, convergencia, equivalencia monocanal/RGB,
  FRC2, compatibilidad FRC1 y algunos datos inválidos.
- `image_io_tests`: orden RGB/BGR y lectura/escritura PNG.

- `execution_tests`: búsqueda completa frente a intervalos de dominios, particiones desiguales
  y vacías, reducción en orden inverso, empates, ejecución RGB y configuraciones inválidas.

No se afirma cobertura exhaustiva de todas las simetrías y límites de parámetros. Los resultados de
compilación, pruebas y equivalencia tras la reorganización se registran en el README principal.
