# Framework Experimental de Modelos Multithreading

Este README es la **única fuente de verdad del progreso, arquitectura y validaciones**.
[README_Cod.md](README_Cod.md) conserva los criterios originales. Las reglas permanentes
están en [AGENTS.md](AGENTS.md) y la guía académica de la versión base en
[sequential/README.md](sequential/README.md).

Alcance autorizado: **etapa 2**, con equipos de hilos por range como diseño aprobado.
Las etapas 3–8 continúan pendientes y requieren solicitud explícita.

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
- `app/`: controlador CLI, entrada/salida, mediciones y serialización FRC1/FRC2 compartidos.
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

**Puntos de entrada y ejecutables aprobados:** cada carpeta de modelo tiene su propio
`main.cpp` y `CMakeLists.txt`. Los main delegan argumentos y E/S en `app/cli.cpp` y fijan
su modelo; el núcleo continúa compartido en `fractal_codec`.

| Modelo | Punto de entrada | Ejecutable |
|---|---|---|
| Secuencial | `sequential/main.cpp` | `fractal_seq` |
| Fine-Grained | `fine-grained/main.cpp` | `fractal_fine` |
| Coarse-Grained (pendiente) | `coarse-grained/main.cpp` | `fractal_coarse` |
| CMP (pendiente) | `CMP/main.cpp` | `fractal_cmp` |
| SMT (pendiente) | `SMT/main.cpp` | `fractal_smt` |

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
Pruebas y equivalencia registradas en el historial.

### Etapa 2 — Fine-Grained

- [x] Implementar ejecución mediante pthreads.
- [x] Implementar planificación cooperativa Round-Robin.
- [x] Definir cuantums mínimos de trabajo.
- [x] Implementar sincronización entre trabajadores.
- [x] Verificar la alternancia de ejecución.
- [x] Validar resultados contra la versión secuencial.

**Aceptación cumplida:** pthreads con Round-Robin cooperativo, cuantums verificables y resultados idénticos a la base. La comprobación adicional con ThreadSanitizer queda pendiente por limitación del entorno.

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

### Backlog

