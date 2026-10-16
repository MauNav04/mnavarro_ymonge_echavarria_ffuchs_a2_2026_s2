# Etapa 8: Linux Perf en el Ryzen de evaluación

El destino es Ubuntu nativo, AMD Ryzen con 8 núcleos físicos y 16 CPU lógicas con SMT
activo (8 con SMT inactivo). Se utiliza la misma validación de topología y afinidad
que CMP/SMT. No hay perfil local ni un modo que omita esa comprobación. Un diagnóstico
corto también necesita esa máquina. Esta guía deja la campaña preparada; no afirma
que ya se hayan recolectado contadores físicos.

## Preparación

Desde `proyecto1/problems/mnavarro/fractal-cpp/`:

```bash
python3 -m venv .venv
.venv/bin/python -m pip install -r scripts/requirements.txt
make all BUILD_DIR=build-evaluation PYTHON=.venv/bin/python
perf --version
```

Si falta Perf, instalar las herramientas para el kernel de Ubuntu utilizado, por
administración de esa computadora. La instalación habitual es:

```bash
sudo apt install linux-tools-common linux-tools-generic "linux-tools-$(uname -r)"
```

La disponibilidad de ese paquete depende del kernel instalado. `perf --version` debe
funcionar antes de continuar. Los scripts no instalan paquetes, no ejecutan `sudo`,
no cambian SMT ni escriben parámetros del kernel.

Con SMT **realmente activo**, comprobar acceso y eventos disponibles:

```bash
make setup_perf BUILD_DIR=build-evaluation PYTHON=.venv/bin/python SMT_STATE=on
```

Guarda `metrics/perf-check-on/availability.json`, metadatos, sondeos `.stat` y
`commands.jsonl`. Cada evento tiene un estado explícito. Este comando sondea eventos;
no ejecuta la campaña de 200 muestras ni genera gráficos. Si se repite, elegir otro
directorio, por ejemplo `PERF_CHECK_DIR=metrics/perf-check-on-2`.

