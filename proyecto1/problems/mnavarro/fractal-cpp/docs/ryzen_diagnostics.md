# Revisión del diagnóstico recibido del Ryzen — 2026-10-16

Se revisan los archivos aportados en `metrics/`. Las ejecuciones físicas fueron
realizadas por el usuario. Reprocesar sus logs y salidas crudas localmente no es una
nueva medición física. La revisión completa de CSV y afinidad queda pendiente porque
esos archivos no están en la copia recibida.

## Entorno y alcance comprobados

- AMD Ryzen 7 7730U with Radeon Graphics; Ubuntu 25.10; kernel 6.17.0-41-generic;
  Perf 6.17.13; 8 núcleos físicos, 16 CPU lógicas, SMT activo y máscara 0–15.
- Compilación Release, `-O3 -DNDEBUG`; imagen `color128.png`, 128×128 RGB,
  range=8, stride=8, Fine con Q=1; 4×2 trabajadores en Fine/Coarse/CMP y 4×4 en SMT.
- `check-on`: 5 muestras y 1 calentamiento por modelo, 25 mediciones en total;
  `success=true` y `state_verified=true`. Sus 33 comandos terminaron sin errores/timeouts.
- `perf-diagnostic-on`: 1 muestra y 1 calentamiento por grupo; 25 invocaciones medidas
  y 75 filas declaradas, `success=true`. Hubo cinco grupos activos; LLC fue excluido.
  De 67 comandos registrados, sólo falló el sondeo de la pareja LLC, con el error
  esperado de evento no soportado; no hubo timeouts.
- `perf-check-on`: sondeo, no campaña. El campo `runs=200` es el valor predeterminado
  del argumento; `check_only=true` confirma que no ejecutó las 200 repeticiones.
- `check-on` registra governor `powersave` y boost activo. Conservar y describir esas
  condiciones en la campaña formal; no se modificaron parámetros del equipo.

## Corrección funcional y tiempos observables

Los cinco FRC finales de `check-on` coinciden por SHA-256 con su referencia secuencial;
lo mismo ocurre con los cinco FRC finales de `perf-diagnostic-on`. Los hashes de ambas
referencias coinciden con sus metadatos. En los logs de 12 ejecuciones CMP/SMT del
benchmark y 20 del perfilado se observan respectivamente 8/16 trabajadores y CPU únicas.
Los CSV de afinidad originales faltan, por lo que no se revalidaron sus columnas
`observed_before/after`; las ejecuciones originales los verificaban antes de aceptar cada pasada.

Medias aproximadas de las cinco muestras del benchmark, excluyendo el calentamiento,
calculadas desde el texto de consola conservado:

| Modelo | Media aproximada (ms) | CV aproximado (%) |
|---|---:|---:|
| Secuencial | 288,00 | 1,43 |
| Fine-Grained | 1008,28 | 0,80 |
| Coarse-Grained | 341,98 | 2,48 |
| CMP | 53,42 | 1,29 |
| SMT ON | 51,84 | 3,17 |

El texto tiene menos precisión que los CSV y no reemplaza su análisis oficial. Los
valores coinciden visualmente con el dashboard recibido. Fine/Coarse pueden ser más
lentos que secuencial por la planificación cooperativa; sólo un hilo busca a la vez.
CMP y SMT son concurrentes y presentan alrededor de 5,4×/5,6× de speedup en este
diagnóstico. Las cinco muestras y esa diferencia pequeña entre CMP y SMT no permiten
concluir una ventaja general del SMT ni su efecto ON/OFF. Sus presupuestos de trabajadores
son distintos y no hay campaña OFF en los archivos recibidos.

## Revisión de Perf y figuras

Los IPC de las pasadas CPU medidas, recalculados desde los `.stat` recibidos, son
aproximadamente 2,630 (secuencial), 2,280 (CMP), 1,197 (Fine), 1,888 (SMT) y 2,481
(Coarse). Las parejas se contaron en la misma pasada. En los 50 archivos de pasadas
conservados, los 110 eventos efectivamente incluidos tienen estado `ok` y runtime
suficiente. Los ciclos agregan trabajo de los hilos y no equivalen al tiempo de pared.

