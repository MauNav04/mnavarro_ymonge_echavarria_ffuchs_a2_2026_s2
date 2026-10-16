# Framework Experimental de Modelos Multithreading

Este README es la **única fuente de verdad del progreso, arquitectura y validaciones**.
[README_Cod.md](README_Cod.md) conserva los criterios originales. Las reglas permanentes
están en [AGENTS.md](AGENTS.md) y la guía académica de la versión base en
[sequential/README.md](sequential/README.md).

Alcance autorizado: **etapa 7 — benchmarking y métricas**, con equipos de hilos por range como diseño aprobado.
La etapa 8 continúa pendiente y requiere solicitud explícita.

## 1. Descripción del proyecto

Proyecto de Arquitectura de Computadores II (CE4302) del Instituto Tecnológico de Costa Rica.

El objetivo es implementar y evaluar experimentalmente cuatro modelos de ejecución concurrente aplicados a la compresión fractal de imágenes:

- Fine-Grained Multithreading
- Coarse-Grained Multithreading
- Chip Multiprocessing (CMP)
- Simultaneous Multithreading (SMT)

El proyecto parte de una implementación secuencial existente, utilizada como referencia para medir el rendimiento de las versiones concurrentes.

**Validación funcional confirmada por el usuario — 2026-10-16:** todos los modelos
funcionan en la computadora de evaluación, incluida la prueba SMT. Esta confirmación
se registra como prueba realizada por el usuario; no como una ejecución del asistente.
CMP y SMT conservan únicamente el destino Ubuntu/AMD Ryzen. Los registros detallados,
la escalabilidad y las mediciones estadísticas/Perf se documentan por separado cuando
exista evidencia; las etapas 6–8 no se adelantan con esta limpieza.

## 2. Tecnologías y entorno

- **Lenguaje principal:** C++
- **Concurrencia:** POSIX Threads (pthreads)
- **Compilación:** CMake
- **Automatización:** Makefile y Python
- **Profiling:** Linux Perf
- **Sistema operativo:** Linux
- **Hardware local:** Intel Core i5-1335U bajo WSL2; el kernel reporta 12 CPU lógicas/6 núcleos.
- **Destino de evaluación confirmado por el usuario:** Ubuntu, AMD Ryzen (modelo exacto
  pendiente; Ryzen 7 aún no confirmado), 8 núcleos físicos/16 hilos lógicos con SMT activo.
  Con SMT inactivo se admiten 8 CPU online. CMP, SMT y Linux Perf se preparan para este destino.

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
| Coarse-Grained | `coarse-grained/main.cpp` | `fractal_coarse` |
| CMP de evaluación | `CMP/main.cpp` | `fractal_cmp` |
| SMT de evaluación | `SMT/main.cpp` | `fractal_smt` |

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

- [x] Implementar ejecución mediante pthreads.
- [x] Definir eventos de bloqueo o estancamiento.
- [x] Implementar la política de cambio ante estos eventos.
- [x] Evitar cambios periódicos incondicionales de Fine-Grained.
- [x] Validar resultados contra la versión secuencial.

**Aceptación cumplida:** ráfagas con stall por estancamiento, reanudación sin perder candidatos y trazas que verifican la política; resultados idénticos a la base. Ver [docs/coarse_grained.md](docs/coarse_grained.md).

### Etapa 4 — CMP

- [x] Implementar distribución paralela de trabajo.
- [x] Ejecutar trabajadores sobre núcleos físicos.
- [x] Implementar afinidad de CPU.
- [x] Evitar condiciones de carrera.
- [ ] Verificar escalabilidad con diferentes cantidades de trabajadores.

**Estado:** única versión `fractal_cmp` para Ubuntu/AMD Ryzen. Funcionalidad en la
máquina de evaluación confirmada por el usuario el 2026-10-16. Se eliminó la variante
local y su selección de perfil; se mantiene el motor compartido con SMT.

**Aceptación funcional confirmada por el usuario:** ejecución correcta en el destino.
La escalabilidad con diferentes cantidades de trabajadores sigue pendiente de documentar.

### Etapa 5 — SMT

- [x] Detectar topología de hilos lógicos y núcleos físicos.
- [x] Implementar configuración de afinidad para SMT.
- [x] Preparar ejecución con SMT habilitado y deshabilitado.
- [ ] Registrar y comparar ambas configuraciones.
- [x] Validar resultados contra la versión secuencial.

**Estado:** prueba SMT y funcionamiento de todos los modelos en la máquina de evaluación
confirmados por el usuario el 2026-10-16. Se registra su validación funcional satisfactoria;
no se inventan resultados numéricos ni registros que no se hayan compartido.
Ver [docs/smt.md](docs/smt.md) para conservar o repetir las corridas.

