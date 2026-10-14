# Framework Experimental de Modelos Multithreading

Este README es la **única fuente de verdad del progreso, arquitectura y validaciones**.
[README_Cod.md](README_Cod.md) conserva los criterios originales. Las reglas permanentes
están en [AGENTS.md](AGENTS.md) y la guía académica de la versión base en
[sequential/README.md](sequential/README.md).

Alcance autorizado: **etapa 1**, con equipos de hilos por range como diseño aprobado.
Las etapas 2–8 continúan pendientes y requieren solicitud explícita.

## 1. Descripción del proyecto

Proyecto de Arquitectura de Computadores II (CE4302) del Instituto Tecnológico de Costa Rica.

El objetivo es implementar y evaluar experimentalmente cuatro modelos de ejecución concurrente aplicados a la compresión fractal de imágenes:

- Fine-Grained Multithreading
- Coarse-Grained Multithreading
- Chip Multiprocessing (CMP)
- Simultaneous Multithreading (SMT)

El proyecto parte de una implementación secuencial existente, utilizada como referencia para medir el rendimiento de las versiones concurrentes.

Considera que yo no tengo una computadora con LINUX, así que la versión de SMT y Linux perf no se va a poder probar de la manera en que lo pide el profesor. Implementa esto adaptando lo que existen en la carpeta ../../echavarria/RayTracing/SMT, ../../echavarria/RayTracing/scripts/Makefile y ../../echavarria/RayTracing/scripts/profile_perf.py .

## 2. Tecnologías y entorno

- **Lenguaje principal:** C++
- **Concurrencia:** POSIX Threads (pthreads)
- **Compilación:** CMake
- **Automatización:** Makefile y Python
- **Profiling:** Linux Perf
- **Sistema operativo:** Linux
- **Hardware de referencia:** Intel Core i5-1335U, 12 procesadores lógicos reportados por `lscpu`

La topología real de núcleos físicos, P-cores, E-cores e hilos SMT debe verificarse antes de configurar afinidad y realizar benchmarks.

## 3. Arquitectura del proyecto

Estructura vigente tras la extracción de la etapa 1:

- `sequential/`: punto de entrada secuencial (nombre actualizado por solicitud del usuario).
- `app/`: entrada/salida compartida y serialización FRC1/FRC2.
- `tests/`: pruebas del núcleo, ejecución y entrada/salida.
- `fine-grained/`: simulación de planificación de grano fino.
- `coarse-grained/`: implementación de grano grueso.
- `CMP/`: ejecución paralela sobre múltiples núcleos.
- `SMT/`: ejecución utilizando hilos lógicos y comparación de configuraciones de hardware.
- `src/`: implementación compartida de compresión fractal.
- `include/`: interfaces y declaraciones compartidas.
- `scripts/`: automatización de compilación, pruebas, benchmarks, métricas y profiling.
- `metrics/`: resultados experimentales y gráficas.
- `docs/`: documentación técnica complementaria.

**Diseño aprobado:** equipos de hilos por range, reparto de dominios entre sus miembros y reducción de mínimos para publicar una regla. Ejemplo: 10 hilos = 5 equipos × 2 hilos.

**Principio arquitectónico:** Todos los modelos deben reutilizar el mismo algoritmo de compresión y diferenciarse principalmente por su política de ejecución y distribución de trabajo.

## 4. Roadmap y seguimiento

Estados permitidos para las tareas:

- `[ ]` Pendiente o en desarrollo.
- `[x]` Implementada y validada.

Una etapa se considera completa únicamente cuando se cumplen sus criterios de aceptación.

### Etapa 1 — Arquitectura y punto de entrada

- [x] Analizar la implementación secuencial existente.
- [x] Identificar operaciones paralelizables.
- [x] Extraer la lógica compartida a `src/` e `include/`.
- [x] Establecer una interfaz común de ejecución.
- [x] Implementar el punto de entrada y sus parámetros.
- [x] Validar que la versión secuencial mantiene su funcionalidad.

**Aceptación cumplida:** núcleo único reutilizable y ejecución secuencial validada.
Pruebas y equivalencia registradas en el historial; no se implementaron las etapas 2–8.

