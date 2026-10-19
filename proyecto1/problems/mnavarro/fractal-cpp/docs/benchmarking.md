# Etapa 7 — Benchmarks, métricas y gráficas

Para el uso habitual, la [guía automática](running.md) reduce el proceso a
`make quick`, `make run` y `make report`. Esta página conserva las opciones
individuales y los detalles metodológicos.

## Receta completa: 200 corridas y todas las figuras de etapa 7

Esta es la secuencia para obtener CSV, tablas y gráficas de rendimiento equivalentes
al alcance de benchmarking de RayTracing. Las 200 corridas son **por modelo y
configuración**: Make invoca el script y el script hace las repeticiones automáticamente.
No ejecutar Make 200 veces. La campaña física ON/OFF del 2026-10-17 está incluida
en `metrics/`; resultados y fuentes en el [README](../README.md#resultados-físicos-incluidos).
Las recetas siguientes permiten repetirla en directorios nuevos.
Las gráficas adicionales de contadores Perf se obtienen
con la [receta de etapa 8](profiling.md).

Ejecutar en Ubuntu nativo del Ryzen, desde `proyecto1/problems/mnavarro/fractal-cpp/`.
Usar el mismo código, imagen, parámetros y binarios en ON/OFF. Los directorios de
salida siguientes deben ser nuevos; cambiar sus nombres si ya existen.

**A. Preparar las herramientas una vez:**

```bash
sudo apt install build-essential cmake libopencv-dev python3-venv
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -r scripts/requirements.txt
make all BUILD_DIR=build-evaluation
```

**B. Con SMT realmente ON, medir todos los modelos y su escalabilidad:**

```bash
make topology BUILD_DIR=build-evaluation SMT_STATE=on
make benchmark_full BUILD_DIR=build-evaluation SMT_STATE=on RUNS=200 BENCH_DIR=metrics/final-on
make scalability BUILD_DIR=build-evaluation SMT_STATE=on RUNS_SCALING=200 SCALING_DIR=metrics/scaling-final-on
```

La primera campaña mide 200 veces cada uno de los cinco modelos. La segunda mide
200 veces cada configuración de hilos del barrido; CMP llega a 8 y SMT a 16.
Ambos comandos guardan CSV y generan sus tablas estadísticas automáticamente.

**C. Desactivar SMT mediante intervención del usuario y repetir en OFF:**

Si hubo reinicio, volver a esta carpeta y activar `.venv` de nuevo antes de continuar.
`SMT_STATE=off` no modifica el hardware; solo comprueba el estado esperado.

```bash
. .venv/bin/activate
make topology BUILD_DIR=build-evaluation SMT_STATE=off
make benchmark_full BUILD_DIR=build-evaluation SMT_STATE=off RUNS=200 BENCH_DIR=metrics/final-off
make scalability BUILD_DIR=build-evaluation SMT_STATE=off RUNS_SCALING=200 SCALING_DIR=metrics/scaling-final-off
```

**D. Generar todas las gráficas juntas, leyendo los cuatro CSV:**

```bash
make plot CSV_FILES="metrics/final-on/raw.csv metrics/final-off/raw.csv metrics/scaling-final-on/raw.csv metrics/scaling-final-off/raw.csv" PLOT_DIR=metrics/final-plots
```

Este último comando no vuelve a comprimir ni repite las mediciones. En
`metrics/final-plots/` quedan el resumen combinado, la convergencia, la tabla de
escalabilidad y las nueve figuras disponibles: boxplot, speedup, eficiencia,
comparativas SMT/granularidad, dashboard, Amdahl, convergencia, estabilidad temporal
y eventos de planificación de software. Las figuras son PNG a 300 DPI.

Con los valores predeterminados se generan **1000 mediciones de campaña fija y 4000
de escalabilidad por estado**: 10 000 mediciones en total para la secuencia ON/OFF,
sin contar calentamientos. Puede ser una campaña larga; `benchmark_fast` sirve para
estimar duración y comprobar el pipeline, pero no sustituye esas 200 repeticiones.

Comprobar que todos los comandos terminan con código cero, que cada campaña tiene
`success: true` en `metadata.json`, y que `analysis.json` del análisis combinado
indica `success: true` y `complete: true`. Conservar las cuatro carpetas completas,
incluidos sus `metadata.json`, y la carpeta de figuras. Restaurar después SMT según
la configuración habitual del equipo.

**Separación de campañas:** IPC, cache-miss rates, ciclos, instrucciones y cambios de
contexto del SO proceden de Perf. Los comandos anteriores generan las figuras de
benchmarking; para añadir las cinco figuras de contadores ejecutar
[las campañas y `make profile_plot` de etapa 8](profiling.md). Tampoco se añade el modelo Fine-Grained
cíclico de las referencias: se conserva el cooperativo aprobado para fractales.

## Correspondencia con RayTracing y gray-scott

Para generar las figuras 01–09 junto con las 10–14 de Perf, existe `make plot_all`.
La [receta para las campañas ON ya ejecutadas](profiling.md#las-figuras-0109-y-1014-pertenecen-a-conjuntos-distintos)
explica las dos listas de CSV y los directorios de salida. `plot` y `profile_plot`
siguen disponibles por separado. Los comandos de captura no generan automáticamente
ambos conjuntos de figuras, y el barrido de hilos es necesario para la figura 06.

Se revisaron `scripts/benchmark.py`, `scripts/plot_metrics.py` y los Makefile de
[RayTracing](../../../echavarria/RayTracing/scripts/benchmark.py) y
[gray-scott](../../../gray-scott/scripts/benchmark.py), además de
[scalability.py](../../../gray-scott/scripts/scalability.py). La cobertura común de
rendimiento se conserva, adaptando la entrada y el reparto a compresión fractal.
Todos los nombres nuevos de archivos y carpetas están en inglés. Los encabezados
estadísticos principales conservan los nombres de los proyectos de referencia.

| Métrica de referencia | Implementación fractal |
|---|---|
| Tiempo individual en ms y s | `tiempo_ms`, `tiempo_s`, medidos por C++ alrededor de `execute_encode` |
| Eventos de planificación | `eventos_contexto`, `turns`, `scheduler_switches`, `stalls` |
| Número de corridas e hilos | `corridas`, `hilos`, más equipos y miembros |
| Media, mediana, mínimo, máximo | `media_ms`, `mediana_ms`, `min_ms`, `max_ms` |
| Desviación estándar muestral | `std_dev_ms`, denominador n−1 |
| Error estándar e IC 95 % | `error_estandar_ms`, semiancho `margen_error_ms`, `ci_95_inf_ms`, `ci_95_sup_ms` |
| Speedup vs secuencial | `speedup = media_seq / media_modelo` |
| Eficiencia paralela | `eficiencia = 100 × speedup / hilos` |
| Coeficiente de variación / jitter | `cv_pct = 100 × std_dev / media` |
| Estabilidad temporal | Tiempos individuales ordenados por `run_id` |
| Escalabilidad fuerte | Barrido 1,2,4,8,16; tiempo, speedup y eficiencia |
| Ideal lineal y Amdahl | S=p y fracciones teóricas 0.90, 0.95, 0.98 |
| Boxplots, comparativas y dashboard | Figuras 01–06 descritas abajo, PNG 300 DPI |
| Convergencia solicitada por el roadmap | Media e IC acumulados, cambios y deriva de la última ventana |

Los eventos de planificación son **contadores de software**, como los eventos propios
de los algoritmos de referencia. No son cambios de contexto del kernel. En
`eventos_contexto`, Fine-Grained registra cambios reales de turno entre trabajadores;
Coarse-Grained registra stalls de búsqueda. Las columnas separadas conservan ambos
contadores. Secuencial/CMP/SMT no ejecutan el planificador cooperativo y registran cero
para esos eventos, sin afirmar que el SO no haya cambiado de contexto.

Los otros proyectos incluyen Fine-Grained cíclico además del cooperativo. Fractales
conserva únicamente el cooperativo aprobado; no inventa una serie cíclica ni modifica
la arquitectura para llenar una gráfica. Las comparativas usan los modelos existentes.

Las figuras adicionales de IPC, cache-miss rates, ciclos, instrucciones y cambios de
contexto del SO de las referencias necesitan Linux Perf: corresponden a **etapa 8**.
No se fabrican esos contadores ni se presentan estimaciones como mediciones. La figura
04 compara tiempos CMP/SMT y granularidad, como la figura equivalente de las referencias;
no constituye un perfilado de contadores microarquitectónicos.

## Preparar el Ryzen

Desde `proyecto1/problems/mnavarro/fractal-cpp/` en Ubuntu nativo de evaluación:

```bash
sudo apt install python3-venv
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -r scripts/requirements.txt
make all BUILD_DIR=build-evaluation
make test_benchmarks BUILD_DIR=build-evaluation
```

Se añaden dos dependencias científicas justificadas: **SciPy**, para los cuantiles
Student-t y grados de libertad de Welch; **Matplotlib**, para las figuras PNG en un
entorno sin escritorio. NumPy se instala como dependencia de estas bibliotecas.
No se necesita pandas. CMake usa el intérprete seleccionado por `PYTHON`, también
para CTest; se puede pasar `PYTHON=.venv/bin/python` explícitamente a Make.
No copiar un build creado en otra computadora. Las campañas formales exigen Release.

## Flujo rápido: comprobar CSV y gráficas

Con SMT realmente ON:

```bash
make topology BUILD_DIR=build-evaluation SMT_STATE=on
make benchmark_fast BUILD_DIR=build-evaluation SMT_STATE=on FAST_DIR=metrics/check-on
make plot BENCH_DIR=metrics/check-on
```

`benchmark_fast` ejecuta cinco corridas por configuración y una de calentamiento
excluida de las estadísticas. Sus registros y figuras dicen **DIAGNÓSTICO**: no satisfacen
las 200 corridas de la campaña formal. El directorio debe ser nuevo para repetir.
Los diagnósticos de CMP/SMT siguen exigiendo el hardware real; no hay bypass de afinidad.

En WSL solo puede verificarse el pipeline con los modelos portables:

```bash
make benchmark_fast BENCHMARK_MODELS=seq,fine,coarse FAST_DIR=metrics/local-diagnostic
make plot BENCH_DIR=metrics/local-diagnostic
```

En un diagnóstico exclusivamente portable, SMT se etiqueta como estado esperado no
verificado; no se afirma haber detectado SMT ON/OFF en WSL.
Esas mediciones locales no se pueden mezclar con campañas formales ni usar como
resultados del Ryzen. Elegir una imagen pequeña mediante `INPUT` permite comprobar
rápidamente la automatización antes de una campaña larga.

## Campaña formal: 200 corridas por configuración

**SMT ON:**

```bash
make benchmark_full BUILD_DIR=build-evaluation SMT_STATE=on BENCH_DIR=metrics/benchmark-on RUNS=200
make plot BENCH_DIR=metrics/benchmark-on
make scalability BUILD_DIR=build-evaluation SMT_STATE=on SCALING_DIR=metrics/scaling-on RUNS_SCALING=200
make plot CSV_FILES="metrics/scaling-on/raw.csv" PLOT_DIR=metrics/scaling-on/plots
```

Tras desactivar SMT realmente mediante intervención del usuario, conservar código,
entrada, parámetros y binarios y ejecutar **SMT OFF**:

```bash
make benchmark_full BUILD_DIR=build-evaluation SMT_STATE=off BENCH_DIR=metrics/benchmark-off RUNS=200
make plot BENCH_DIR=metrics/benchmark-off
make scalability BUILD_DIR=build-evaluation SMT_STATE=off SCALING_DIR=metrics/scaling-off RUNS_SCALING=200
make plot CSV_FILES="metrics/scaling-off/raw.csv" PLOT_DIR=metrics/scaling-off/plots
```

Los objetivos individuales descritos aquí no cambian SMT. `SMT_STATE` exige
el estado esperado. `make run` automatiza ON/OFF/ON con sudo; véase
[ejecución automática](running.md). Si se usan comandos individuales, restaurar
después la configuración del equipo. Cerrar aplicaciones
ajenas a la medición y conservar las condiciones de energía/refrigeración. El cooldown
no garantiza por sí solo ausencia de thermal throttling.

Cada estado mide su propia línea base secuencial. Por defecto la campaña fija usa
4 equipos × 2 miembros en Fine/Coarse/CMP y 4×4 en SMT. Son 1000 mediciones por estado
con cinco modelos; la escalabilidad completa tiene 20 configuraciones válidas y 4000
mediciones por estado. La duración depende de la imagen y el hardware: hacer primero
el diagnóstico para estimarla. No hay tiempos calibrados inventados para este Ryzen.

## Combinar las campañas ON/OFF para compararlas

Las gráficas combinadas muestran SMT ON/OFF y las demás configuraciones medidas,
con baseline propio de cada estado. Se pueden combinar benchmark y escalabilidad:

```bash
make plot CSV_FILES="metrics/benchmark-on/raw.csv metrics/benchmark-off/raw.csv metrics/scaling-on/raw.csv metrics/scaling-off/raw.csv" PLOT_DIR=metrics/comparison-on-off
```

Para regenerar las gráficas en otra computadora bastan los CSV crudos y sus
`metadata.json` asociados, conservados juntos en cada carpeta. No se necesitan los
ejecutables ni ejecutar los modelos.

El analizador exige la misma máquina, tipo de medición, entrada, geometría, canales,
cuantum y binarios compartidos. No mezcla muestras ON y OFF al calcular medias o
speedups. Cada campaña/configuración conserva su identidad. Las condiciones ambientales
adicionales deben revisarse junto con `metadata.json`; el identificador de máquina no
certifica por sí solo toda la configuración física.

## Archivos generados

Cada carpeta de campaña contiene:

```text
metrics/benchmark-on/
├── raw.csv                  # Una fila por corrida medida; flush incremental
├── metadata.json            # Hardware, estado, configuración, hashes y éxito/fallo
├── commands.jsonl           # Comandos, stdout/stderr, retorno y timeouts; incluye warmups
├── reference.frc            # Referencia secuencial para verificar cada resultado
├── <modelo>-<equipos>x<miembros>/
│   ├── output.frc           # Último FRC de esa configuración, verificado en cada corrida
│   ├── metrics.csv          # Último reporte nativo de tiempo y contadores
│   └── affinity.csv         # Última afinidad, en CMP/SMT; se verifica en cada corrida
├── statistics/              # Creado automáticamente por los objetivos benchmark/scalability
│   ├── summary.csv
│   ├── convergence.csv
│   ├── scaling.csv          # Solo si hay datos de escalabilidad
│   └── analysis.json
└── plots/                   # Creado al ejecutar make plot
```

`raw.csv` conserva todas las mediciones, aunque se reutilicen los archivos FRC por
configuración para no duplicar miles de imágenes. Guarda su hash en cada fila, validado
contra secuencial. Cada fila incluye tiempo computacional nativo y `wall_s` del
subproceso completo. Las estadísticas y gráficas principales usan **tiempo nativo**.
La lectura de imagen, serialización, CSV, stdout, hashes, comparación y cooldown quedan
fuera de ese tiempo. Se incluyen preparación de dominios, creación/join de pthreads,
sincronización y verificaciones de afinidad que forman parte de `execute_encode`.

Los contadores ligeros opcionales incrementan bajo el mutex ya existente. No se activa
la traza por dominio ni se acumula una lista de eventos durante los benchmarks.
El reporte `--report-metrics archivo.csv` está disponible en los cinco ejecutables.

Antes de las mediciones se genera la referencia secuencial; cada FRC debe coincidir
byte por byte. Las reconstrucciones ya se contrastan en etapa 6. No se incluyen las
reconstrucciones ni sus tiempos en la métrica de compresión de etapa 7.

## Figuras equivalentes y adicionales

| Archivo | Contenido |
|---|---|
| `01_execution_boxplot.png` | Distribución de tiempos, mediana, media y outliers |
| `02_speedup.png` | Speedup con IC 95 % aproximado y referencia 1× |
| `03_efficiency.png` | Eficiencia, incertidumbre y referencia 100 % |
| `04_comparisons.png` | CMP/SMT ON/OFF y Coarse/Fine, con IC de tiempos |
| `05_dashboard.png` | Tiempo medio, speedup, eficiencia y coeficiente de variación |
| `06_strong_scaling_amdahl.png` | Speedup y eficiencia vs p, ideal y Amdahl |
| `07_convergence.png` | Media e IC relativos acumulados |
| `08_temporal_stability.png` | Tiempo individual por número de corrida |
| `09_software_events.png` | Cambios de turno y stalls de software |

Solo se generan las figuras que tienen datos: 01–05 requieren campaña fija, 06 un
barrido y 09 modelos cooperativos. No se inventan series faltantes. Todas son PNG a
300 DPI. Los datos diagnósticos, sintéticos e incompletos se identifican en las figuras.

Los CSV estadísticos usan valores numéricos sin sufijos `x` o `%`; los nombres/unidades
indican el significado y las figuras añaden las etiquetas. Esto permite procesarlos
sin depender de formatos de presentación.

## Cálculos y metodología

- Media aritmética, mediana, extremos y desviación estándar muestral sin eliminar outliers.
- Error estándar: `s / sqrt(n)`. Semiancho del IC de la media:
  `t(0.975, n−1) × s / sqrt(n)`. Se usa Student-t como RayTracing, en lugar de fijar
  1.96 como gray-scott. Con una sola muestra no se inventan desviación ni IC.
- Speedup: cociente de medias secuencial/modelo dentro de la misma campaña y estado.
  Su IC es una aproximación log-delta con incertidumbre de **ambas** medias y grados
  de libertad de Welch, suponiendo muestras independientes. La referencia secuencial
  contra sí misma tiene speedup 1. Los IC no implican que los tiempos sean normales.
- Eficiencia: speedup dividido entre el número de pthreads, multiplicado por 100.
  En los modelos cooperativos ese denominador no significa que todos ejecuten búsqueda
  simultáneamente; se conserva la definición académica y el modelo aprobado.
- Escalabilidad fuerte: imagen/geometría/canales constantes, p en 1,2,4,8,16 por defecto.
  Con dos miembros preferidos: p=1 usa 1×1; p=2 usa 1×2; p=4 usa 2×2; etc.
  CMP nunca mide p=16: queda excluido explícitamente en metadata por su límite de 8
  núcleos. No se ejecuta como otra modalidad. SMT llega a 16 en ambos estados reales.
- Amdahl es una referencia teórica con fracciones 90/95/98 %, no una fracción paralela
  medida ni ajustada automáticamente a los datos.
- Convergencia: media e IC para cada prefijo de muestras, cambio relativo de la media y
  deriva de la última quinta parte respecto a la media total. Es análisis descriptivo;
  no prueba estacionariedad ni permite terminar antes de las 200 corridas formales.
- CV y estabilidad temporal permiten inspeccionar variación; no certifican por sí solos
  ausencia de ruido térmico, cambios de frecuencia o tareas del sistema.

## Objetivos y parámetros

`benchmark` y `benchmark_full` son equivalentes. `benchmark_on/off` fijan el estado
esperado; `scalability`, `benchmark_scaling` y `escalabilidad` son alias del mismo barrido.
`benchmark_scaling_fast` prueba el barrido con pocas corridas diagnósticas.
`metrics` genera solo tablas; `plot`, `plot_metrics` y `graficas` generan tablas y figuras
sin ejecutar los programas C++. Los alias Python `plot_metrics.py` y `scalability.py`
reutilizan las implementaciones principales.

| Variable Make | Valor por defecto |
|---|---|
| `RUNS`, `RUNS_SCALING` | 200 por configuración |
| `FAST_RUNS`, `WARMUPS`, `COOLDOWN` | 5 repeticiones, 1 calentamiento, 0.15 segundos |
| `BENCHMARK_MODELS` | `all`; o `seq,cmp,smt,coarse,fine` y subconjuntos |
| `TEAMS`, `THREADS_PER_TEAM` | 4, 2 |
| `SMT_TEAMS`, `SMT_THREADS_PER_TEAM` | 4, 4 |
| `THREADS_SCALING` | `1,2,4,8,16` |
| `SMT_STATE` | `on` |
| `BENCH_DIR`, `FAST_DIR`, `SCALING_DIR` | `metrics/benchmark-on`, `metrics/diagnostic-on`, `metrics/scaling-on` según estado |
| `CSV_FILES` | `<BENCH_DIR>/raw.csv`; admite varios CSV |
| `PLOT_DIR`, `STATS_DIR` | `<BENCH_DIR>/plots`, `<BENCH_DIR>/statistics` |

`INPUT`, `RANGE`, `STRIDE`, `GRAY`, `QUANTUM`, `BUILD_DIR`, `JOBS`, `PYTHON` y `TIMEOUT`
se comparten con la automatización anterior. `QUICK` corresponde a pruebas de etapa 6;
para benchmarks cortos usar `benchmark_fast`/`FAST_RUNS`. Una campaña formal con RUNS<200
se rechaza. `TIMEOUT` limita cada proceso; aumentarlo para imágenes grandes/cooperativas.

Las carpetas de captura y de análisis de rendimiento deben ser **nuevas**: no se sobrescriben ni se anexan
corridas incompatibles. Para repetir, elegir otra ruta. No se eliminan campañas con clean.
Una falla o interrupción conserva CSV/logs parciales y no declara éxito. Cada muestra
válida se guarda antes de calcular estadísticas. Un fallo queda señalado; no se incluye
como un tiempo cero ni se descarta silenciosamente para mejorar los resultados.

Para inspeccionar una campaña interrumpida:

```bash
.venv/bin/python scripts/metrics.py --csv metrics/interrupted-campaign/raw.csv --output-dir metrics/partial-analysis --allow-partial
```

Se etiqueta incompleta; no satisface la campaña formal. El analizador rechaza duplicados,
mezclas de máquina/binarios/entrada, datos no finitos, configuraciones incompletas y la
falta de baseline. También evita mezclar diagnósticos con resultados físicos.

## Estado de validación

Las pruebas `benchmark_report_tests` contrastan contadores con las trazas existentes y
comprueban preservación de archivos. `benchmark_pipeline_tests` ejecuta campañas reales
pero diagnósticas en los tres modelos portables y verifica límites/repeticiones/CSV.
`metrics_tests` usa datos sintéticos identificados para verificar fórmulas, rechazo de
mezclas/incompletos y las nueve figuras. Sin las dependencias científicas esta última
se omite en CTest con código 77; `make test_benchmarks` exige instalarlas.

La campaña física ON/OFF incluida registra 1.000 mediciones fijas y 4.000 de
escalabilidad por estado: 200 repeticiones para cada configuración. Los CSV, metadatos,
boxplots y análisis de convergencia están en `metrics/automatic/` y `metrics/reports/`.
El informe físico contiene nueve figuras de rendimiento; se verificaron los recuentos,
hashes FRC declarados y reportes de afinidad conservados. La deriva máxima de la última
ventana fue 2,33 % y el semiancho relativo máximo del IC de la media fue 1,41 % en
las 50 filas del resumen conjunto; son indicadores descriptivos de esta campaña.

Las mediciones fueron realizadas por el usuario en el Ryzen. Las pruebas locales y
los gráficos sintéticos validan las herramientas, no representan nuevas mediciones
físicas. El [README](../README.md) conserva el estado final y los pendientes reales.
La captura de contadores se documenta por separado en [profiling](profiling.md).
