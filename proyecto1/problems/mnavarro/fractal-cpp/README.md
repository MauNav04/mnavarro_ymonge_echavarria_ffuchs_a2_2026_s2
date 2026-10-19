# Compresión fractal RGB y modelos de paralelismo

Proyecto de Arquitectura de Computadores II (CE4302), Instituto Tecnológico de
Costa Rica. Implementa un compresor fractal de imágenes en C++17 y compara una
versión secuencial con Fine-Grained, Coarse-Grained, CMP y SMT mediante pruebas
funcionales, benchmarks, escalabilidad y Linux Perf.

## El problema

La compresión fractal aprovecha similitudes dentro de una imagen. Divide la imagen
en bloques de destino pequeños (*ranges*) y busca, para cada uno, un bloque de
origen mayor (*domain*) que permita aproximarlo. Cada dominio se reduce y se evalúa
con ocho simetrías; se ajustan contraste y brillo y se conserva la transformación
con menor error cuadrático. La versión RGB realiza la búsqueda para cada canal.

El archivo comprimido guarda las reglas de transformación. Para reconstruir la
imagen, el decodificador aplica esas reglas de forma iterativa. Es una técnica
**con pérdida**: que todos los modelos produzcan el mismo archivo FRC demuestra
que conservan la búsqueda y sus desempates, pero no que la reconstrucción sea
idéntica a la imagen original. La calidad puede evaluarse mediante MSE y PSNR.

La búsqueda de coincidencias es el trabajo costoso que se distribuye entre hilos.
El diseño compartido utiliza **equipos por range**: los integrantes examinan dominios
distintos y reducen sus mínimos para publicar una única regla. Por ejemplo,
4 equipos de 2 trabajadores utilizan 8 hilos. Todos los modelos conservan el mismo
algoritmo matemático y los formatos FRC1/FRC2.

## Estado final de esta rama

Los cinco modelos, su automatización, los analizadores y las gráficas están
implementados.

| Etapa | Estado y evidencia |
|---|---|
| 1. Arquitectura y base secuencial | Implementada y validada; núcleo y controlador compartidos. |
| 2. Fine-Grained | Round-Robin cooperativo, cuantum configurable y resultados equivalentes a secuencial. |
| 3. Coarse-Grained | Cesión por estancamiento, reanudación sin omitir candidatos y equivalencia funcional. |
| 4. CMP | Afinidad sobre núcleos físicos; barrido de 1, 2, 4 y 8 trabajadores medido en ON/OFF. |
| 5. SMT | Topología y afinidad verificadas; campañas ON/OFF registradas y comparadas. |
| 6. Integración y pruebas | CMake, Make y suites implementadas; pruebas locales registradas y campañas físicas completas. La suite funcional dedicada completa en Ryzen conserva el pendiente indicado abajo. |
| 7. Benchmarking y métricas | 200 repeticiones por configuración en ambos estados, estadísticas, convergencia y nueve figuras de rendimiento. |
| 8. Profiling | 20 repeticiones por modelo/grupo en ambos estados, análisis de contadores disponibles y cinco figuras adicionales. LLC no disponible en estas capturas. |

**Hitos experimentales validados:**

- [x] Registrar 200 repeticiones por modelo en la campaña fija ON/OFF.
- [x] Registrar 200 repeticiones por configuración de escalabilidad ON/OFF.
- [x] Comparar modelos, escalabilidad de CMP y comportamiento de SMT ON/OFF.
- [x] Capturar y analizar Perf con 20 repeticiones por modelo/grupo disponible.
- [x] Generar boxplots, convergencia, tablas estadísticas y las 14 figuras.
- [x] Completar la campaña automática ON → OFF → ON; su manifiesto registra restauración satisfactoria.

## Requisitos y preparación del ambiente

Ejecutar los comandos desde esta carpeta. Desde la raíz del repositorio:

```bash
cd proyecto1/problems/mnavarro/fractal-cpp
```

Requisitos: GNU Make, CMake 3.16 o posterior, compilador C++17, pthreads, OpenCV 4,
Python 3 con `venv` y, para el perfilado, Linux Perf compatible con el kernel.
En Ubuntu, instalar las dependencias de sistema:

```bash
sudo apt update
sudo apt install build-essential cmake libopencv-dev python3 python3-venv python3-pip
```