### Etapa 2 — Fine-Grained

- [ ] Implementar ejecución mediante pthreads.
- [ ] Implementar planificación cooperativa Round-Robin.
- [ ] Definir cuantums mínimos de trabajo.
- [ ] Implementar sincronización entre trabajadores.
- [ ] Verificar la alternancia de ejecución.
- [ ] Validar resultados contra la versión secuencial.

**Aceptación:** Simulación funcional de alternancia por cuantums, con comportamiento verificable y resultados correctos.

### Etapa 3 — Coarse-Grained

- [ ] Implementar ejecución mediante pthreads.
- [ ] Definir eventos de bloqueo o estancamiento.
- [ ] Implementar la política de cambio ante estos eventos.
- [ ] Evitar cambios periódicos incondicionales de Fine-Grained.
- [ ] Validar resultados contra la versión secuencial.

**Aceptación:** Política de ejecución documentada, comprobable y diferenciada de Fine-Grained.

### Etapa 4 — CMP

- [ ] Implementar distribución paralela de trabajo.
- [ ] Ejecutar trabajadores sobre núcleos físicos.
- [ ] Implementar afinidad de CPU.
- [ ] Evitar condiciones de carrera.
- [ ] Verificar escalabilidad con diferentes cantidades de trabajadores.

**Aceptación:** Paralelismo real sobre múltiples núcleos y resultados correctos.

### Etapa 5 — SMT

- [ ] Detectar topología de hilos lógicos y núcleos físicos.
- [ ] Implementar configuración de afinidad para SMT.
- [ ] Preparar ejecución con SMT habilitado y deshabilitado.
- [ ] Registrar y comparar ambas configuraciones.
- [ ] Validar resultados contra la versión secuencial.

**Aceptación:** Comparación reproducible de ambas configuraciones de hardware, con mediciones verificadas.

### Etapa 6 — Integración y pruebas

- [ ] Configurar CMake para todos los ejecutables.
- [ ] Integrar Makefile para automatización.
- [ ] Implementar `test_models.py`.
- [ ] Validar resultados para diferentes cantidades de trabajadores.
- [ ] Comprobar errores, sincronización y ausencia de bloqueos.

**Aceptación:** Todos los modelos compilan, ejecutan y superan sus pruebas funcionales.

### Etapa 7 — Benchmarking y métricas

- [ ] Implementar `benchmark.py`.
- [ ] Ejecutar al menos 200 repeticiones por configuración experimental.
- [ ] Guardar resultados individuales en CSV.
- [ ] Calcular tiempo, speedup, eficiencia y escalabilidad.
- [ ] Calcular media, desviación estándar e intervalos de confianza del 95 %.
- [ ] Implementar `metrics.py` para generar gráficas automáticamente.
- [ ] Generar boxplots y analizar convergencia.

**Aceptación:** Resultados estadísticos y gráficas reproducibles desde los CSV.

### Etapa 8 — Profiling

- [ ] Implementar `profile.py` utilizando Linux Perf.
- [ ] Recolectar contadores de hardware disponibles.
- [ ] Analizar ciclos, instrucciones, fallos de caché y cambios de contexto.
- [ ] Comparar el comportamiento de los modelos.
- [ ] Analizar el impacto de SMT habilitado/deshabilitado.

**Aceptación:** Resultados de profiling organizados y comparables.

## 5. Metodología experimental

Todas las implementaciones deben utilizar entradas y condiciones de trabajo equivalentes.

Métricas principales:

- Tiempo total de ejecución.
- Speedup respecto a la versión secuencial.
- Eficiencia paralela.
- Escalabilidad según cantidad de trabajadores.

Los benchmarks deben registrar modelo, cantidad de trabajadores, parámetros de entrada, afinidad, estado SMT, tiempo y configuración del hardware.

Las mediciones principales deben ejecutarse sobre hardware físico, no en máquinas virtuales o contenedores.

Los experimentos deben distinguir entre tiempo total y tiempo exclusivo de cómputo cuando se midan ambos.

## 6. Compilación y ejecución

Documentar los comandos reales y verificados para:

- Compilar mediante CMake.
- Ejecutar cada modelo.
- Ejecutar pruebas funcionales.
- Generar benchmarks.
- Generar gráficas.
- Ejecutar Linux Perf.

Desde `proyecto1/problems/mnavarro/fractal-cpp/`:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_APP=ON
cmake --build build -j2
ctest --test-dir build --output-on-failure
./build/fractal --help
./build/fractal compress sequential/examples/color128.png sequential/examples/results/color.frc --model sequential --teams 1 --threads-per-team 1 --range 8 --stride 8
./build/fractal decompress sequential/examples/results/color.frc sequential/examples/results/color.png --iterations 80
./build/fractal compare sequential/examples/color128.png sequential/examples/results/color.png
```

Se conservan los argumentos posicionales originales (`compress entrada salida 8 8` y
`decompress entrada salida 80`), el modo gris (`compress-gray`, `compare-gray`) y el alias
`--model sequential`. Las opciones no se pueden repetir ni duplicar con argumentos posicionales.

`--teams` y `--threads-per-team` describen la configuración común. En etapa 1 deben valer 1.
Fine-Grained, Coarse-Grained, CMP y SMT se reconocen pero se rechazan como no implementados.
No se aceptan opciones de ejecución en `compare` ni parámetros de compresión en `decompress`.

También se puede construir desde `sequential/` con `cmake -S sequential -B build`.
`-DBUILD_APP=OFF` permite construir la biblioteca y pruebas sin OpenCV.
CTest registra la prueba `cli_tests` cuando Python3 está disponible; no es dependencia de
la biblioteca C++. Las pruebas del núcleo no requieren OpenCV ni Python.
Los comandos de benchmarking y profiling se documentarán en sus etapas.

## 7. Decisiones técnicas

Registrar únicamente decisiones relevantes y aprobadas.

Cada decisión debe incluir:

- Decisión tomada.
- Justificación.
- Alternativas consideradas cuando corresponda.
- Consecuencias o limitaciones.

**Decisión aprobada por el usuario — 2026-10-13:**

- Equipos de hilos por range: cada miembro examina distintos dominios candidatos,
  prueba sus ocho simetrías y entrega su mejor candidato. El equipo reduce mínimos
  antes de publicar una única regla. Se descarta la interpretación de un range completo
  independiente por hilo como diseño principal. Ver [docs/Propuestas.md](docs/Propuestas.md).
- En empates se conserva el primer dominio/simetría del orden secuencial; el candidato
  incluye su índice global para hacer la reducción independiente del orden de finalización.
- La etapa 1 ofrece las operaciones reutilizables y ejecuta solo el modelo secuencial.
  Cuantum, eventos de stall y afinidad pertenecen a etapas futuras.

**Decisiones iniciales:**

- C++ como lenguaje de implementación.
- pthreads para concurrencia.
- Reutilización del núcleo de compresión fractal.
- Implementación incremental de los modelos.
- CMake para compilación y Python/Makefile para automatización.

## 8. Pendientes y validaciones

Registrar problemas conocidos, tareas bloqueadas y validaciones que requieren intervención manual.

**Pendientes:**

- Implementar equipos reales y sincronización en las etapas concurrentes autorizadas.
- Confirmar cuantums para Fine-Grained y eventos de stall para Coarse-Grained antes
  de sus respectivas implementaciones; no bloquean la etapa 1.
- La configuración secuencial rechaza varios equipos/hilos para evitar informar
  paralelismo inexistente. `10 hilos = 5 equipos × 2` describe el diseño futuro.


- Confirmar topología física y lógica de la CPU.
- Definir el tamaño de cuantum de Fine-Grained.
- Definir cómo se modelarán los stalls de Coarse-Grained.
- Establecer configuraciones equivalentes de comparación.
- Verificar los requisitos académicos adicionales de la especificación.

## 9. Historial de progreso

Registrar actualizaciones relevantes con el siguiente formato:

**Fecha — Etapa — Resumen**

- Cambios implementados.
- Pruebas ejecutadas y resultados.
- Tareas pendientes.
- Próximo paso.

Mantener este historial breve. Git conserva el historial detallado de modificaciones.


### 2026-10-13 — Etapa 1 — Arquitectura y punto de entrada completados

- Extraído el núcleo único a `src/` e `include/` y la E/S compartida a `app/`.
  `sequential/main.cpp` conserva el controlador base; ejemplos permanecen en `sequential/examples/`.
- `include/fractal/search.hpp` permite preparar dominios y ranges, buscar un intervalo
  de dominios y combinar mínimos con desempate determinista. Se conserva la aritmética
  original y el formato/orden de reglas FRC1/FRC2. `rules` se dimensiona por adelantado.
- `include/fractal/execution.hpp` define la configuración de modelos/equipos/hilos y
  ejecución común mono/RGB. Solo `sequential` (1×1) está implementado en esta etapa.
- CLI compatible con argumentos originales y opciones explícitas, con validación previa
  a E/S, ayuda y rechazo de modelos pendientes. Se actualizó la propuesta al diseño de
  equipos por range aprobado por el usuario; no quedan decisiones que bloqueen la etapa 1.
- Validación funcional en WSL2, GNU C++ 13.3.0, OpenCV 4.6.0 y Python3:

  | Construcción Release | Configuración | Resultado CTest |
  |---|---|---|
  | Raíz: `/tmp/fractal-stage1-release` | `BUILD_APP=ON` | 5/5 aprobadas |
  | Independiente: `/tmp/fractal-stage1-direct-fixed` | `-S sequential`, `BUILD_APP=ON` | 5/5 aprobadas |
  | Solo núcleo: `/tmp/fractal-stage1-core` | `BUILD_APP=OFF` | 3/3 aprobadas |

  Se usaron `cmake -S <fuente> -B <construcción> -DCMAKE_BUILD_TYPE=Release -DBUILD_APP=<ON/OFF>`,
  `cmake --build <construcción> -j2` y `ctest --test-dir <construcción> --output-on-failure`.
- Pruebas existentes conservadas: gris, RGB, FRC1/FRC2, convergencia e intercambio RGB/BGR.
  `execution_tests` verifica particiones 1/2/5/23 sobre 18 dominios, intervalos vacíos,
  reducción en orden inverso, empates en imágenes constantes y ejecución mono/RGB.
  `cli_tests` verifica compatibilidad posicional/opciones, ayuda y **26 casos inválidos**
  sin alterar la entrada ni una salida existente.
- Comparación contra el binario anterior (`/tmp/fractal-stage1-baseline/fractal`): **22 casos**
  de compresión/reconstrucción RGB y gris, imágenes 16×24 constantes/gradientes/patrones con
  `(range,stride)=(2,1),(4,3),(8,16)` y `color128.png` con `(8,8),(16,16)`, 80 iteraciones.
  Todos los FRC y PNG coinciden **byte por byte**. Artefactos temporales:
  `/tmp/fractal-stage1-equivalence-yx94efje/`.
- Revisados enlaces locales de documentación y `git diff --check`.
  Se corrigió una recursión en la construcción independiente de CMake antes de validar
  satisfactoriamente esa configuración. No se efectuaron benchmarks de hardware.
- README: seis tareas de etapa 1 completadas; etapas 2–8 pendientes.
  Próximo paso, solo bajo solicitud explícita: etapa 2 y definición de su cuantum cooperativo.

### 2026-10-14 — Renombre de la carpeta secuencial

- Renombrada `secuencial/` a `sequential/` mediante `git mv -k`; se omitieron las rutas
  ya ausentes por la extracción anterior. Git registra 10 renombres de archivos versionados,
  sin duplicar imágenes ni ejemplos. El punto de entrada aún no versionado también se trasladó.
- Actualizadas referencias en CMake, documentación y reglas; se conserva el alias CLI
  `--model secuencial`. El algoritmo y los estados del roadmap permanecen sin cambios.
- Configuración Release con `BUILD_APP=ON`, construcción `-j2` y CTest desde la raíz
  (`/tmp/fractal-rename-root-20261014`) y desde `sequential/`
  (`/tmp/fractal-rename-direct-20261014`): **5/5 pruebas aprobadas en ambas construcciones**.
  Enlaces locales y `git diff --check` correctos. Sin nuevos pendientes por este renombre.
