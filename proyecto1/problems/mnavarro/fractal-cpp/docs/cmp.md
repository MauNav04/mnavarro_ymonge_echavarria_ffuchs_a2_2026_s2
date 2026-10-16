# CMP — Versión de evaluación

`CMP/main.cpp` construye `fractal_cmp`, la única versión CMP. Está destinada a Ubuntu
nativo con AMD Ryzen, 8 núcleos físicos y 16 CPU lógicas con SMT activo (8 con SMT
inactivo). El usuario confirmó la funcionalidad de todos los modelos en la máquina
de evaluación el 2026-10-16 y solicitó retirar la variante local.

## Topología y afinidad

La selección exige el destino de evaluación: rechaza WSL, hipervisores/contenedores
reconocidos, otras distribuciones/familias de CPU y topologías fuera del contrato.
La ausencia de señales de virtualización no demuestra por sí sola hardware físico.
El modelo exacto de Ryzen y la versión de Ubuntu siguen pendientes de registrar.

Se adapta el reparto contiguo y afinidad de [RayTracing CMP](../../../echavarria/RayTracing/CMP/main.cpp).
Sysfs proporciona CPU online, `physical_package_id` y `core_id`; `sched_getaffinity`
limita los representantes permitidos. Se selecciona una sola CPU lógica por núcleo:
hasta 8 trabajadores y nunca dos hermanos SMT para suplir núcleos faltantes.
No se asumen IDs pares/impares ni IDs de núcleo únicos entre sockets.

Cada pthread fija afinidad y verifica una máscara de exactamente una CPU con
`pthread_getaffinity_np`; `sched_getcpu` debe coincidir antes y después del trabajo.
Los errores cancelan el cálculo. CMP no activa/desactiva SMT. No hace falta desactivarlo
para seleccionar un único trabajador por núcleo; la comparación ON/OFF pertenece a SMT.

## Motor compartido

`src/parallel_encoder.cpp` comparte con SMT la búsqueda y el pool de pthreads.
Los equipos reciben ranges contiguos equilibrados y sus miembros dividen los dominios.
Cada miembro publica su mínimo en un slot privado; el líder reduce conservando el
orden dominio/simetría secuencial y escribe exclusivamente sus reglas.

Tres barreras cancelables por range protegen preparación, mínimos y reutilización.
No existe turno global, cuantum ni stall cooperativo durante la búsqueda. Miembros
sin dominios participan con candidato neutro; equipos sin ranges finalizan.
La barrera inicial espera afinidad verificada; ante errores se cancelan las barreras
y se reúnen todos los pthreads creados. Los diagnósticos se agregan tras los joins.
Canales procesados en orden, un pool por canal; reconstrucción secuencial.

## Compilar y ejecutar

Desde `fractal-cpp/`, en la computadora de evaluación:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_APP=ON
cmake --build build --target fractal_cmp fractal_seq -j2
./build/fractal_cmp --list-cpus
mkdir -p CMP/results
./build/fractal_cmp compress sequential/examples/color128.png CMP/results/color.frc --teams 4 --threads-per-team 2 --report-affinity CMP/results/affinity.csv
./build/fractal_seq decompress CMP/results/color.frc CMP/results/color.png 80
./build/fractal_seq compare sequential/examples/color128.png CMP/results/color.png
```

Se puede configurar desde `CMP/` con `cmake -S CMP -B build-cmp`. `--teams` por
`--threads-per-team` debe caber en los núcleos permitidos. Las máscaras restringidas
se respetan sin relajar la comprobación de la topología completa del equipo.
`--report-affinity` guarda canal, trabajador, equipo, CPU/socket/core y CPU observada.
La columna `profile=target` se conserva por compatibilidad de los registros; no existe
un perfil alternativo. Se rechazan cuantums y trazas de planificación cooperativa.

## Validación

`cmp_tests` comprueba contratos mediante fixtures: 8/16, SMT inactivo, máscaras
restringidas, varios sockets y destinos incompatibles. `cmp_hardware_tests` compara
reglas y reconstrucciones gris/RGB con secuencial, comprueba afinidad, equipos vacíos
y miembros sin dominios. Se omite con código 77 fuera del destino.

```bash
cmake --build build -j2
ctest --test-dir build -R 'cmp|cli' --output-on-failure
```

La confirmación funcional del usuario no se presenta como una ejecución del asistente.
Escalabilidad y mediciones estadísticas siguen pendientes de documentar. Las pruebas
WSL de contratos no sustituyen mediciones físicas. El historial del README conserva
el contexto de la variante anterior; sus comandos ya no están vigentes.