- [ ] Se quiere crear versiones de cada modelo para descomprimir.
- [ ] 

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
mkdir -p sequential/examples/results
./build/fractal_seq --help
./build/fractal_fine --help
./build/fractal_seq compress sequential/examples/color128.png sequential/examples/results/color.frc --teams 1 --threads-per-team 1 --range 8 --stride 8
./build/fractal_seq decompress sequential/examples/results/color.frc sequential/examples/results/color.png --iterations 80
./build/fractal_seq compare sequential/examples/color128.png sequential/examples/results/color.png
```
Rebuild de un directirio/modo específico.
```
cmake --build build --target fractal_fine -j2
```

Fine-Grained, con 10 trabajadores organizados en 5 equipos de 2:

```bash
mkdir -p fine-grained/results
./build/fractal_fine compress sequential/examples/color128.png fine-grained/results/color.frc --teams 5 --threads-per-team 2 --quantum-domains 1
./build/fractal_seq decompress fine-grained/results/color.frc fine-grained/results/color.png --iterations 80
./build/fractal_seq compare sequential/examples/color128.png fine-grained/results/color.png
```

Cuantum por defecto: 1 dominio y sus ocho simetrías; se configura con `--quantum-domains N`.
Agregar `--trace-schedule fine-grained/results/schedule.csv` a `compress` o `compress-gray`
permite inspeccionar los turnos. Es una traza de software opcional, no un contador de hardware;
registrarla añade tiempo y memoria. Ver [docs/fine_grained.md](docs/fine_grained.md).

Se conservan los argumentos posicionales originales (`compress entrada salida 8 8` y
`decompress entrada salida 80`), el modo gris (`compress-gray`, `compare-gray`). Por compatibilidad, `--model` es opcional
y solo acepta el modelo del ejecutable (`sequential`/`secuencial` para `fractal_seq`,
`fine-grained` para `fractal_fine`). Los modelos cruzados se rechazan.
Las opciones no se pueden repetir ni duplicar con argumentos posicionales.

`--teams` y `--threads-per-team` describen la configuración común; para `sequential` deben valer 1.
Fine-Grained está disponible para compresión; Coarse-Grained, CMP y SMT continúan pendientes.
La reconstrucción se ejecuta mediante `fractal_seq decompress`; `fractal_fine` la rechaza.
No se aceptan opciones de ejecución en `compare` ni parámetros de compresión en `decompress`.

También se puede configurar desde una carpeta de modelo con `cmake -S sequential -B build`
o `cmake -S fine-grained -B build`. Ambos casos reutilizan las bibliotecas de la raíz.
Para construir un ejecutable específico, usar `cmake --build build --target fractal_seq`
o `cmake --build build --target fractal_fine`.
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

**Etapa 2 — Referencia indicada por el usuario:**

- Se adapta el modo cooperativo de RayTracing: Q=1 unidad de trabajo por defecto,
  configurable y rotación incondicional. En fractales la unidad es un dominio y sus ocho
  simetrías, con pthreads reales y turnos estrictos globales. No se implementa su modo
  paralelo cíclico porque no verifica alternancia cooperativa.
- Se conserva el diseño de equipos por range y se reducen los mínimos antes de publicar.
  Decodificación secuencial; las trazas registran turnos de software, no ciclos de hardware
  ni cambios de contexto del SO. Ver [docs/fine_grained.md](docs/fine_grained.md).

**Mejora autorizada — 2026-10-15:**

- Fine-Grained emplea una condición por trabajador y notifica únicamente al próximo turno.
  Solo al finalizar o cancelar se señalan todas las condiciones para reunir los hilos.
  Se mantiene el Round-Robin global, los cuantums y la reducción de mínimos por equipo.

**Decisiones iniciales:**

- C++ como lenguaje de implementación.
- pthreads para concurrencia.
- Reutilización del núcleo de compresión fractal.
- Implementación incremental de los modelos.
- CMake para compilación y Python/Makefile para automatización.

## 8. Pendientes y validaciones

Registrar problemas conocidos, tareas bloqueadas y validaciones que requieren intervención manual.

**Pendientes:**

- [ ] Repetir la comprobación adicional con ThreadSanitizer en un entorno compatible.
  Las dos ejecuciones locales fallaron antes de iniciar la prueba con
  `FATAL: ThreadSanitizer: unexpected memory mapping`; no se afirma validación con TSan.

- Implementar las etapas 3–8 únicamente cuando se soliciten explícitamente.
- Confirmar eventos de stall antes de implementar Coarse-Grained.
- La configuración secuencial rechaza varios equipos/hilos para evitar informar
  paralelismo inexistente. `10 hilos = 5 equipos × 2` ya está disponible para Fine-Grained.


- Confirmar topología física y lógica de la CPU.
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


### 2026-10-14 — Etapa 2 — Fine-Grained implementado y validado

- `fine-grained/encoder.cpp`: pthreads, compuerta de inicio, mutex/condición, Round-Robin
  global, Q=1 dominio configurable, reducción de mínimos por equipo y joins de todos los
  trabajadores. Equipos/ranges y miembros/dominios se reparten de manera contigua equilibrada.
- Interfaz común y CLI: `--model fine-grained`, `--quantum-domains` y `--trace-schedule`.
  Solo codificación gris/RGB; la reconstrucción usa el codec secuencial.
  Todos los archivos y carpetas nuevos tienen nombres en inglés.
- Release con OpenCV: `cmake -S . -B /tmp/fractal-stage2-release -DCMAKE_BUILD_TYPE=Release -DBUILD_APP=ON`,
  `cmake --build /tmp/fractal-stage2-release -j2` y
  `ctest --test-dir /tmp/fractal-stage2-release --output-on-failure`: **6/6 aprobadas**.
- Sin OpenCV (`BUILD_APP=OFF`, `/tmp/fractal-stage2-core`): **4/4 aprobadas**.
- `fine_grained_tests`: **48 configuraciones monocanal y un caso RGB**; reglas y
  reconstrucciones exactas, turnos globales reproducidos desde traza, cobertura completa
  sin duplicados, cuantums acotados, empates, barrera del equipo e intervalos/equipos vacíos.
  Configuraciones 1×1, 1×3, 5×2 y 7×4, Q=1/2/100; CTest impone timeout de 45 segundos.
- `cli_tests`: compatibilidad original y seis compresiones Fine-Grained (gris/RGB,
  5×2, Q=1/3/100), traza, reconstrucción y **31 rechazos** de parámetros/rutas inválidos.
- Contra el binario anterior `/tmp/fractal-stage2-baseline/fractal`, `color128.png`,
  range=8, stride=8 y 80 iteraciones: gris y RGB, 5×2 trabajadores, Q=1/4.
  **Cuatro compresiones FRC y reconstrucciones PNG idénticas byte por byte**.
  Artefactos: `/tmp/fractal-stage2-check-if9gnijo/`.
- Fallo de creación inducido limitando únicamente el subproceso de prueba a 128 MiB
  (`RLIMIT_AS`): se propagó `pthread_create: Resource temporarily unavailable` y terminó
  sin bloqueo dentro de un timeout de 15 segundos.
- ThreadSanitizer: ambas construcciones Debug compilaron (normal y sin PIE), pero ambas
  ejecuciones fallaron antes de los tests por `unexpected memory mapping`. Ver pendiente.
  No se reportan resultados de benchmarking ni se extrapolan medidas físicas desde WSL2.
- README: seis tareas de etapa 2 completadas; etapas 3–8 pendientes.
  Próximo paso, solo bajo solicitud explícita: etapa 3 y definición de eventos de stall.

### 2026-10-14 — Organización aprobada — Ejecutables separados por modelo

- `sequential/main.cpp` → `fractal_seq`; `fine-grained/main.cpp` → `fractal_fine`,
  cada uno con su CMake. Los puntos de entrada fijan su modelo y delegan en `app/cli.cpp`.
- El controlador antes ubicado en `sequential/main.cpp` se trasladó a `app/cli.cpp`.
  `fractal_app` comparte argumentos, E/S y mediciones; `fractal_codec` conserva el núcleo.
  No se duplicó el algoritmo ni se cambió la planificación Fine-Grained.
- Ya no se construye el ejecutable único `fractal`. `--model` es opcional por compatibilidad
  y rechaza modelos que no correspondan al ejecutable. Reconstrucción con `fractal_seq`.
- Esta estructura queda como regla permanente para Coarse-Grained, CMP y SMT en sus etapas;
  sus ejecutables y puntos de entrada todavía no se implementan.
- Release con OpenCV desde la raíz (`/tmp/fractal-split-root`) y desde `fine-grained/`
  (`/tmp/fractal-split-direct`): construcción `-j2` y CTest, **6/6 aprobadas en cada caso**.
  Sin OpenCV (`/tmp/fractal-split-core`): **4/4 aprobadas**.
- Pruebas CLI adaptadas a los dos binarios: modelo predeterminado, ayuda, rechazo de
  modelos cruzados, trazas, igualdad de reglas y reconstrucción de archivos Fine-Grained
  con el ejecutable secuencial. Enlaces locales y `git diff --check` revisados.
- Contra el ejecutable único anterior, `color128.png`, range=8, stride=8, 80 iteraciones,
  secuencial y Fine-Grained 5×2: cuatro casos RGB/gris con FRC y PNG idénticos byte por byte.
  Artefactos: `/tmp/fractal-split-check-frop1mgw/`.
- Actualizados comandos actuales y reglas; se conservan los registros históricos.
  Etapas 1 y 2 completas; etapas 3–8 y la comprobación adicional con TSan siguen pendientes.

### 2026-10-15 — Fine-Grained — Despertar únicamente al próximo trabajador

- Completado: condición propia por trabajador y `pthread_cond_signal` dirigido al turno
  seleccionado, tanto al iniciar como al ceder. Si el turno permanece en el mismo hilo,
  no se envía señal. Se elimina `pthread_cond_broadcast` del planificador.
- Finalización y errores: se señalan todas las condiciones para permitir los joins de
  trabajadores activos, inactivos y todavía no bloqueados. Inicialización parcial y
  destrucción de condiciones con liberación de los recursos correspondientes.
- Se conservan el Round-Robin global, la búsqueda, los cuantums y los mínimos por equipo.
  La prueba incorpora 6×4 trabajadores: 60 configuraciones mono y un caso RGB, incluidos
  un solo trabajador, dominios vacíos y equipos sin ranges.
- Validación: `cmake -S . -B /tmp/fractal-wakeup-after-20261015 -DCMAKE_BUILD_TYPE=Release -DBUILD_APP=ON`,
  `cmake --build /tmp/fractal-wakeup-after-20261015 -j2` y
  `ctest --test-dir /tmp/fractal-wakeup-after-20261015 --output-on-failure`: **6/6 aprobadas**.
- Diagnóstico puntual local en WSL2, una ejecución por variante, sin traza: `color128.png`,
  range=8, stride=8, 6 equipos × 4 hilos, Q=2. Tiempo de codificación sin E/S:

  | Variante | Segundos |
  |---|---:|
  | Fine-Grained anterior (broadcast) | 35.7465 |
  | Fine-Grained con señal dirigida | 5.12334 |
  | Secuencial | 0.593602 |

  Los tres FRC coinciden byte por byte (13080 bytes). Es una comprobación de la mejora,
  no el benchmark estadístico de la etapa 7 ni una medición de multihilo por hardware.
  Antes: `/tmp/fractal-wakeup-before-20261015`; después: `/tmp/fractal-wakeup-after-20261015`.
- Traza antes/después idéntica en una imagen RGB 16×24, range=4, stride=3, 6×4 y Q=2.
  Fallo de `pthread_create` inducido con `RLIMIT_AS=128 MiB` en el subproceso de prueba:
  error propagado y proceso finalizado sin bloqueo dentro de un timeout de 15 segundos.
  Artefactos: `/tmp/fractal-targeted-wakeup-rspz92do/`.
- README y documentación actualizados. Pendientes: etapas 3–8 y comprobación adicional
  con ThreadSanitizer ya registrada; no se implementó ninguna otra etapa.