**Funcionalidad validada por el usuario.** El registro reproducible y la comparación
experimental ON/OFF permanecen pendientes de incorporar; la confirmación funcional no
sustituye sus artefactos ni las mediciones verificadas del criterio experimental.

### Etapa 6 — Integración y pruebas

- [x] Configurar CMake para todos los ejecutables.
- [x] Integrar Makefile para automatización.
- [x] Implementar `test_models.py`.
- [ ] Validar resultados para diferentes cantidades de trabajadores.
- [ ] Comprobar errores, sincronización y ausencia de bloqueos.

**Estado:** automatización preparada con Makefile raíz y `scripts/Makefile`, CMake y
`scripts/test_models.py`. La nueva matriz completa con CMP/SMT debe ejecutarse en el
Ryzen; las últimas dos tareas mantienen pendiente esa validación. Los modelos ya fueron
confirmados funcionales por el usuario antes de integrar esta suite.
Ver [docs/testing.md](docs/testing.md) para el protocolo, comandos y artefactos.

**Aceptación pendiente de la nueva suite en destino:** todos los modelos compilan,
ejecutan y superan las pruebas integradas de etapa 6.

### Etapa 7 — Benchmarking y métricas

- [x] Implementar `benchmark.py`.
- [ ] Ejecutar al menos 200 repeticiones por configuración experimental.
- [x] Guardar resultados individuales en CSV.
- [x] Calcular tiempo, speedup, eficiencia y escalabilidad.
- [x] Calcular media, desviación estándar e intervalos de confianza del 95 %.
- [x] Implementar `metrics.py` para generar gráficas automáticamente.
- [ ] Generar boxplots y analizar convergencia.

**Estado:** recolección, estadísticas y gráficas implementadas; pruebas numéricas,
figuras sintéticas y campañas diagnósticas locales validadas. Las 200 repeticiones físicas
y el análisis experimental de convergencia en el Ryzen continúan pendientes.
Cobertura y equivalencia de métricas con RayTracing/gray-scott, comandos y metodología
en [docs/benchmarking.md](docs/benchmarking.md).

**Aceptación física pendiente:** campañas reproducibles en el Ryzen y revisión de sus
CSV/gráficas. No se presentan los diagnósticos locales como resultados de evaluación.

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

La interfaz de automatización recomendada para etapa 6 es:

```bash
make help
make all
make test_local                         # CTest y tres modelos portables; permite WSL
make topology SMT_STATE=on              # En Ubuntu/AMD Ryzen de evaluación
make test QUICK=1 SMT_STATE=on          # Comprobación breve de todos los modelos
make test SMT_STATE=on                  # Matriz completa en el destino
# Tras desactivar SMT realmente mediante intervención del usuario:
make test SMT_STATE=off
```

