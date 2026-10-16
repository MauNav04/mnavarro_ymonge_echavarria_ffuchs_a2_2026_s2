# Etapa 6 — Integración y pruebas

## Organización

`Makefile` en la raíz incluye `scripts/Makefile`. Ambos ofrecen la misma interfaz,
inspirada en `RayTracing/scripts/Makefile` y `gray-scott/scripts/Makefile`: compilar
con `all`, ejecutar con `run_*` y comprobar con `verify_*`. CMake sigue siendo la
única definición de compilación para conservar el núcleo compartido y los cinco
puntos de entrada. No se duplican comandos g++ ni se crean variantes locales de CMP/SMT.
En los proyectos de referencia, `verify_*` activa su verificación interna `--verify`.
Aquí invoca `test_models.py`, que utiliza la CLI compartida y compara los archivos
sin copiar la verificación ni el algoritmo en cada main.

La suite `scripts/test_models.py` usa únicamente la biblioteca estándar de Python.
CTest conserva las pruebas C++ y CLI y añade pruebas del orquestador y una integración
rápida de los tres modelos que pueden ejecutarse sin el Ryzen.

## Preparar Ubuntu de evaluación

Desde `proyecto1/problems/mnavarro/fractal-cpp/`, tener disponibles GNU Make, compilador
C++17, CMake 3.16 o posterior, pthreads, OpenCV 4 y Python3. En Ubuntu se pueden instalar
las dependencias desde la terminal de esa máquina:

```bash
sudo apt update
sudo apt install build-essential cmake libopencv-dev python3
make help
make all JOBS=2
```

Estos comandos funcionales no configuran Perf; su preparación y capturas están en
[docs/profiling.md](profiling.md). El modelo exacto de Ryzen y la versión
de Ubuntu deben conservarse junto con los registros de evaluación. CMP/SMT verifican
Ubuntu/AMD Ryzen, 8 núcleos y topología 8/16 según el estado real de SMT.

## Primera comprobación y suite completa

Con SMT realmente habilitado:

```bash
make topology SMT_STATE=on
make test QUICK=1 SMT_STATE=on
make test SMT_STATE=on
```

`test` compila, ejecuta CTest y después la integración de **todos** los modelos.
La integración exige el hardware y estado solicitados: falla si no están disponibles,
no convierte pruebas omitidas de CTest en una validación completa. Su código de salida
es distinto de cero ante error, timeout, diferencias o afinidad incorrecta.
`QUICK=1` reduce la matriz; no sustituye la corrida completa.

Tras cambiar SMT mediante intervención del usuario en BIOS/UEFI o la configuración
Linux del equipo, conservando los mismos binarios:

```bash
make topology SMT_STATE=off
make test QUICK=1 SMT_STATE=off
make test SMT_STATE=off
```

Ningún objetivo cambia SMT, ejecuta sudo automáticamente ni emula SMT OFF mediante
pinning con el hardware todavía activo. Restaurar después la configuración del equipo.
Los comandos `run_smt_off`, `verify_smt_off` y `record_smt_off` **exigen** SMT realmente
inactivo. A diferencia de los Makefile de referencia, aquí la intervención es externa
para conservar las reglas aprobadas y la trazabilidad de ambos estados.

## Objetivos disponibles

| Objetivo | Acción |
|---|---|
| `all` | Compilar bibliotecas, cinco modelos y pruebas |
| `seq`, `fine`, `coarse`, `cmp`, `smt` | Compilar un ejecutable concreto |
| `test` | CTest y matriz completa de todos los modelos |
| `test_unit` | CTest sin pruebas físicas ni integración entre ejecutables |
| `test_local` | CTest sin pruebas físicas; incluye integración rápida de seq/fine/coarse |
| `verify_all`, `test_models` | Solo matriz integrada (también construye) |
| `verify_seq`, `verify_fine`, `verify_coarse`, `verify_cmp` | Matriz de un modelo contra secuencial |
| `verify_smt`, `verify_smt_on`, `verify_smt_off` | Matriz SMT para el estado indicado |
| `run_seq`, `run_sequential`, `run_fine`, `run_coarse`, `run_cmp` | Comprimir y reconstruir imagen demostrativa |
| `run_smt`, `run_smt_on`, `run_smt_off` | Demostración SMT para el estado indicado |
| `topology` | Diagnóstico de CMP y SMT |
| `record_smt_on`, `record_smt_off`, `compare_smt` | Protocolo reproducible de registros de etapa 5 |
| `clean` | Limpiar productos compilados de CMake, conservando resultados |

