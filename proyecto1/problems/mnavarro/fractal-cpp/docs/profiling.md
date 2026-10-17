# Etapa 8: Linux Perf en el Ryzen de evaluación

La [guía automática](running.md) integra Perf con benchmark, escalabilidad,
gráficas y ZIP mediante `make run`; `make quick` hace el diagnóstico completo.
Las recetas de esta página permiten ejecutar cada parte por separado.

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
funcionar antes de continuar. Los motores individuales no instalan paquetes, no
ejecutan sudo ni cambian SMT. El orquestador `make run` sí automatiza ON/OFF/ON
mediante un ayudante privilegiado; véase la [guía automática](running.md).

Con SMT **realmente activo**, comprobar acceso y eventos disponibles:

```bash
make setup_perf BUILD_DIR=build-evaluation PYTHON=.venv/bin/python SMT_STATE=on
```

Guarda `metrics/perf-check-on/availability.json`, metadatos, sondeos en `perf-stat.jsonl` y
`commands.jsonl`. Cada evento tiene un estado explícito. Este comando sondea eventos;
no ejecuta la campaña de 20 muestras ni genera gráficos. Si se repite, elegir otro
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

## Diagnóstico breve antes de las 20 repeticiones

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
  SMT_STATE=on PERF_RUNS=20 PERF_DIR=metrics/perf-on
```

**2. Desactivar SMT manualmente** según el protocolo de evaluación. Si hay reinicio,
volver a la misma carpeta. Confirmar el estado con `make topology SMT_STATE=off
BUILD_DIR=build-evaluation`, y ejecutar:

```bash
make profile_perf BUILD_DIR=build-evaluation PYTHON=.venv/bin/python \
  SMT_STATE=off PERF_RUNS=20 PERF_DIR=metrics/perf-off
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

