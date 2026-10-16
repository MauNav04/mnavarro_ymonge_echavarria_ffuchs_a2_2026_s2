# SMT — Etapa 5

Destino: Ubuntu nativo, AMD Ryzen de 8 núcleos físicos y 16 hilos lógicos con SMT
habilitado. El modelo exacto sigue pendiente. La topología de WSL no valida este destino.

## Ejecución y referencia

Se adapta `../../echavarria/RayTracing/SMT/main.cpp`: mantener 16 pthreads tanto
con SMT activo (16 CPU lógicas) como inactivo (16 trabajadores sobre 8 núcleos).
Se conserva la arquitectura fractal aprobada: equipos por range, dominios divididos entre
miembros y reducción determinista de mínimos. No se asigna un range independiente a cada hilo.
`SMT/main.cpp` fija el modelo; `app/` comparte CLI/E/S y `src/parallel_encoder.cpp`
comparte con CMP el pool de pthreads, barreras cancelables, manejo de errores y búsqueda.
CMP mantiene un trabajador por núcleo distinto; SMT cambia la selección de CPU.

`--smt-state on|off` expresa el estado **esperado**, no modifica el sistema.
Se comprueban `/sys/devices/system/cpu/smt/control` y `smt/active`, CPU online,
`physical_package_id`, `core_id` y la máscara permitida por `sched_getaffinity`.
ON exige 8 pares de hermanos y 16 CPU online; OFF exige 8 núcleos con una CPU online
por núcleo. Estados desconocidos o inconsistentes, WSL, contenedores reconocidos y
hardware ajeno al perfil se rechazan. La detección de virtualización no prueba por sí
sola que una máquina sea física: confirmar Ubuntu nativo antes de medir.

ON selecciona primero un lógico de cada núcleo y luego sus hermanos, sin suponer
numeración par/impar. OFF repite la lista de núcleos para sobresuscribir por software.
Con 4 equipos × 4 trabajadores y máscara completa, ON usa 16 CPU distintas y OFF usa
8 CPU dos veces cada una. Se admiten 1–16 trabajadores respetando el presupuesto de la
máscara: ON hasta los lógicos permitidos; OFF hasta dos veces los núcleos permitidos.
La afinidad efectiva y CPU observada se verifican antes y después del cómputo.
Se vuelve a comprobar topología/estado al terminar. Ante errores se cancelan las barreras
y se reúnen todos los pthreads; no hay fallback silencioso ni espera activa global.

A diferencia del modo `sw` del ejemplo, fijar afinidad sobre núcleos físicos mientras
SMT sigue activo no se acepta como evidencia de SMT OFF. Es necesario desactivarlo
realmente mediante la intervención del usuario en BIOS/UEFI o la configuración Linux
del equipo, y comprobarlo antes de correr. Este código no cambia SMT ni necesita sudo.

## Prueba en la computadora de evaluación

Desde `fractal-cpp/`, instalar las dependencias habituales de C++/CMake, pthreads,
OpenCV 4 y Python3. Compilar una vez y conservar esos mismos binarios en ambas corridas:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_APP=ON
cmake --build build -j2
ctest --test-dir build --output-on-failure
./build/fractal_smt --list-cpus --smt-state on
python3 SMT/verify_smt.py run --state on --input assets/color128.png --output-dir SMT/results/on
```

Tras desactivar SMT realmente y reiniciar si corresponde, sin recompilar ni cambiar
entrada/parámetros, repetir en la misma máquina:

```bash
./build/fractal_smt --list-cpus --smt-state off
ctest --test-dir build -R smt_hardware_tests --output-on-failure
python3 SMT/verify_smt.py run --state off --input assets/color128.png --output-dir SMT/results/off
python3 SMT/verify_smt.py compare --on SMT/results/on --off SMT/results/off --output SMT/results/comparison.json
```

Restaurar el estado del equipo mediante intervención del usuario. El orden ON/OFF
puede invertirse. Usar directorios nuevos para repetir: el registrador evita sobrescribir
corridas y el comparador evita sobrescribir su resultado. La opción `--gray` permite
repetir el protocolo en escala de grises, usando otros directorios.

El registrador ejecuta secuencial y SMT, reconstruye ambos y exige FRC/PNG idénticos.
Guarda afinidad, diagnóstico, comandos con stdout/stderr (incluidos los tiempos
individuales ya emitidos por la CLI), hashes de entrada/binarios/artefactos y un hash del
identificador de máquina para comparar la misma computadora después del reinicio.
`run.json` se escribe solo tras validar la corrida. Los fallos conservan diagnósticos y
no producen un registro exitoso. `compare` exige estados opuestos, misma máquina,
entrada, binarios y parámetros, integridad de artefactos y resultados exactos.
Los hashes sirven para detectar cambios accidentales, no certifican mediciones externas.
No se generan estadísticas, speedup ni resultados Perf: pertenecen a etapas posteriores.

Ejecución manual equivalente:

```bash
mkdir -p SMT/results/manual
./build/fractal_smt compress assets/color128.png SMT/results/manual/color.frc --smt-state on --teams 4 --threads-per-team 4 --report-affinity SMT/results/manual/affinity.csv
./build/fractal_seq decompress SMT/results/manual/color.frc SMT/results/manual/color.png --iterations 80
```

También se admite configurar con `cmake -S SMT -B build-smt` y construir el target
`fractal_smt`. La reconstrucción continúa siendo secuencial. SMT no utiliza cuantums,
stalls cooperativos ni trazas de turnos. El CSV contiene una fila por trabajador y canal,
con CPU/socket/core, CPU observada al entrar/salir y estado SMT verificado.

## Validación y pendientes

Las pruebas `smt_tests` validan fixtures ON/OFF, numeración dispersa, máscaras restringidas,
estados desconocidos y destinos incompatibles. No constituyen ejecución física SMT.
`cmp_hardware_tests` valida el motor compartido contra secuencial en el destino;
`cmp_tests` valida los contratos de topología sin una variante CMP local.
`cli_tests` comprueba errores y preservación de archivos. `smt_record_tests` prueba el
comparador con registros sintéticos y rechaza incompatibilidades/alteraciones.

`smt_hardware_tests` se omite con código 77 si no está disponible el destino completo.
En el Ryzen comprueba 15 casos RGB (cinco distribuciones, tres geometrías), reglas,
reconstrucciones y afinidad real para el estado actual. Ejecutarlo una vez con cada estado.

El usuario confirmó el 2026-10-16 una prueba SMT satisfactoria en el Ryzen y el
funcionamiento de todos los modelos. Esta confirmación corresponde a validación funcional
realizada por el usuario, no a una prueba física ejecutada por el asistente.
Pendientes: incorporar los registros reales ON/OFF y su comparación, confirmar el modelo
de CPU y documentar las mediciones experimentales. Las etapas 6–8 no se implementan aquí.