En la computadora de evaluación, instalar también las herramientas de Perf para
su kernel si todavía no están disponibles:

```bash
sudo apt install linux-tools-common linux-tools-generic "linux-tools-$(uname -r)"
perf --version
```

La disponibilidad del paquete de herramientas depende del kernel instalado. Si Perf
no funciona o no tiene acceso a los contadores, seguir la preparación de
[docs/profiling.md](docs/profiling.md). El software no cambia permisos de Perf.

Preparar las dependencias Python y compilar:

```bash
make setup
make all
make help
```

`make setup` crea `.venv` e instala SciPy y Matplotlib desde
[scripts/requirements.txt](scripts/requirements.txt). Make utiliza automáticamente
ese Python; no hace falta activar el entorno. `make all` configura CMake en Release
y compila los cinco ejecutables en `build/`. Los paquetes de sistema se instalan
por separado: `make setup` sólo prepara Python.

Si se copia el proyecto desde otra computadora, generar la compilación en un
directorio nuevo; los archivos generados por CMake contienen rutas del equipo de origen:

```bash
make all BUILD_DIR=build-evaluation
```

Usar el mismo `BUILD_DIR=build-evaluation` en los comandos posteriores de ejecución
y pruebas si se elige esa ubicación.

## Usar el compresor

Las imágenes compartidas están en `assets/`. La entrada predeterminada es
`assets/color128.png`; también se incluyen `color.png`, `color480.png` y
`mirrors512.png`. Las imágenes de entrada no se redimensionan automáticamente.

Para comprimir y reconstruir una imagen con un modelo:

```bash
make run_seq
make run_fine
make run_coarse
# Los siguientes requieren la computadora de evaluación:
make run_cmp
make run_smt_on
# Sólo si el estado real de SMT ya es OFF:
make run_smt_off
```

Cada objetivo guarda `image.frc` y la reconstrucción `image.png` en
`results/demo/seq/`, `fine/`, `coarse/`, `cmp/` o `smt/`, respectivamente.
Las ejecuciones demostrativas del mismo modelo reutilizan esas rutas.
Todos los modelos comprimen; la reconstrucción se hace con `fractal_seq`.

Ejemplo con otra imagen y parámetros:

```bash
make run_fine INPUT=assets/mirrors512.png RANGE=8 STRIDE=8 TEAMS=4 THREADS_PER_TEAM=2 QUANTUM=1
```

Para controlar directamente los archivos de salida o medir la calidad:

```bash
mkdir -p results/manual
./build/fractal_seq compress assets/color128.png results/manual/image.frc --range 8 --stride 8
./build/fractal_seq decompress results/manual/image.frc results/manual/image.png --iterations 80
./build/fractal_seq compare assets/color128.png results/manual/image.png
./build/fractal_fine --help
```

`compress-gray` y `compare-gray` permiten trabajar en escala de grises. Desde Make,
`GRAY=1` selecciona compresión gris. La opción `--report-metrics archivo.csv` guarda
tiempo de codificación y contadores de software; CMP/SMT también ofrecen
`--report-affinity archivo.csv`.

## Pruebas funcionales

```bash
make test_local                  # Pruebas portables; excluye las físicas
make verify_fine                 # Compara Fine con la versión secuencial
```

En la computadora de evaluación, con SMT realmente activo:

```bash
make topology SMT_STATE=on
make test QUICK=1 SMT_STATE=on    # Suite y matriz funcional breve
make test SMT_STATE=on           # Suite y matriz funcional completa
```

Para verificar OFF por separado, ejecutar `make test SMT_STATE=off` cuando el equipo
esté realmente en ese estado. Estos objetivos no cambian SMT. `make test` combina
CTest con el ejecutor funcional de todos los modelos; `verify_all` sólo ejecuta
la matriz funcional. Las pruebas físicas opcionales de CTest informan sus omisiones,
y el ejecutor funcional completo falla si no tiene el destino o estado requerido.

Estas pruebas comprueban corrección, argumentos, sincronización y equivalencia;
las campañas estadísticas se ejecutan con los comandos de la siguiente sección.
Protocolo y alcance en [docs/testing.md](docs/testing.md).

## Campaña completa: rendimiento, escalabilidad y Perf