**Importante sobre las 20 ejecuciones:** se hacen 20 repeticiones **por modelo y por
grupo de contadores**. Se separan seis grupos para evitar pedir todos los eventos PMU
al mismo tiempo y se agrupan las parejas necesarias para los cocientes. Con cinco
modelos y todos los grupos disponibles son **600 compresiones medidas por estado**,
1.200 para ON+OFF, además de sondeos, referencia y calentamientos. No son sólo 20
compresiones totales. `WARMUPS=1` y `COOLDOWN=0.15` son los valores predeterminados;
`PERF_TIMEOUT=600` limita cada invocación. Se acepta `PERF_RUNS=15` como mínimo;
valores inferiores necesitan diagnóstico. Benchmark y escalabilidad conservan sus
200 repeticiones. Las capturas anteriores de 200 siguen siendo válidas para el análisis.

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
por ciento, valores inválidos o no contados, sin borrarlos del CSV. El texto original de
cada salida queda disponible en `perf-stat.jsonl`. No hay sustitución de LLC por caché genérica ni de un evento
no soportado por cero. Los estados de disponibilidad/errores se conservan para auditar.
La sintaxis y alcance por proceso siguen el
[manual de perf stat](https://github.com/torvalds/linux/blob/master/tools/perf/Documentation/perf-stat.txt).

## Archivos generados

Cada `metrics/perf-on/` o `metrics/perf-off/` contiene:

- `events.csv`: formato largo, una fila por evento/repetición/modelo, incluso si no está
  disponible; cuenta, unidad, runtime, estado, grupo, configuración y hash del FRC.
- `metadata.json`, `availability.json`, `commands.jsonl`: estado real, topología,
  configuración, versiones, hashes, comandos, salidas y errores.
- `perf-stat.jsonl`: un registro por sondeo/pasada con `path` y `text`, el contenido
  original íntegro de Perf. Se escribe y vacía tras cada pasada; evita miles de archivos
  `.stat`. El `.stat` temporal sólo se retira después de conservar su contenido.
- `probes/` y carpetas por modelo: último FRC,
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

## Compatibilidad de unidades y nuevas capturas en el Ryzen

El diagnóstico recibido de Ubuntu 25.10, Ryzen 7 7730U y Perf 6.17.13 emite
`task-clock` y `duration_time` como conteos crudos en nanosegundos con la columna de
unidad vacía. El lector los normaliza a ms y ns respectivamente; también acepta las
unidades explícitas de otras versiones. Una unidad explícita desconocida se rechaza.
Las dos unidades vacías se habían clasificado incorrectamente como `unknown_unit`.

Los eventos genéricos `LLC-loads` y `LLC-load-misses` no están disponibles en la
configuración recibida; continúan N/D. Esto no significa que el procesador carezca de
caché L3. No se sustituyen esos contadores por los de caché genérica.

Las capturas anteriores excluyeron los dos eventos de tiempo de las pasadas por modelo.
No se pueden recuperar las CPU efectivas de todos los modelos a partir del sondeo
secuencial. Repetir únicamente el diagnóstico Perf en directorios nuevos:

```bash
make setup_perf PERF_CHECK_DIR=metrics/perf-check-on-v2 SMT_STATE=on
make profile_fast PERF_FAST_DIR=metrics/perf-diagnostic-on-v2 SMT_STATE=on
make profile_plot PERF_CSV_FILES="metrics/perf-diagnostic-on-v2/events.csv" PERF_PLOT_DIR=metrics/perf-check-plots-v2
```

Estos ejemplos usan `build` y el Python activo; activar antes `.venv` o indicar
`PYTHON=.venv/bin/python`. Los datos originales se conservan para auditar la corrección.

## Cantidad de archivos y exportación completa

Perf utiliza `PERF_RAW_STORAGE=jsonl` por defecto. Para conservar la disposición anterior,
ejecutar una campaña nueva con `PERF_RAW_STORAGE=files` (`--raw-storage files` en Python).
CSV, estadísticas, FRC y afinidad conservan su organización. Un lector del registro crudo:

```python
import json
with open("metrics/perf-diagnostic-on-v2/perf-stat.jsonl") as stream:
    for line in stream:
        record = json.loads(line)
        print(record["path"])
        print(record["text"])
```

Después de terminar las campañas, exportar todos sus archivos, incluidos los CSV:

```bash
make archive_metrics
```

Genera `results/exports/metrics.zip`, comprueba su integridad y conserva los originales.
Si ya existe, elegir otro nombre con `METRICS_ARCHIVE=results/exports/metrics-v2.zip`.
El ZIP debe quedar fuera de `METRICS_DIR`; no necesita compilar ni instalar dependencias.
Puede extraerse para recuperar `metrics/` con su estructura y volver a ejecutar los análisis.
Si no encuentra CSV avisa que la exportación no permite reproducir el análisis completo.

La regla global `*.csv` de Git excluía esos archivos al compartir el repositorio. Se
añadió una excepción limitada a `fractal-cpp/metrics/`; incluir los CSV en el commit o
transferir el ZIP completo. Los JSON y PNG por sí solos no sustituyen los datos originales.

Los archivos de `build/` son el caché, objetos, dependencias, Makefiles generados y pruebas
de CMake. En la revisión local había 323 archivos y unos 4,9 MB. No se crean otros 323
por cada corrida. Mantener un único build y compartir los resultados mediante el ZIP;
no hace falta copiar el build a la otra computadora ni borrar internamente sus archivos.

Revisión detallada de los datos recibidos: [ryzen_diagnostics.md](ryzen_diagnostics.md).

## Campañas anteriores con miles de .stat

Cinco modelos × cinco grupos disponibles × 200 repeticiones producen 5.000 `.stat`
en el formato anterior, además de calentamientos y sondeos. No son miles de imágenes
ni un fallo de las 200 repeticiones: es un archivo por pasada instrumentada.

La corrección JSONL debe estar en los scripts de la computadora Ryzen, además de esta
copia del proyecto. Comprobar que `python3 scripts/profile.py --help` muestra
`--raw-storage`. En capturas nuevas el valor predeterminado es JSONL; puede hacerse
explícito con `PERF_RAW_STORAGE=jsonl`. Una versión antigua del Makefile no utiliza
esa variable aunque se escriba en el comando.

Para reducir los archivos de una captura antigua ya finalizada, sin repetirla:

```bash
make compact_perf PERF_DIR=metrics/perf-on REMOVE_STAT_FILES=1
```

El comando exige `success=true`, conserva el texto exacto de cada `.stat` en un único
`perf-stat.jsonl`, verifica cada copia por SHA-256 y crea `perf-compaction.json` antes
de retirar los `.stat`. No altera CSV, FRC, gráficas, metadatos ni valores medidos.
Rechaza capturas fallidas y no sobrescribe un JSONL o manifiesto previo.

Si sólo se desea copiar y verificar, usar `make compact_perf PERF_DIR=metrics/perf-on`
sin `REMOVE_STAT_FILES=1`: en ese caso los `.stat` permanecen. Elegir una de las dos
modalidades desde el principio; la segunda ejecución sobre el mismo destino no
sobrescribe la primera. Las capturas que ya usan JSONL no necesitan compactarse.

La compactación cambia almacenamiento, no corrige contadores que no se capturaron.
Si los eventos de tiempo fueron excluidos por el lector antiguo, CPU efectivas seguirá
N/D en esa campaña. Conservar su registro de disponibilidad al interpretar las figuras.

## Las figuras 01–09 y 10–14 pertenecen a conjuntos distintos

`setup_perf` y su carpeta `perf-check-on` sólo sondean disponibilidad. `profile_plot`
genera las cinco figuras 10–14 en el directorio indicado por `PERF_PLOT_DIR`.
Las figuras 01–09 se generan con `plot`, usando los CSV de benchmarking y escalabilidad;
la 06 requiere el CSV del barrido de hilos. No deben buscarse todas en la carpeta de Perf.

Los tres comandos `benchmark_full`, `scalability` y `profile_perf` recolectan datos y
tablas; después deben ejecutarse los comandos de gráficos. Para las campañas ON que
el usuario ya ejecutó, generar ambos conjuntos en una sola llamada:

```bash
make plot_all \
  CSV_FILES="metrics/final-on/raw.csv metrics/scaling-final-on/raw.csv" \
  PLOT_DIR=metrics/final-plots-on/benchmark \
  PERF_CSV_FILES="metrics/perf-on/events.csv" \
  PERF_PLOT_DIR=metrics/final-plots-on/perf
```

Así quedan las nueve figuras de rendimiento en `benchmark/` y las cinco de Perf en
`perf/`, debajo de una misma carpeta principal. Mantener los directorios distintos:
ambos análisis generan `summary.csv` y `analysis.json` con contenidos diferentes.
El comando sólo lee CSV y metadatos; no compila ni ejecuta de nuevo la compresión.
Usar `perf-on/events.csv` para el perfilado completo, no `perf-diagnostic-on/events.csv`.
La carpeta de salida por sí sola no determina qué campaña se está graficando.

Cuando existan las campañas OFF, añadir sus CSV a las listas correspondientes. Los
CSV y sus `metadata.json` deben acompañarse para poder validar compatibilidad.