Se admite `make -C scripts <objetivo>` desde la raíz o `make <objetivo>` dentro de
`scripts/`; no cambia la ubicación predeterminada de binarios ni resultados.

## Ejemplos de ejecución

```bash
make seq
make run_seq
make run_fine TEAMS=4 THREADS_PER_TEAM=2 QUANTUM=1
make run_coarse TEAMS=4 THREADS_PER_TEAM=2
make run_cmp TEAMS=4 THREADS_PER_TEAM=2
make run_smt_on SMT_TEAMS=4 SMT_THREADS_PER_TEAM=4
# Solo después de desactivar SMT realmente:
make run_smt_off
```

Las demostraciones guardan `image.frc`, `image.png` y, para CMP/SMT, `affinity.csv`
en `results/demo/<seq|fine|coarse|cmp|smt>/`. Repetir sobrescribe esa demostración,
no los registros únicos de integración. No se calculan speedups ni estadísticas.

Ejecutar con otra imagen, geometría o modo gris:

```bash
make run_cmp INPUT=/ruta/imagen.png RANGE=8 STRIDE=8 GRAY=1
make verify_cmp QUICK=1
make verify_fine TEST_INPUT=/ruta/imagen.png RANGE=8 STRIDE=8 TIMEOUT=600
make -C scripts verify_all SMT_STATE=on QUICK=1
```

`INPUT` afecta demostraciones y registros SMT; `TEST_INPUT` sustituye las imágenes
pequeñas generadas por la matriz integrada. Sin `TEST_INPUT`, se generan imágenes
16×24 constantes y con patrón, en RGB y gris, con `(range,stride)=(4,3),(8,16)`.
Esto cubre empates, intervalos desiguales, pocos dominios y equipos sin ranges.
Con entrada propia se usa su geometría indicada por `RANGE` y `STRIDE`; una imagen
grande puede necesitar más tiempo, especialmente con planificación cooperativa.

| Variable | Valor inicial / efecto |
|---|---|
| `BUILD_DIR` | `<raíz>/build` |
| `BUILD_TYPE`, `JOBS` | `Release`, `2` |
| `PYTHON` | `python3` |
| `RESULTS_DIR` | `<raíz>/results` |
| `INPUT` | `sequential/examples/color128.png` |
| `RANGE`, `STRIDE`, `ITERATIONS` | `8`, `8`, `80` |
| `TEAMS`, `THREADS_PER_TEAM` | `4`, `2`, para demostraciones fine/coarse/CMP |
| `SMT_TEAMS`, `SMT_THREADS_PER_TEAM` | `4`, `4`, para demostraciones/registros SMT |
| `SMT_STATE` | `on`, para verificación del estado esperado |
| `QUANTUM` | `1` dominio, exclusivamente Fine-Grained |
| `GRAY` | `0`; `1` activa gris en demostraciones/registros SMT |
| `QUICK` | `0`; `1` reduce la matriz integrada |
| `TIMEOUT` | `120` segundos por subproceso |
| `TEST_INPUT` | Vacío: usar fixtures generadas |
| `SMT_RECORD_DIR` | `<RESULTS_DIR>/smt` |

`TEAMS` y `THREADS_PER_TEAM` no reducen la matriz de verificación. La matriz usa
configuraciones fijas para comprobar distintas cantidades y distribuciones de trabajo.

## Qué comprueba la matriz

Para cada imagen, geometría y modo, genera una referencia secuencial y reconstruye
con `fractal_seq`. Exige FRC y PNG idénticos byte por byte para cada configuración.
Las verificaciones no comparan contra la imagen original, porque la compresión fractal
es con pérdida: comparan contra el resultado del mismo algoritmo secuencial.

Distribuciones completas:

- Secuencial: 1×1.
- Fine-Grained y Coarse-Grained: 1×1, 1×2, 2×2, 4×2 y 16×1.
- CMP: 1×1, 1×2, 2×2 y 4×2, sin superar 8 núcleos distintos.
- SMT: las de CMP más 4×4, 1×16 y 16×1; verifica 16 lógicos en ON y dos
  trabajadores por núcleo en OFF al usar 16 trabajadores.

Con fixtures completas y cinco modelos se comprueban **176 casos**. La versión rápida
hace **18 casos**, incluidos 16 trabajadores SMT. Cada caso CMP/SMT inspecciona el CSV
de afinidad por canal y trabajador. Se prueban también argumentos inválidos, modelo
fijo, reconstrucción rechazada en ejecutables paralelos y preservación de entrada/salida.
Los procesos tienen timeout; al vencer, se cancela su grupo y el registro queda fallido.
CTest añade `thread_failure_tests` en Linux: limita a 512 MiB únicamente el subproceso
de prueba, solicita 1024 trabajadores Fine-Grained/Coarse-Grained y exige que el fallo
real de `pthread_create` se propague sin bloqueo ni truncamiento. El límite no modifica
la sesión ni el sistema. Cada proceso tiene timeout de 15 segundos. Esta prueba cubre
el motor cooperativo; no se presenta como inyección física CMP/SMT.

Una prueba sin bloqueos no constituye una demostración general de ausencia de carreras;
la comprobación adicional con ThreadSanitizer sigue siendo un pendiente independiente.

Cada corrida crea `results/integration/run-<fecha-hora>-<id>/` y conserva:

- Imágenes de prueba, FRC/PNG de referencia y de cada modelo y CSV de afinidad.
- `report.json` con modelos, configuraciones, hashes de entrada/binarios/resultados,
  comandos, stdout/stderr, códigos de salida, timeouts y estado de cada caso.
- `success: true` solo tras completar todos los casos y los rechazos esperados.
  Ante un fallo queda `success: false`, diagnóstico y resultados parciales. Revisar
  también el código de salida del comando; no basta con encontrar archivos generados.

Uso directo, por ejemplo para comprobar solo los tres modelos portables:

```bash
python3 scripts/test_models.py --models sequential fine-grained coarse-grained --quick
```

## Conservar evidencia ON/OFF

```bash
make record_smt_on
# Desactivar SMT realmente, conservar el build y la entrada.
make record_smt_off
make compare_smt
```

Estos objetivos reutilizan `SMT/verify_smt.py`; los directorios ON/OFF deben ser nuevos.
Para otra campaña usar `SMT_RECORD_DIR=/ruta/campaña`. La comparación exige la misma
máquina, entrada, parámetros y binarios. Ver [smt.md](smt.md).

## Validación local y pendiente físico

En WSL se puede ejecutar `make test_local`; esto no añade un modelo CMP local.
El comando `make test` completo debe fallar fuera del destino. Las fixtures y las pruebas
del orquestador validan automatización, no se presentan como pruebas SMT físicas.

La funcionalidad anterior de CMP/SMT fue confirmada por el usuario en el Ryzen. La nueva
matriz integrada de etapa 6 aún debe ejecutarse allí en ON y OFF y conservar sus registros.
Esta etapa prepara pruebas funcionales; no implementa las 200 repeticiones, estadísticas,
gráficas ni Perf de las etapas 7 y 8.


Validación de la automatización en este desarrollo (WSL2): `make test_local` con
`BUILD_DIR=/tmp/fractal-stage6-release` aprobó 13/13; CTest completo aprobó 13 y omitió
las 2 pruebas físicas. La matriz completa de los tres modelos portables aprobó 88 casos
FRC/PNG. Demostraciones Make de seq/fine/coarse dieron archivos idénticos; `verify_all`
falló correctamente por falta del Ryzen y conservó un registro con `success: false`.
No se ejecutó aquí la matriz nueva CMP/SMT.


## Pruebas de perfilado

`make test_profiles PYTHON=.venv/bin/python` ejecuta los contratos de contadores y
análisis, y el diagnóstico físico opcional. Sus omisiones se informan explícitamente.
La [guía de etapa 8](profiling.md) documenta `setup_perf`, las capturas de 200
repeticiones por modelo/grupo y las cinco figuras complementarias.