En el Ryzen, después de preparar el ambiente:

```bash
make quick
make run
```

`make quick` comprueba el estado actual, topología, afinidad y acceso a Perf. Ejecuta
5 repeticiones por modelo en benchmarking, 2 por configuración de escalabilidad y
1 por modelo/grupo Perf, y produce tablas, gráficas y ZIP. Se etiqueta como
**diagnóstico** y conserva el estado actual de SMT.

`make run` integra las tres campañas en **ambos estados** y genera el informe:

1. Compila en Release y valida el equipo.
2. Solicita sudo para un ayudante que controla SMT mediante sysfs.
3. Activa SMT y ejecuta benchmark, escalabilidad y Perf en ON.
4. Desactiva SMT y repite las campañas en OFF.
5. Restaura y verifica SMT ON.
6. Genera las 14 figuras, las tablas y el ZIP de resultados.

No ejecutar `sudo make run`: los compresores y los reportes se ejecutan como usuario
normal. Sólo el ayudante de SMT tiene privilegios. El kernel debe permitir el cambio
y SMT debe estar disponible en BIOS/UEFI. Ante errores o interrupciones controlables
se intenta restaurar ON; un corte de energía o SIGKILL del propio ayudante impide
asegurar esa restauración. El protocolo y la recuperación manual están en
[docs/running.md](docs/running.md).

| Campaña | Repeticiones de `make run` | Propósito |
|---|---:|---|
| Benchmark fijo | 200 por modelo | Tiempo, speedup y eficiencia con equipos fijos. |
| Escalabilidad | 200 por configuración | Barrido de 1, 2, 4, 8 y 16 trabajadores; CMP llega a 8. |
| Perf | 20 por modelo/grupo | Contadores del proceso y sus hilos; mínimo aceptado de 15. |

No se ejecuta Make 200 veces: los scripts hacen las repeticiones. Perf separa seis
grupos para medir parejas de contadores simultáneamente; sólo ejecuta los grupos
con eventos disponibles. En la campaña recibida fueron cinco grupos activos:
**1.000 compresiones de benchmark, 4.000 de escalabilidad y 500 de Perf por estado**,
sin contar referencia, sondeos y calentamientos. Son campañas distintas.

La configuración habitual es range=8, stride=8, Q=1, equipos 4×2 para Fine/Coarse/CMP
y 4×4 para SMT. Para otra imagen, por ejemplo:

```bash
make run INPUT=assets/mirrors512.png
```

Usar siempre imagen, parámetros y binarios comparables entre ON/OFF. Los objetivos
individuales siguen disponibles con `make help_advanced`; permiten ejecutar cada
campaña sin cambiar SMT automáticamente:

```bash
make benchmark_full SMT_STATE=on RUNS=200 BENCH_DIR=metrics/custom-benchmark-on
make scalability SMT_STATE=on RUNS_SCALING=200 SCALING_DIR=metrics/custom-scaling-on
make profile_perf SMT_STATE=on PERF_RUNS=20 PERF_DIR=metrics/custom-perf-on
```

Las capturas individuales requieren directorios nuevos. Las guías de
[benchmarking](docs/benchmarking.md) y [profiling](docs/profiling.md) explican las
opciones, las recetas manuales ON/OFF y la disponibilidad de eventos.

## Regenerar gráficas y compartir resultados

Si ya existen las mediciones completas:

```bash
make report
```

Este comando busca CSV con sus metadatos compatibles, selecciona las campañas,
genera un informe nuevo con rendimiento y Perf y exporta un ZIP, sin repetir
compresiones ni cambiar SMT. Puede utilizarse en otra computadora después de copiar
los resultados e instalar las dependencias Python.

```text
metrics/
  automatic/run-pair-<fecha-id>/    Campañas originales ON/OFF y manifiesto
  reports/physical-<fecha-id>/
    benchmark/                    Figuras 01–09, summary.csv, scaling.csv, convergence.csv
    perf/                         Figuras 10–14, estadísticas, cobertura y comparación ON/OFF
    report.json                   Estado y campañas utilizadas
  latest.json                     Rutas del último informe y ZIP
results/exports/                  ZIP de los resultados completos
```

