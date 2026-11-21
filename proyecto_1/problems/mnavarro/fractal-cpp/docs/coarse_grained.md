# Coarse-Grained: cesión por estancamiento de búsqueda

## Política aprobada

Cada equipo procesa un range; sus pthreads se reparten dominios y mantienen mínimos locales.
El hilo con turno continúa en una ráfaga, comparando las ocho simetrías de cada dominio.
Si mejora estrictamente su menor error, reinicia la racha; los empates cuentan como no mejora.

Con `D` dominios asignados a ese hilo para el range, el stall se activa al alcanzar
`floor(2*D/3)+1` dominios consecutivos sin mejora, siempre que quede trabajo pendiente.
Ejemplo: D=45 implica una racha de 31. El denominador no es el catálogo global ni la cantidad
restante: es la asignación original del miembro para ese range. La racha se reinicia después
del stall y al comenzar otro range; el mínimo y la posición se conservan para reanudar.

Si termina el intervalo, registra `domain_complete` y cede para esperar los mínimos de su equipo.
La finalización tiene prioridad sobre el stall; no se cuentan stalls sin trabajo por reanudar.
El último miembro publica una regla usando el desempate secuencial dominio/simetría y habilita
el siguiente range. Intervalos vacíos y equipos sin ranges no reciben turnos de búsqueda.
Todos los dominios se comparan exactamente una vez; no se descartan candidatos ni ranges.

La elección del próximo trabajador listo sigue orden circular global. Si no hay otro listo,
el actual reanuda: un stall registrado no implica necesariamente cambio de trabajador.
Al superar dos tercios de la asignación original, hay como máximo un stall por miembro y range.

## Diferencia con Fine-Grained y referencia

Se toma de [RayTracing](../../../echavarria/RayTracing/coarse-grained/main.cpp) la organización
con main propio, pthreads duraderos, bloques contiguos y ráfagas con eventos de stall.
RayTracing cuenta rebotes profundos; aquí se implementa una cesión explícita y comprobable
cuando ocurre la condición aprobada. Es una heurística de estancamiento de búsqueda, no
una medición de stalls de CPU o caché. Incluso un mínimo perfecto puede dejar de mejorar.

Fine-Grained cede cada Q dominios independientemente del error. Coarse-Grained no tiene
cuantum: la duración de cada ráfaga depende de las mejoras y de la finalización del intervalo.
`--quantum-domains` se rechaza en `fractal_coarse`.

`src/cooperative_encoder.cpp` comparte el mecanismo de equipos, mutex, condición por trabajador,
compuerta de inicio, cancelación y joins entre ambas políticas, conservando su diferencia.
Sólo un hilo realiza búsqueda a la vez; los demás esperan sin sondeo. Esta política
se diferencia de la búsqueda concurrente de CMP y SMT, disponibles en sus ejecutables.
Cada main delega en `app/cli.cpp`. La reconstrucción permanece en `fractal_seq`.

## Uso y traza

Desde `fractal-cpp/`, tras compilar:

```bash
mkdir -p coarse-grained/results
./build/fractal_coarse compress assets/color128.png coarse-grained/results/color.frc --teams 5 --threads-per-team 2
./build/fractal_seq decompress coarse-grained/results/color.frc coarse-grained/results/color.png 80
./build/fractal_seq compare assets/color128.png coarse-grained/results/color.png
```

Agregar `--trace-schedule coarse-grained/results/schedule.csv` a la compresión permite examinar
una fila por ráfaga: `channel,worker,team,range,domain_begin,domain_end,reason,stagnant_domains,stall_threshold`.
El intervalo es semiabierto. `reason` es `stall` o `domain_complete`; la racha se registra antes
de reiniciarla. La traza Fine-Grained conserva sus seis columnas anteriores. Registrar eventos
añade memoria y tiempo de cómputo, por lo que debe desactivarse en benchmarks principales.

## Validación

`coarse_grained_tests` reproduce la política dominio por dominio desde los errores reales,
verifica la primera oportunidad de stall, reanudación, cobertura completa sin duplicados,
orden circular y espera del equipo antes de cambiar de range. Incluye constantes con empates,
patrones, mejoras que reinician la racha, intervalos pequeños/vacíos, equipos vacíos y RGB.
Compara reglas campo por campo y reconstrucción exacta con la base; CTest limita la prueba
a 45 segundos. Los resultados medidos se registran en el README principal.