La suite completa falla si falta el hardware/estado requerido; no omite CMP/SMT
silenciosamente. `make -C scripts <objetivo>` ofrece los mismos comandos.
`run_seq`, `run_fine`, `run_coarse`, `run_cmp`, `run_smt_on` y `run_smt_off`
comprimen y reconstruyen una imagen demostrativa. Los `verify_*` comparan contra
secuencial; `record_smt_on/off` y `compare_smt` conservan la comparación ON/OFF.
Guía detallada de ejecución, parámetros y resultados: [docs/testing.md](docs/testing.md).
Para realizar las 200 repeticiones por modelo/configuración y generar todas las
figuras de rendimiento, seguir la [receta completa de etapa 7](docs/benchmarking.md#receta-completa-200-corridas-y-todas-las-figuras-de-etapa-7):
preparación, campañas fijas y escalabilidad ON/OFF, y un único `make plot` para combinarlo.
Las figuras Perf de RayTracing requieren todavía la etapa 8.

Para etapa 7, después de instalar `scripts/requirements.txt` en un entorno virtual:

```bash
make benchmark_fast BUILD_DIR=build-evaluation FAST_DIR=metrics/check-on
make plot BENCH_DIR=metrics/check-on
make benchmark_full BUILD_DIR=build-evaluation SMT_STATE=on BENCH_DIR=metrics/benchmark-on
make scalability BUILD_DIR=build-evaluation SMT_STATE=on SCALING_DIR=metrics/scaling-on
```

Repetir las campañas en OFF tras la intervención real de SMT. Los objetivos formales
requieren Release, hardware de evaluación y al menos 200 corridas; no cambian SMT.
Gráficas ON/OFF, parámetros y rutas en [docs/benchmarking.md](docs/benchmarking.md).
Las figuras de contadores Perf corresponden a etapa 8 y no se inventan en estos CSV.

Los comandos CMake directos continúan disponibles:

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

Coarse-Grained, con 5 equipos de 2 trabajadores:

```bash
cmake --build build --target fractal_coarse -j2
mkdir -p coarse-grained/results
./build/fractal_coarse compress sequential/examples/color128.png coarse-grained/results/color.frc --teams 5 --threads-per-team 2
./build/fractal_seq decompress coarse-grained/results/color.frc coarse-grained/results/color.png --iterations 80
./build/fractal_seq compare sequential/examples/color128.png coarse-grained/results/color.png
```

Agregar `--trace-schedule coarse-grained/results/schedule.csv` registra una fila por ráfaga,
con la causa `stall` o `domain_complete`, la racha sin mejora y su umbral. Se rechaza
`--quantum-domains`: las cesiones dependen de los datos y de la finalización del intervalo.
Ver [docs/coarse_grained.md](docs/coarse_grained.md) para la definición y ejemplos del evento.

CMP de evaluación, **en Ubuntu nativo con AMD Ryzen 8/16**:

```bash
cmake --build build --target fractal_cmp -j2
./build/fractal_cmp --list-cpus
mkdir -p CMP/results
./build/fractal_cmp compress sequential/examples/color128.png CMP/results/color.frc --teams 4 --threads-per-team 2 --report-affinity CMP/results/affinity.csv
./build/fractal_seq decompress CMP/results/color.frc CMP/results/color.png 80
./build/fractal_seq compare sequential/examples/color128.png CMP/results/color.png
```

El perfil de evaluación rechaza WSL/virtualización detectada, sistemas distintos de Ubuntu,
CPU de otra familia y topologías fuera del contrato 8 núcleos/8 o 16 lógicas.
Se puede configurar desde `CMP/`. No existe una variante local.
`--report-affinity` registra CPU solicitada y observada antes/después de cada trabajador.
El total `teams * threads_per_team` no puede exceder los núcleos permitidos; no se rellena
con hermanos SMT. CMP no acepta cuantums ni trazas de turnos cooperativos.
Ver [docs/cmp.md](docs/cmp.md).

SMT de evaluación, en la misma máquina Ubuntu/AMD Ryzen de CMP:

```bash
cmake --build build --target fractal_smt -j2
./build/fractal_smt --help
./build/fractal_smt --list-cpus --smt-state on
python3 SMT/verify_smt.py run --state on --input sequential/examples/color128.png --output-dir SMT/results/on
```

Después de que el usuario desactive SMT realmente en la computadora de evaluación,
conservando los mismos binarios y entrada:

```bash
./build/fractal_smt --list-cpus --smt-state off
ctest --test-dir build -R smt_hardware_tests --output-on-failure
python3 SMT/verify_smt.py run --state off --input sequential/examples/color128.png --output-dir SMT/results/off
python3 SMT/verify_smt.py compare --on SMT/results/on --off SMT/results/off --output SMT/results/comparison.json
```

Por defecto son 4 equipos × 4 trabajadores en ambos estados. ON usa 16 CPU lógicas;
OFF conserva 16 pthreads sobre 8 núcleos, con sobresuscripción por software.
`--smt-state` comprueba el estado esperado y no lo cambia. El programa rechaza estados
desconocidos/incompatibles y entornos reconocidos como virtualizados; no ofrece una
simulación local que se pueda confundir con SMT físico. `--report-affinity salida.csv`
registra CPU efectiva y estado verificado. La herramienta guarda diagnósticos, comandos,
tiempos individuales y hashes; exige resultados FRC/PNG idénticos a secuencial.
Guía completa, comandos manuales y protocolo de ambas corridas en [docs/smt.md](docs/smt.md).
El usuario confirmó una prueba SMT satisfactoria en el Ryzen; conservar o repetir ambos
registros con estos comandos para documentar la comparación experimental ON/OFF.
También se puede configurar con `cmake -S SMT -B build-smt`.

Se conservan los argumentos posicionales originales (`compress entrada salida 8 8` y
`decompress entrada salida 80`), el modo gris (`compress-gray`, `compare-gray`). Por compatibilidad, `--model` es opcional
y solo acepta el modelo del ejecutable (`sequential`/`secuencial` para `fractal_seq`,
`fine-grained` para `fractal_fine`, `coarse-grained` para `fractal_coarse`, `CMP` para `fractal_cmp` y `SMT` para `fractal_smt`). Los modelos cruzados se rechazan.
Las opciones no se pueden repetir ni duplicar con argumentos posicionales.

`--teams` y `--threads-per-team` describen la configuración común; para `sequential` deben valer 1.
Fine-Grained, Coarse-Grained, CMP y SMT están disponibles para compresión; SMT requiere el hardware de evaluación.
La reconstrucción se ejecuta mediante `fractal_seq decompress`; los ejecutables paralelos la rechazan.
No se aceptan opciones de ejecución en `compare` ni parámetros de compresión en `decompress`.

También se puede configurar desde una carpeta de modelo con `cmake -S sequential -B build`
`cmake -S fine-grained -B build` o `cmake -S coarse-grained -B build`. Los tres casos reutilizan las bibliotecas de la raíz.
Para construir un ejecutable específico, usar `cmake --build build --target fractal_seq`
`cmake --build build --target fractal_fine` o `cmake --build build --target fractal_coarse`.
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

**Etapa 3 — Evento de stall aprobado por el usuario — 2026-10-15:**

- Stall: racha de dominios consecutivos que no mejoran estrictamente el menor error local
  del hilo. Con D dominios asignados para ese range, se dispara en `floor(2*D/3)+1`.
  Se cuenta después de evaluar las ocho simetrías; los empates no mejoran el mínimo.
- El mínimo y la posición se conservan; la racha se reinicia al mejorar, al ceder por stall
  o al comenzar otro range. Se reanuda sin descartar candidatos. Si termina el intervalo,
  se registra finalización en lugar de stall aunque se alcance el umbral.
- Es estancamiento de búsqueda simulado; no representa bloqueo físico de CPU/caché ni
  implica que el mínimo actual sea malo. Puede ocurrir incluso con un mínimo perfecto.
- Se reutilizan equipos y sincronización en `src/cooperative_encoder.cpp`. Fine-Grained
  mantiene cesión periódica por cuantum; Coarse-Grained cede solo por evento o finalización.
  Las condiciones siguen siendo individuales y únicamente se notifica al siguiente turno.

**CMP — Única versión de evaluación — 2026-10-16:**

- Por solicitud del usuario, se elimina `CMP/local/` y el perfil local tras confirmar
  funcionalidad en la computadora de evaluación. `fractal_cmp` exige Ubuntu/AMD Ryzen
  8/16, selecciona un representante por núcleo y verifica afinidad efectiva.
- CMP y SMT comparten `src/parallel_encoder.cpp`, con equipos por range, barreras
  cancelables y mínimos deterministas. Se conserva el algoritmo matemático.
- CSV y diagnósticos conservan la etiqueta `target` para mantener compatibilidad con los
  registros SMT; no corresponde a un perfil seleccionable.
- Linux Perf continúa destinado a la misma máquina Ubuntu y pendiente de su etapa.

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

- Implementar la etapa 8 únicamente cuando se solicite explícitamente.
- La configuración secuencial rechaza varios equipos/hilos para evitar informar
  paralelismo inexistente. `10 hilos = 5 equipos × 2` ya está disponible para Fine-Grained y Coarse-Grained.


- Ejecutar la nueva suite de etapa 6 con `make test SMT_STATE=on` y, tras la intervención
  de SMT, `make test SMT_STATE=off`; conservar los dos `report.json`.
- Ejecutar las campañas de etapa 7 en ON/OFF con 200 repeticiones por configuración,
  conservar raw.csv y revisar escalabilidad/convergencia; diagnóstico local no equivale a medición física.
- Documentar la escalabilidad de CMP con diferentes cantidades de trabajadores en Ubuntu/AMD Ryzen.
- Registrar el modelo Ryzen exacto y la versión de Ubuntu en esa máquina antes de medir.
- Incorporar los registros ON/OFF del Ryzen mediante `SMT/verify_smt.py` y su comparación.
  La prueba funcional satisfactoria ya fue confirmada por el usuario; no se han adjuntado
  aquí los artefactos experimentales detallados.
- Confirmar el modelo exacto de Ryzen. No ejecutar aún benchmarks estadísticos ni Perf.
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


### 2026-10-15 — Etapa 3 — Coarse-Grained completado

- Nuevo modelo `coarse-grained/` con main pequeño, CMake y `fractal_coarse`, reutilizando
  CLI/E/S y la búsqueda matemática. Cinco tareas de etapa 3 implementadas y validadas;
  etapas 4–8 y reconstrucción concurrente del backlog continúan pendientes.
- Política aprobada: racha estrictamente mayor a 2/3 de dominios locales sin mejorar;
  ráfagas reanudables, sin omisiones ni cesiones periódicas. Traza con motivo y contadores.
  Los nombres de todos los nuevos archivos y carpetas están en inglés.
- Mecanismo de pthreads/equipos extraído a `src/cooperative_encoder.cpp` para compartir
  inicialización, condiciones individuales, espera por mínimos, cancelación y joins.
- Validación Release, GNU C++ 13.3.0 y OpenCV 4.6.0 en WSL2:

  | Construcción | Resultado |
  |---|---|
  | `/tmp/fractal-stage3-release`, `BUILD_APP=ON` | 7/7 pruebas CTest aprobadas |
  | `/tmp/fractal-stage3-core`, `BUILD_APP=OFF` | 5/5 pruebas CTest aprobadas |
  | `/tmp/fractal-stage3-direct`, fuente `coarse-grained/` | Target `fractal_coarse` compilado; ayuda correcta |

  Comandos: `cmake -S <fuente> -B <build> -DCMAKE_BUILD_TYPE=Release -DBUILD_APP=<ON/OFF>`,
  `cmake --build <build> -j2`, `ctest --test-dir <build> --output-on-failure`.
  Para la configuración directa se usó `cmake --build <build> --target fractal_coarse -j2`.
- `coarse_grained_tests`: 30 casos monocanal más RGB, con 1×1, 1×3, 5×2, 7×4 y 1×23,
  constantes y patrones, tres geometrías de búsqueda y asignaciones vacías/desiguales.
  Verificó 1393 stalls y 2700 reinicios por mejora, umbral estricto, primera oportunidad
  de cesión, reanudación, cobertura sin duplicados, espera del equipo y reglas exactas.
  Los intervalos pequeños terminan sin stalls artificiales. Timeout CTest de 45 segundos.
- CLI: FRC gris/RGB equivalentes a la base, trazas con motivos válidos, parámetros de
  cuantum rechazados en Coarse-Grained, protección de entrada/salida y modelo fijo.
- `color128.png`, range=8, stride=8, RGB y gris, equipos 1×1/5×2/7×4:
  seis FRC y sus reconstrucciones de 80 iteraciones idénticos byte por byte al binario
  secuencial previo `/tmp/fractal-stage3-baseline/fractal_seq`.
  Fine-Grained conserva FRC y CSV de turnos idénticos antes/después de la extracción.
  Artefactos: `/tmp/fractal-stage3-compare-nz3zembr/`.
- Fallo de `pthread_create` inducido con límite de 128 MiB solo en el subproceso:
  error propagado y salida controlada sin bloqueo (timeout de 15 segundos).
  Enlaces de documentación y `git diff --check` verificados.
- ThreadSanitizer continúa pendiente por su limitación de entorno ya registrada; no se
  repitió ni se presenta como aprobado. No se ejecutaron benchmarks de hardware.
- Próximo paso, solo bajo solicitud explícita: etapa 4 (CMP), previa verificación de
  topología física y afinidad apropiada para el hardware disponible.


### 2026-10-15 — Etapa 4 — Dos variantes CMP implementadas; validación física pendiente

- Entradas `CMP/main.cpp` y `CMP/local/main.cpp`, ejecutables `fractal_cmp` y
  `fractal_cmp_local`, CMake por variante y motor único en `CMP/encoder.cpp`.
  `src/cpu_topology.cpp` detecta CPU online, socket/core, máscara permitida, entorno,
  distribución y familia del procesador. El perfil de evaluación aplica Ubuntu/AMD Ryzen/8-16.
- Equipos paralelos con barreras cancelables, resultados y candidatos por índice exclusivo;
  afinidad fijada/verificada en cada pthread antes/después. Un fallo cancela todas las
  barreras y reúne los hilos creados. No hay mutex global durante la búsqueda.
- Release con OpenCV (`/tmp/fractal-stage4-release`): **8/8 pruebas CTest aprobadas**.
  Sin OpenCV (`/tmp/fractal-stage4-core`): **6/6 aprobadas**.
  Configuración desde `CMP/` (`/tmp/fractal-stage4-direct`): ambos targets compilan y el
  diagnóstico local identifica WSL/Ubuntu, i5-1335U, 12 CPU online y 6 núcleos permitidos.
- Comandos: `cmake -S <fuente> -B <build> -DCMAKE_BUILD_TYPE=Release -DBUILD_APP=<ON/OFF>`,
  `cmake --build <build> -j2` y `ctest --test-dir <build> --output-on-failure`.
  En la configuración directa se construyeron los targets `fractal_cmp` y `fractal_cmp_local`.
- `cmp_tests`: **54 casos monocanal más RGB y equipos vacíos**, hasta seis trabajadores,
  reglas/reconstrucciones exactas y afinidad en núcleos distintos según el kernel.
  Con `taskset -c 1`, nueve casos más RGB/equipos vacíos confirman respeto de la máscara
  restringida, incluso si el representante permitido es un hermano lógico impar.
- Fixtures verifican selección de representantes, varios sockets, 8/16, 8/8 con SMT
  inactivo, máscaras parciales y rechazo de exceso de trabajadores o perfil incorrecto.
  Son pruebas del contrato, **no ejecuciones físicas del perfil target**. CLI verifica
  reportes de afinidad, modelo fijo y rechazo del perfil de evaluación en WSL.
- `color128.png`, range=8, stride=8, gris/RGB y equipos 1×1, 2×1, 1×2, 2×2 y 3×2:
  **diez FRC y PNG idénticos byte por byte** a `/tmp/fractal-stage4-baseline/fractal_seq`;
  cada reporte verifica CPU observada antes/después igual a la solicitada.
  Artefactos: `/tmp/fractal-stage4-check-v6d_eejp/`.
- Creación parcial de pthreads probada con límite de memoria de 48 MiB solo para el
  subproceso: error propagado y salida sin bloqueo (timeout de 15 segundos).
- Completadas distribución, implementación de afinidad y sincronización/memoria compartida.
  Se mantienen pendientes los dos criterios físicos de la etapa 4; no se ejecutaron
  benchmarks principales en WSL ni se afirma escalabilidad física por estos tests.
  ThreadSanitizer conserva su pendiente anterior. No se implementaron etapas 5–8.

### 2026-10-16 — Aclaración documental de CMP frente a SMT

- `docs/cmp.md` aclara que CMP selecciona un único hilo lógico por núcleo reportado,
  sin activar/desactivar SMT ni implementar la etapa 5. Incluye la salida WSL aportada
  por el usuario: 12 CPU lógicas, 6 núcleos reportados y 6 representantes permitidos.
- Se distingue disponibilidad de ejecución: 2 equipos de 2 usan cuatro representantes;
  la afinidad aplicada se comprueba con `--report-affinity`. La salida WSL no certifica
  núcleos físicos del anfitrión ni su estado SMT.
- Validación: revisión de la documentación, enlaces locales y `git diff --check`.
  Sin cambios de código ni nuevos checkboxes completados; se conservan pendientes físicos.


### 2026-10-16 — Etapa 5 — SMT implementado; comparación física pendiente

- `SMT/main.cpp`, CMake y `fractal_smt`, con 4×4 trabajadores por defecto. Adaptado el
  reparto ON/OFF de RayTracing al diseño fractal aprobado de equipos por range.
- Detección de todos los lógicos permitidos y estado `smt/control` + `smt/active`.
  ON usa lógicos distintos, OFF sobresuscribe núcleos; sin numeración fija de hermanos,
  sin alterar el estado del sistema y sin aceptar pinning como sustituto de SMT OFF real.
- Motor concurrente de CMP trasladado a `src/parallel_encoder.cpp` y reutilizado por
  ambos modelos. CMP conserva su política. Afinidad efectiva comprobada por trabajador;
  en SMT se vuelve a verificar topología/estado al reunir los pthreads.
- `SMT/verify_smt.py`: protocolo funcional de corrida/registro y comparación ON/OFF,
  igualdad FRC/PNG contra secuencial, diagnósticos y comandos con tiempos individuales,
  hashes de entrada/binarios/máquina/artefactos. No implementa benchmarks ni Perf.
- Validación local WSL2, GNU C++ 13.3.0, OpenCV 4.6.0:

  | Construcción | Resultado |
  |---|---|
  | `/tmp/fractal-stage5-release`, `BUILD_APP=ON` | 10 aprobadas, 1 omitida de 11 CTest |
  | `/tmp/fractal-stage5-core`, `BUILD_APP=OFF` | 7 aprobadas, 1 omitida de 8 CTest |
  | `/tmp/fractal-stage5-direct`, fuente `SMT/` | Target `fractal_smt` compilado y ayuda correcta |

  Comandos: `cmake -S <fuente> -B <build> -DCMAKE_BUILD_TYPE=Release -DBUILD_APP=<ON/OFF>`,
  `cmake --build <build> -j2`, `ctest --test-dir <build> --output-on-failure`.
  En la construcción directa se usó `--target fractal_smt`.
- `smt_tests`: contratos ON/OFF con fixtures, IDs dispersos, máscaras limitadas, estados
  desconocidos, trabajadores fuera de presupuesto y hardware/sistema incorrectos.
  `smt_record_tests`: comparación de registros sintéticos y rechazo de incompatibilidades
  de máquina/entrada/binario/configuración/estado y de artefactos alterados.
  CLI: ayuda, opciones incompatibles y preservación de archivos al rechazar el destino.
- `smt_hardware_tests` se omitió correctamente por falta del destino: no es una prueba física
  aprobada. Preparada para comparar 15 casos RGB con secuencial en cada estado del Ryzen.
  Las regresiones existentes de secuencial, Fine-Grained, Coarse-Grained y CMP aprobaron.
- Comprobación adicional con `color128.png`, range=8, stride=8, CMP local 2×2:
  dos FRC y sus reconstrucciones RGB/gris idénticos al secuencial anterior de
  `/tmp/fractal-stage5-baseline`. Artefactos: `/tmp/fractal-stage5-equivalence-f8dcl5t9/`.
  El registrador SMT rechazó WSL antes de crear el directorio de resultados.
  `git diff --check` y enlaces locales de la documentación revisados.
- Tres tareas de preparación de etapa 5 marcadas; registro/comparación física y validación
  SMT contra secuencial en el destino permanecen pendientes. No se ejecutaron experimentos
  físicos, no se cambió SMT y no se implementaron las etapas 6–8.
- Siguiente paso: llevar esta misma construcción al Ryzen, seguir `docs/smt.md` y conservar
  las dos corridas reales antes de declarar completa la etapa 5.


### 2026-10-16 — Validación de usuario y retiro de CMP local

- El usuario confirma prueba SMT satisfactoria en la computadora de evaluación y
  funcionalidad de todos los modelos. Registrada como validación del usuario, sin
  inventar tiempos, métricas o archivos experimentales.
- Eliminados `CMP/local/`, `fractal_cmp_local` y la selección de perfiles de la API.
  Solo permanece `fractal_cmp` para Ubuntu/AMD Ryzen; algoritmo y equipos conservados.
- Resultados anteriores conservados en `CMP/results/previous_local/` (artefactos locales
  históricos, no resultados de evaluación). Retirados binario y construcción local obsoletos.
- Pruebas CMP separadas en contratos de topología y ejecución física. Las pruebas
  físicas CMP/SMT se omiten fuera del destino, sin añadir una vía local alternativa.
- Validación de esta limpieza: configuración Release `BUILD_APP=ON`,
  `cmake --build build -j2` y `ctest --test-dir build --output-on-failure`:
  **10 aprobadas, 2 físicas omitidas de 12**. El árbol `build/` quedó actualizado
  sin el ejecutable anterior. No se repitió aquí la prueba física del usuario.
- Sin OpenCV (`/tmp/fractal-cmp-cleanup-core`, `BUILD_APP=OFF`): **7 aprobadas y
  2 físicas omitidas de 9**. Construcción desde `CMP/`
  (`/tmp/fractal-cmp-cleanup-direct`): target `fractal_cmp` y ayuda correctos.
  Enlaces locales, conservación de resultados anteriores y `git diff --check` verificados.
- Los apartados históricos anteriores describen versiones y pendientes de su fecha;
  los comandos vigentes y el estado actual están en las secciones 4, 6 y 8.
- Pendientes: incorporar registros ON/OFF, documentar escalabilidad y abordar etapas
  6–8 únicamente bajo solicitud explícita.


### 2026-10-16 — Etapa 6 — Automatización preparada; matriz Ryzen pendiente

- Makefile raíz y `scripts/Makefile` con interfaz `all`, modelos individuales,
  `run_*`, `verify_*`, `test`, `test_unit`, `test_local` y registro/comparación SMT.
  Revisadas las referencias de RayTracing y gray-scott; CMake continúa definiendo la
  compilación única. Ningún objetivo cambia SMT ni implementa benchmarking/Perf.
- `scripts/test_models.py`: 176 casos completos o 18 rápidos para cinco modelos,
  RGB/gris, constantes/patrones, geometrías y equipos variados, FRC/PNG exactos,
  afinidad, errores, protección de archivos, timeouts y registros JSON por corrida.
  Un destino incompatible produce fallo y registro parcial, nunca una validación omitida.
- CTest incorpora integración portátil, cinco pruebas del orquestador (incluidos
  contratos sintéticos SMT ON/OFF) y fallos reales de `pthread_create` limitando solo
  la memoria del subproceso. Se verificó salida controlada de Fine-Grained/Coarse-Grained.
- Validación WSL2 con GNU C++ 13.3.0, OpenCV 4.6.0 y Python3:
  `make -C scripts test_local BUILD_DIR=/tmp/fractal-stage6-release JOBS=2`: **13/13**.
  `ctest --test-dir /tmp/fractal-stage6-release --output-on-failure`: **13 aprobadas,
  2 físicas omitidas de 15**. Los cinco ejecutables compilan.
- Matriz completa de secuencial/Fine-Grained/Coarse-Grained: **88 casos** FRC/PNG
  exactos, sin fallos ni timeouts. Registro:
  `/tmp/fractal-stage6-matrix/run-20261016-123832-6ead4e0a/report.json`.
- Make raíz: `run_seq`, `run_fine` y `run_coarse` con imagen pequeña 16×24,
  range=4, stride=3 y equipos 2×2; FRC/PNG idénticos. `verify_all QUICK=1` rechazó WSL,
  devolvió error a Make y conservó `success: false`. Objetivos CMP/SMT revisados con
  `make -n`, sin atribuirles ejecución física en esta sesión.
- README: tres tareas de preparación completadas; validación de la nueva matriz con
  múltiples trabajadores y comprobaciones completas en Ryzen pendientes. La confirmación
  anterior de modelos individuales no se confunde con haber corrido esta suite nueva.
- Guía de instalación, objetivos, parámetros, resultados y protocolo ON/OFF en
  [docs/testing.md](docs/testing.md). Enlaces locales y `git diff --check` revisados.
  Próximo paso: ejecutar los comandos de etapa 6 en el Ryzen y conservar sus registros.


### 2026-10-16 — Etapa 7 — Benchmarks y métricas preparados; campañas físicas pendientes

- Comparados RayTracing y gray-scott: tiempos individuales, media/mediana/extremos,
  desviación muestral, IC 95 %, speedup, eficiencia, CV, estabilidad, escalabilidad
  fuerte y Amdahl. Añadida convergencia acumulada exigida por este roadmap.
- `scripts/benchmark.py`: CSV incremental, warmups excluidos, cooldown, timeouts,
  referencia FRC secuencial, verificación de afinidad y estado real, hashes/metadatos,
  campañas diagnósticas explícitas y mínimo formal de 200 corridas/configuración en Release.
- `scripts/metrics.py`, alias `plot_metrics.py` y `scalability.py`: estadísticas y nueve
  figuras posibles a 300 DPI desde CSV y metadata, sin ejecutar modelos al graficar.
  Rechazan mezclas incompatibles y duplicados; los análisis parciales quedan identificados.
  SciPy calcula Student-t y Welch; Matplotlib genera figuras, sin dependencia de pandas.
- Makefile: `benchmark_full`, `benchmark_fast`, `benchmark_on/off`, `scalability`,
  `benchmark_scaling`, `benchmark_scaling_fast`, `metrics`, `plot` y sus alias.
  El intérprete `PYTHON` también se transmite a CMake/CTest para usar el entorno científico.
- C++: `--report-metrics` emite tiempo exclusivo de codificación con precisión completa
  y contadores cooperativos opcionales, sin acumular trazas. Se conservan búsqueda,
  desempates, equipos y FRC1/FRC2. Eventos de software no se etiquetan como contexto del SO.
- Validación Release en WSL2, GNU C++ 13.3.0 y OpenCV 4.6.0:
  `/tmp/fractal-stage7-release`, CTest con Python científico: **16 aprobadas y 2 físicas
  omitidas de 18**. Solo núcleo (`/tmp/fractal-stage7-core`, `BUILD_APP=OFF`):
  **7 aprobadas y 2 físicas omitidas de 9**.
- Nuevas pruebas: seis codificaciones RGB/gris con contadores contrastados contra las
  trazas, tiempos positivos, archivos protegidos; pipeline real diagnóstico en tres
  modelos; rechazo de campañas formales cortas y protección de directorios existentes.
  Estadísticas con valores conocidos y 200 muestras sintéticas, incompatibilidades,
  faltantes/duplicados y nueve figuras sintéticas identificadas.
- Comandos reales Make: `benchmark_fast` con tres corridas de seq/fine/coarse sobre
  imagen 16×24 produjo **9 mediciones diagnósticas**; `benchmark_scaling_fast` con dos
  corridas por configuración produjo **22 mediciones diagnósticas**. Resúmenes y
  gráficas generados y combinados desde sus CSV. Artefactos en
  `/tmp/fractal-stage7-make-fast/`, `/tmp/fractal-stage7-make-scaling/` y
  `/tmp/fractal-stage7-combined-plots/`. Inspeccionado el dashboard y verificados 300 DPI.
  Estas comprobaciones no son benchmarks físicos ni permiten concluir rendimiento del Ryzen.
- Las pruebas detectaron una comprobación de equivalencia de rutas que fallaba si un
  reporte todavía no existía; se corrigió y se repitieron satisfactoriamente las pruebas
  afectadas. Enlaces y `git diff --check` revisados.
- Cinco tareas de software marcadas. Pendientes: campañas físicas de 200 corridas por
  configuración ON/OFF y revisión experimental de convergencia/escalabilidad. La figura
  de convergencia y sus tablas ya están preparadas; no se inventa ese análisis físico.
  IPC, caché, ciclos, instrucciones y contexto del kernel corresponden a etapa 8 pendiente.
- Guía completa y correspondencia de métricas: [docs/benchmarking.md](docs/benchmarking.md).


### 2026-10-16 — Receta de 200 corridas y gráficas combinadas

- Añadida al inicio de `docs/benchmarking.md` la secuencia completa para medir todos
  los modelos y escalabilidad en ON/OFF, y generar figuras desde los cuatro CSV.
- Aclarados automatización de repeticiones, número de mediciones, resultados esperados
  y diferencia respecto a las figuras que necesitan Perf en etapa 8.
- Validación documental: objetivos y argumentos contrastados con Makefile, enlaces y
  `git diff --check`. No se ejecutaron campañas físicas ni se cambiaron estados de avance.