Los diagnósticos usan nombres `quick-...` y `diagnostic-...`. Cada captura e informe
automático recibe un nombre único. Las rutas absolutas de un `latest.json` transferido
pertenecen al equipo que lo produjo; consultar las carpetas locales o ejecutar
`make report` para generar referencias correspondientes a la nueva ubicación.

Las figuras 01–09 cubren boxplots, speedup, eficiencia, comparativas, dashboard,
escalabilidad/Amdahl, convergencia, estabilidad temporal y planificación por software.
Las figuras 10–14 cubren IPC, caché, contexto del SO, dashboard Perf y CPU efectivas.
Se exportan a 300 DPI y mantienen el color de cada modelo; OFF se distingue mediante
trazos o barras rayadas. `make plot` genera las figuras de benchmarking;
`make profile_plot` genera las de Perf; `make report` reúne ambos conjuntos.

Conservar las carpetas completas, incluidos CSV, `metadata.json`, afinidad y registros.
Perf guarda las salidas crudas en `perf-stat.jsonl`. El ZIP incluye los CSV, aunque
otras reglas de Git puedan ignorarlos. Para exportar por separado:

```bash
make archive_metrics METRICS_ARCHIVE=results/exports/metrics-manual.zip
```

La ruta del ZIP debe ser nueva. No se borran los datos originales.

## Resultados físicos incluidos

Campaña: [run-pair-20261017T101552Z-36af3e](metrics/automatic/run-pair-20261017T101552Z-36af3e).
Informe: [physical-20261017T120628Z-e7b4d7](metrics/reports/physical-20261017T120628Z-e7b4d7).
Los manifiestos registran éxito para ambas capturas, generación de 14 figuras y
restauración de SMT ON. Las mediciones las realizó el usuario en el Ryzen; revisarlas
localmente no constituye una nueva ejecución física.

Entrada: `assets/color128.png`, 128×128 RGB, range=8, stride=8 y Q=1.
La campaña fija usa 4×2 trabajadores en Fine/Coarse/CMP y 4×4 en SMT.
Medias y speedup respecto a la referencia secuencial del mismo estado:

| Modelo | Tiempo ON (ms) | Speedup ON | Tiempo OFF (ms) | Speedup OFF |
|---|---:|---:|---:|---:|
| Secuencial | 273,716 | 1,000× | 274,398 | 1,000× |
| Fine-Grained | 1.074,041 | 0,255× | 1.002,260 | 0,274× |
| Coarse-Grained | 451,111 | 0,607× | 434,149 | 0,632× |
| CMP | 51,111 | 5,355× | 49,644 | 5,527× |
| SMT | 46,320 | 5,909× | 57,394 | 4,781× |

En SMT OFF se mantienen 16 trabajadores sobre 8 CPU físicas, por lo que esa fila
representa sobresuscripción por software. Los resultados corresponden a esta carga
y configuración; no establecen una aceleración universal de cada modelo.

CMP mejora al pasar de 1 a 8 trabajadores: en el barrido ON su speedup pasa de 0,972×
a 5,346×. Fine con 8 trabajadores tarda aproximadamente cuatro veces lo secuencial.
En la revisión descriptiva de las 50 configuraciones/estado/campaña del resumen,
la deriva máxima de la última ventana respecto a la media fue 2,33 % y el semiancho
relativo máximo del IC 95 % fue 1,41 %. Estos indicadores describen la muestra,
sin garantizar estabilidad para otras imágenes o condiciones.

En Perf ON, el IPC medio fue 2,65 para secuencial y 1,21 para Fine; este último
registró unos 173.000 cambios de contexto por ejecución y mayor tasa de fallos L1.
Los contadores son consistentes con un costo elevado de coordinación; no atribuyen
un porcentaje exacto de instrucciones a funciones específicas. LLC aparece N/D:
no se midió su tasa de fallos y no se sustituye por caché genérica.

Fuentes y figuras principales:

- [Resumen de rendimiento](metrics/reports/physical-20261017T120628Z-e7b4d7/benchmark/summary.csv).
- [Speedup](metrics/reports/physical-20261017T120628Z-e7b4d7/benchmark/02_speedup.png) y [escalabilidad](metrics/reports/physical-20261017T120628Z-e7b4d7/benchmark/06_strong_scaling_amdahl.png).
- [Resumen Perf](metrics/reports/physical-20261017T120628Z-e7b4d7/perf/summary.csv) y [cobertura de eventos](metrics/reports/physical-20261017T120628Z-e7b4d7/perf/coverage.csv).
- [Cambios de contexto](metrics/reports/physical-20261017T120628Z-e7b4d7/perf/12_perf_context_switches.png).