Se leyeron las 13 figuras disponibles: ocho de benchmarking y cinco de Perf. Se
inspeccionaron ambos dashboards y una vista conjunta de todas ellas. La figura de
Amdahl/escalabilidad no se generó porque esta entrega no contiene un barrido de hilos;
las gráficas de convergencia con cinco puntos no validan convergencia de 200 corridas.
Las figuras Perf con una sola muestra no tienen intervalos de confianza estimables.

Se encontraron dos tipos distintos de N/D:

1. LLC: el sondeo devuelve `<not supported>` para los dos eventos genéricos. Es una
   limitación de disponibilidad de esa configuración, no ausencia de caché L3. Se mantiene N/D.
2. Tiempo/CPU efectivas: el lector rechazaba la unidad vacía de `task-clock` y
   `duration_time`. En el sondeo real son 327532597 ns y 332559518 ns; equivalen a
   327,532597 ms y unas 0,985 CPU efectivas. El lector corregido reproduce ese resultado.

La sintaxis CSV de Perf conserva la unidad de cada evento cuando existe; véase el
[formateador oficial de Perf 6.17](https://github.com/torvalds/linux/blob/v6.17/tools/perf/util/stat-display.c).
El formato crudo recibido de estos dos eventos temporales usa nanosegundos.
La corrección incluye pruebas con sus líneas originales y con unidades explícitas.
Unidades explícitas desconocidas siguen rechazadas.

La campaña antigua excluyó esos dos eventos después del sondeo, por lo que faltan en
las pasadas software de cada modelo. No se pueden reconstruir las CPU efectivas de
CMP/SMT/Fine/Coarse usando el sondeo secuencial. Repetir `setup_perf` y `profile_fast`
con el lector corregido y directorios nuevos; no sobrescribir los datos originales.

## Cantidad de archivos y datos faltantes

La copia recibida contiene 116 archivos (aproximadamente 3,8 MB): 76 `.stat`, 15 FRC,
13 PNG, 9 JSON y 3 JSONL. No contiene ningún CSV. Faltan, entre otros, `raw.csv`,
`events.csv`, las tablas estadísticas y los CSV nativos/afinidad. La regla global
`*.csv` de `.gitignore` los excluía; la excepción de este módulo queda corregida.

Los metadatos declaran análisis completos y los PNG son coherentes con los registros,
pero no se puede verificar cada fila, sus hashes ni reproducir todas las estadísticas
sin los CSV. No se inventaron CSV desde textos redondeados ni se modificaron los artefactos
originales. Enviar el ZIP completo creado en el Ryzen mediante `make archive_metrics`.

Las nuevas capturas guardan las salidas crudas en un único `perf-stat.jsonl`, con
texto íntegro y ruta por pasada, en lugar de generar un `.stat` permanente por pasada.
CSV y metadatos siguen separados y listos para analizar. `PERF_RAW_STORAGE=files`
permite conservar el formato anterior. La exportación ZIP incluye todos los archivos,
incluso CSV ignorados por Git, y no elimina los originales.

En el build local había 323 archivos, unos 4,9 MB: son productos de compilación y
pruebas reutilizados, no resultados nuevos por cada repetición. No se alteró CMake ni
la arquitectura para reducir su cantidad.

## Pendientes antes de la campaña formal

- Recibir los CSV originales y completar la revisión fila por fila y de afinidad.
- Repetir el diagnóstico Perf corregido y comprobar que ambos eventos de tiempo son
  `ok` y que la figura de CPU efectivas tiene valores medidos.
- Ejecutar las 200 repeticiones, escalabilidad y ON/OFF con las mismas condiciones;
  estos diagnósticos no completan esos criterios de aceptación.
- Conservar el estado N/D de LLC y las condiciones de frecuencia al presentar resultados.

Los comandos de recaptura y exportación están en [profiling.md](profiling.md).