Si aparecen permisos denegados, revisar `cat /proc/sys/kernel/perf_event_paranoid`
y los registros con el administrador. El perfil incluye eventos del proceso en usuario
y kernel, además de los hilos heredados. La política de Perf depende de ese valor y de
las capacidades del usuario, como explica la
[documentación de seguridad de Linux](https://cdn.kernel.org/doc/html/latest/admin-guide/perf-security.html).
Un administrador puede, si corresponde en esa computadora, anotar el valor anterior y
habilitar temporalmente la medición por proceso:

```bash
cat /proc/sys/kernel/perf_event_paranoid
sudo sysctl -w kernel.perf_event_paranoid=1
```

Después de terminar, restaurar manualmente el valor anterior con `sudo sysctl`.
No es necesario ejecutar los compresores como root. Cambiar permisos no añade eventos
que la PMU del procesador o su kernel no soporten.

## Diagnóstico breve antes de las 200 repeticiones

```bash
make profile_fast BUILD_DIR=build-evaluation PYTHON=.venv/bin/python SMT_STATE=on
make profile_plot PYTHON=.venv/bin/python \
  PERF_CSV_FILES="metrics/perf-diagnostic-on/events.csv" \
  PERF_PLOT_DIR=metrics/perf-diagnostic-plots
```

Por defecto hay una repetición por grupo y modelo, sin IC para una sola muestra.
Se etiqueta `diagnostic`, exige la misma máquina real y no sustituye la campaña formal.
Verificar `metadata.json: success=true`, `statistics/coverage.csv`, los FRC y las figuras.
Si una captura falla, queda `success=false`, el error y los registros parciales; el
Makefile se detiene. Una captura exitosa puede tener eventos no disponibles: comprobar
además `analysis.json: all_events_measured` y los estados por evento.

## Campaña completa y las cinco gráficas que faltaban

**1. SMT activo**, sin cambiar de imagen ni binarios entre campañas:

```bash
make profile_perf BUILD_DIR=build-evaluation PYTHON=.venv/bin/python \
  SMT_STATE=on PERF_RUNS=200 PERF_DIR=metrics/perf-on
```

**2. Desactivar SMT manualmente** según el protocolo de evaluación. Si hay reinicio,
volver a la misma carpeta. Confirmar el estado con `make topology SMT_STATE=off
BUILD_DIR=build-evaluation`, y ejecutar:

```bash
make profile_perf BUILD_DIR=build-evaluation PYTHON=.venv/bin/python \
  SMT_STATE=off PERF_RUNS=200 PERF_DIR=metrics/perf-off
```

**3. Generar todas las tablas y figuras de Perf de ambos estados, sin volver a comprimir:**

```bash
make profile_plot PYTHON=.venv/bin/python \
  PERF_CSV_FILES="metrics/perf-on/events.csv metrics/perf-off/events.csv" \
  PERF_PLOT_DIR=metrics/perf-plots
```

Los alias `profile`, `perfilado`, `profile_on` y `profile_off` llaman al mismo perfilado
formal. `setup_perf` sólo comprueba; `profile_fast` es diagnóstico; `profile_plot` sólo
lee CSV/metadatos. Cada captura exige un directorio nuevo para conservar los datos.
Para un subconjunto, usar `PERF_MODELS=seq,fine,coarse`; siempre se incluye secuencial.
Para cambiar la imagen, usar el mismo `INPUT=...` en ambos estados. También deben
coincidir `RANGE`, `STRIDE`, `GRAY`, `QUANTUM` y las configuraciones comparadas.

**Importante sobre las 200 ejecuciones:** se hacen 200 repeticiones **por modelo y por
grupo de contadores**. Se separan seis grupos para evitar pedir todos los eventos PMU
al mismo tiempo y se agrupan las parejas necesarias para los cocientes. Con cinco
modelos y todos los grupos disponibles son **6.000 compresiones medidas por estado**,
12.000 para ON+OFF, además de sondeos, referencia y calentamientos. No son sólo 200
compresiones totales. `WARMUPS=1` y `COOLDOWN=0.15` son los valores predeterminados;
`PERF_TIMEOUT=600` limita cada invocación. Una campaña formal rechaza `PERF_RUNS<200`.

## Eventos, fórmulas y equivalencia con las referencias

| Grupo | Eventos solicitados | Métricas |
|---|---|---|
| CPU | `cycles`, `instructions` | Ciclos, instrucciones, IPC = instrucciones/ciclos |
| Caché genérica | `cache-references`, `cache-misses` | Referencias, fallos, porcentaje de fallos |
| L1 datos | `L1-dcache-loads`, `L1-dcache-load-misses` | Cargas, fallos, porcentaje de fallos L1 |
| LLC | `LLC-loads`, `LLC-load-misses` | Cargas, fallos, porcentaje de fallos LLC |
| Predicción | `branches`, `branch-misses` | Saltos, fallos, porcentaje de fallos |
| Software/tiempo | `task-clock`, `context-switches`, `cpu-migrations`, `page-faults`, `duration_time` | CPU efectivas, contexto del SO, migraciones, fallos de página, duración |

Se incluyen los eventos de RayTracing y, de gray-scott, `task-clock` y las CPU efectivas.
`duration_time` proporciona la duración de la misma pasada: CPU efectivas =
`task-clock_ms / (duration_time_ns / 1e6)`. No se divide por el tiempo del compresor ni
por el tiempo que Python tarda en lanzar Perf. Si el kernel no admite `duration_time`,
las CPU efectivas quedan N/D; se conservan los demás eventos disponibles.

Los porcentajes se calculan por repetición, como 100 × fallos/referencias o cargas.
Después se calcula la media de esos cocientes, desviación estándar muestral e IC del
95% Student-t. Los intervalos de una sola muestra quedan vacíos. No se eliminan outliers.
Un denominador cero produce N/D. Cero eventos realmente contados sí es un valor válido.
Los intervalos de las tasas no se fuerzan artificialmente al rango 0–100.

Perf mide **el comando completo** y sus hilos: arranque, lectura, compresión, escritura
del FRC y reportes. No mide sólo `encode`. El `native_time_ms` guardado en cada fila
se refiere únicamente al algoritmo y puede consultarse por separado. Para tiempo del
algoritmo, speedup y eficiencia se mantienen los CSV/figuras de la
[etapa 7](benchmarking.md); sus tiempos no se mezclan con las pasadas instrumentadas.
Los contadores cooperativos de Fine/Coarse no son cambios de contexto del SO.

La salida conserva valores escalados por Perf y el runtime/porcentaje efectivo de cada
evento. Se excluyen del cálculo eventos con ejecución inferior a `PERF_MIN_RUNNING=90`
por ciento, valores inválidos o no contados, sin borrarlos del CSV. El archivo `.stat`
original queda disponible. No hay sustitución de LLC por caché genérica ni de un evento
no soportado por cero. Los estados de disponibilidad/errores se conservan para auditar.
La sintaxis y alcance por proceso siguen el
[manual de perf stat](https://github.com/torvalds/linux/blob/master/tools/perf/Documentation/perf-stat.txt).

## Archivos generados

Cada `metrics/perf-on/` o `metrics/perf-off/` contiene:

- `events.csv`: formato largo, una fila por evento/repetición/modelo, incluso si no está
  disponible; cuenta, unidad, runtime, estado, grupo, configuración y hash del FRC.
- `metadata.json`, `availability.json`, `commands.jsonl`: estado real, topología,
  configuración, versiones, hashes, comandos, salidas y errores.
- `probes/*.stat` y carpetas por modelo: salida cruda de cada pasada, último FRC,
  reporte de tiempo del algoritmo y afinidad para CMP/SMT. Cada FRC se compara por hash
  con una referencia secuencial antes de aceptar la pasada.
- `statistics/`: `summary.csv`, `metric_statistics.csv`, `coverage.csv`, `analysis.json`.

`profile_plot` escribe esas mismas tablas y:

| Figura | Referencia cubierta |
|---|---|
| `10_perf_ipc.png` | IPC por modelo (RayTracing) |
| `11_perf_cache_miss_rates.png` | L1, LLC y caché genérica |
| `12_perf_context_switches.png` | Contexto del SO, migraciones y fallos de página |
| `13_perf_dashboard.png` | IPC, L1, predicción y contexto (RayTracing) |
| `14_perf_cpu_utilization.png` | IPC, CPU efectivas, caché y ciclos (gray-scott) |

Las tablas incluyen también instrucciones, cargas, saltos y todos los contadores en
valores absolutos. Las barras muestran IC cuando hay suficientes muestras; las ausencias
se anotan N/D. Si se proporcionan ON+OFF compatibles se añade `smt_comparison.csv` con
medias y cocientes OFF/ON para la misma configuración. Un cociente de contadores no es
un speedup. La comparación SMT principal conserva 4×4 trabajadores en ambos estados.

El análisis rechaza campañas de máquinas, imágenes, binarios, kernels o parámetros
incompatibles, filas duplicadas y CSV incompletos. Los metadatos deben acompañar al CSV.
Para inspeccionar una captura interrumpida, ejecutar explícitamente:

```bash
.venv/bin/python scripts/profile_metrics.py \
  --csv metrics/perf-on/events.csv --output-dir metrics/perf-partial --allow-partial
```

Se etiqueta INCOMPLETO y no cuenta como campaña formal validada. Un análisis completo
con eventos no soportados sigue mostrando N/D y `all_events_measured=false`.

## Validación y campaña final

```bash
make test_profiles BUILD_DIR=build-evaluation PYTHON=.venv/bin/python
```

Las pruebas de parser/análisis usan casos sintéticos rotulados y no requieren Perf.
La prueba `perf_hardware_tests` ejecuta un diagnóstico de una muestra secuencial sólo
si la máquina y los permisos lo permiten; de otro modo CTest la muestra omitida.
No sustituye la campaña completa de cinco modelos en los dos estados.

Para obtener **todas** las figuras del proyecto se necesitan las campañas y `make plot`
de la [etapa 7](benchmarking.md), más estas dos capturas Perf y `make profile_plot`.
Etapa 8 aporta contadores microarquitectónicos; no reemplaza los benchmarks de tiempo,
speedup, eficiencia o escalabilidad.