## Modelos y organización del código

| Modelo | Ejecutable | Política |
|---|---|---|
| Secuencial | `fractal_seq` | Búsqueda de referencia en un hilo. |
| Fine-Grained | `fractal_fine` | Round-Robin cooperativo global; por defecto cede tras un dominio y sus ocho simetrías. Un trabajador busca a la vez. |
| Coarse-Grained | `fractal_coarse` | Conserva el turno hasta finalizar su intervalo o superar 2/3 de sus dominios locales sin mejorar consecutivamente el mínimo. Cede y después reanuda sin descartar candidatos. |
| CMP | `fractal_cmp` | Equipos concurrentes; un trabajador por núcleo físico y afinidad verificada. |
| SMT | `fractal_smt` | Motor paralelo compartido con CMP; afinidad y estado SMT reales verificados. |

Cada modelo tiene su propio `main.cpp` y `CMakeLists.txt`; los ejecutables no permiten
seleccionar otro modelo mediante argumentos. Todos usan el controlador común y el
mismo núcleo de compresión. Sólo `fractal_seq` ofrece descompresión.

| Ruta | Responsabilidad |
|---|---|
| `src/`, `include/` | Compresión, canales, planificación, topología y ejecución compartidas. |
| `app/` | CLI, lectura/escritura de imágenes, FRC y reportes nativos. |
| `sequential/`, `fine-grained/`, `coarse-grained/`, `CMP/`, `SMT/` | Entradas y adaptaciones de cada modelo. |
| `assets/` | Imágenes de entrada compartidas. |
| `tests/` | Pruebas del núcleo, CLI, concurrencia, captura y análisis. |
| `scripts/` | Automatización, control SMT, benchmarks, Perf y gráficas. |
| `metrics/`, `results/` | Capturas, informes, archivos comprimidos, reconstrucciones y exportaciones. |
| `docs/` | Guías técnicas y metodología detallada. |

El **Makefile raíz incluye `scripts/Makefile`** y ofrece comandos de uso habitual.
**CMakeLists.txt define la compilación**, bibliotecas, dependencias y pruebas; CMake
genera los archivos de construcción dentro de `build/`. Make configura y llama a
CMake antes de ejecutar los programas. Las opciones avanzadas también pueden
consultarse con `make help_advanced`.

## Límites y pendientes reales

Los eventos cooperativos de Fine/Coarse son contadores de software, distintos de
los cambios de contexto del SO. El tiempo de benchmarking comprende exclusivamente
la codificación; Perf mide el comando completo, incluyendo arranque, E/S y reportes.
No mezclar ambos alcances para calcular speedup ni dividir task-clock por el tiempo
del algoritmo: las CPU efectivas usan duration_time de la misma pasada.

Un contador no soportado o inválido queda N/D y se documenta en `coverage.csv`;
una captura terminada no implica que todos los eventos solicitados estuvieran
disponibles. Los datos sintéticos, diagnósticos y físicos se identifican por separado.

## Documentación y validación

- [Ejecución automática, SMT y recuperación](docs/running.md).
- [Pruebas funcionales](docs/testing.md).
- [Benchmarking, estadísticas y gráficas](docs/benchmarking.md).
- [Linux Perf](docs/profiling.md).
- [Fine-Grained](docs/fine_grained.md), [Coarse-Grained](docs/coarse_grained.md), [CMP](docs/cmp.md) y [SMT](docs/smt.md).

Revisión documental del 2026-10-18: las guías de `docs/` se contrastaron con el estado
final, Makefile y CLI; se corrigieron referencias obsoletas, enlaces y repeticiones
de Perf. No se encontraron credenciales ni claves privadas en los documentos revisados.
La [guía de ejecución](docs/running.md#información-incluida-al-compartir-resultados)
explica qué identificadores pueden contener los registros exportados. Esta revisión
no modifica campañas ni amplía las validaciones físicas.
