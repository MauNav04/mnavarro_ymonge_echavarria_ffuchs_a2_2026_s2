# CMP: variante local y variante de evaluación

## Destinos separados y núcleo único

- `fractal_cmp_local`, entrada `CMP/local/main.cpp`: pruebas funcionales en Linux/WSL2,
  con núcleos y máscara de afinidad reportados por el kernel del entorno actual.
- `fractal_cmp`, entrada `CMP/main.cpp`: evaluación en Ubuntu nativo con AMD Ryzen, perfil de referencia
  de RayTracing de 8 núcleos físicos y 16 CPUs lógicas con SMT activo (8 con SMT inactivo).
  Rechaza WSL, hipervisores/contenedores detectados, otra distribución/familia de CPU y topologías distintas del contrato.

Los scripts [Makefile](../../../echavarria/RayTracing/scripts/Makefile) y
[benchmark.py](../../../echavarria/RayTracing/scripts/benchmark.py) documentan ese perfil;
el usuario confirmó Ubuntu y AMD Ryzen (posiblemente Ryzen 7, aún sin modelo exacto). El perfil se verifica por topología, sin fijar identificadores CPU de esta laptop.
La ausencia de una señal de virtualización no certifica por sí sola hardware físico.

Ambas variantes ejecutan `CMP/encoder.cpp` y el mismo codec. La diferencia es el contrato
del entorno de ejecución, sin duplicar ni cambiar el algoritmo matemático. El modelo y
perfil se fijan en cada main; `--model` no permite cambiar a otro modelo y no existe un
flag que convierta silenciosamente la variante de evaluación en local.

## Topología y afinidad

Se adopta de [RayTracing CMP](../../../echavarria/RayTracing/CMP/main.cpp) el reparto contiguo,
pthreads duraderos y afinidad por núcleo. La detección lee CPUs online y sysfs, agrupa por
`(physical_package_id, core_id)` y selecciona un representante permitido por núcleo según
`sched_getaffinity`. No supone que las CPUs pares sean físicas ni que core_id sea único
entre sockets. No rellena con hermanos SMT si se piden demasiados trabajadores.

Cada pthread fija afinidad con `pthread_setaffinity_np` y verifica con
`pthread_getaffinity_np` una máscara de exactamente una CPU. `sched_getcpu` debe coincidir
antes y después del trabajo. Cualquier error cancela el cálculo: no se guarda como CMP
verificado un resultado obtenido sin la afinidad solicitada.

El total de trabajadores es `teams * threads_per_team` y debe caber en los núcleos permitidos.
El perfil target verifica los 8 núcleos de la topología online completa aunque el proceso
esté restringido a un subconjunto de CPUs; el reparto respeta ese subconjunto.

## Equipos y memoria compartida

Se conservan los equipos por range. Cada equipo recibe ranges contiguos equilibrados y
cada miembro recibe dominios contiguos de su range. El catálogo es de solo lectura.
Cada hilo guarda su candidato en un slot exclusivo; el líder combina mínimos con el
orden dominio/simetría original y publica en un índice exclusivo de `rules`.

Tres barreras cancelables por range protegen preparación del target, entrega de mínimos
y publicación/reutilización de buffers. Fuera de esas barreras, todos los miembros y equipos
pueden buscar simultáneamente: no hay turno global, cuantum ni mecanismo de stall.
Miembros sin dominios siguen participando con candidato neutro; equipos sin ranges finalizan.
Los canales se procesan en orden con un pool por canal. La reconstrucción continúa secuencial.

Una barrera inicial abre el cálculo solo tras verificar afinidad de todos los trabajadores.
Los errores de creación, afinidad o cómputo cancelan todas las barreras y se reúnen los
pthreads creados antes de propagar el error. Los vectores de diagnóstico se añaden tras los
joins: no hay `push_back` concurrente sobre el vector del llamador.

## Compilar y ejecutar

Desde `fractal-cpp/`:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_APP=ON
cmake --build build --target fractal_cmp_local fractal_cmp fractal_seq -j2
./build/fractal_cmp_local --list-cpus
mkdir -p CMP/local/results
./build/fractal_cmp_local compress sequential/examples/color128.png CMP/local/results/color.frc --teams 2 --threads-per-team 2 --report-affinity CMP/local/results/affinity.csv
./build/fractal_seq decompress CMP/local/results/color.frc CMP/local/results/color.png 80
./build/fractal_seq compare sequential/examples/color128.png CMP/local/results/color.png
```

La configuración local 2×2 requiere cuatro núcleos permitidos; usar 1×1 si solo hay uno.
En la computadora de evaluación, sin modificar los IDs de CPU en el código:

```bash
./build/fractal_cmp --list-cpus
mkdir -p CMP/results
./build/fractal_cmp compress sequential/examples/color128.png CMP/results/color.frc --teams 4 --threads-per-team 2 --report-affinity CMP/results/affinity.csv
./build/fractal_seq decompress CMP/results/color.frc CMP/results/color.png 80
./build/fractal_seq compare sequential/examples/color128.png CMP/results/color.png
```

`--report-affinity` es opcional. Registra perfil, canal, trabajador, equipo, CPU/socket/core,
y CPU observada antes/después; no representa ciclos ni contadores PMU. La variante CMP
rechaza `--quantum-domains` y `--trace-schedule` para no sugerir ejecución cooperativa.
La biblioteca se puede compilar y probar sin OpenCV usando `BUILD_APP=OFF`.

## Validación y etapas futuras

Las pruebas locales comparan reglas exactas y reconstrucciones gris/RGB, verifican núcleos
distintos y afinidad efectiva con varios tamaños de equipos, miembros sin dominios y equipos
vacíos. Las fixtures 8/16, SMT inactivo y varios sockets verifican el contrato de selección;
no equivalen a haber ejecutado el perfil target en su hardware.

La ejecución física y escalabilidad de la variante de evaluación permanecen pendientes hasta
correr en la computadora destino. Las pruebas WSL son funcionales; no se usan como benchmarks
principales ni se afirma que sus vCPU representen los núcleos físicos de la laptop.

**Destino de las etapas 5 y 8:** SMT y Linux Perf deben implementarse pensando en esa misma
computadora Linux de evaluación; sus resultados físicos no se sustituyen por simulaciones
locales. Aquí se prepara la detección y el contrato de hardware, sin implementar esas etapas
ni cambiar el estado SMT, los permisos del kernel o los contadores Perf.
