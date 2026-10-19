# AGENTS.md — Instrucciones de desarrollo

## 1. Contexto

Este módulo tiene como objetivo implementar un framework experimental para comparar Fine-Grained, Coarse-Grained, CMP y SMT utilizando compresión fractal de imágenes en C++.

El archivo `README.md` contiene el roadmap oficial, la arquitectura, las tareas pendientes y los criterios de aceptación.

Estas reglas se aplican a `fractal-cpp/` y todos sus subdirectorios.
Antes de modificar código, consultar [README.md](README.md), fuente de verdad del progreso.
[README_Cod.md](README_Cod.md) es la referencia original de criterios, no un segundo tablero.
La guía histórica en `sequential/README.md` no autoriza avanzar automáticamente de etapa.

Cada modelo tiene su propio `main.cpp` y `CMakeLists.txt` en su carpeta, y un ejecutable
separado: `sequential/` → `fractal_seq`, `fine-grained/` → `fractal_fine`,
`coarse-grained/` → `fractal_coarse`, `CMP/` → `fractal_cmp` y
`SMT/` → `fractal_smt`. CMP conserva únicamente la versión de evaluación; no
reintroducir una variante local ni un perfil que omita la verificación del destino.
Cada main fija su modelo; no permite ejecutar otro mediante `--model`.
El núcleo único compartido está en `src/` e `include/`. Argumentos, E/S y mediciones se
reutilizan desde `app/`; los main deben mantenerse pequeños, sin copiar el controlador.
Las imágenes de entrada compartidas viven en `assets/`; no guardarlas dentro de
un modelo, `app/` ni `src/`. Las salidas y campañas conservan sus directorios de resultados.
Conservar el algoritmo matemático y los formatos FRC1/FRC2.
Diseño aprobado: equipos de hilos por range; cada hilo evalúa dominios distintos y el equipo
reduce mínimos para publicar una única regla. En empates conservar el orden dominio/simetría
secuencial. No sustituir este diseño por ranges completos independientes por hilo sin consultar.
No interpretar la disponibilidad del roadmap como autorización para implementarlo entero.

## 2. Reglas generales

1. Implementar únicamente la etapa o tarea solicitada.
2. No modificar el roadmap ni las decisiones de arquitectura aprobadas sin autorización.
3. Mantener la estructura existente del repositorio.
4. Reutilizar el código de compresión fractal compartido.
5. Evitar duplicación, abstracciones innecesarias y código muerto.
6. Utilizar C++ y pthreads para la implementación concurrente.
7. Utilizar Python únicamente para automatización, análisis estadístico y generación de gráficas.
8. Mantener comentarios técnicos que expliquen sincronización, planificación y decisiones no evidentes.
9. No añadir dependencias sin justificar su necesidad.
10. Priorizar implementaciones comprensibles, verificables y defendibles académicamente.
11. Los nombres de las carpetas y archivos deben de estar en inglés.

## 3. Flujo obligatorio de trabajo

### Antes de implementar

1. Leer el README.
2. Inspeccionar los archivos relacionados con la tarea.
3. Identificar funcionalidades existentes que puedan reutilizarse.
4. Verificar las dependencias con otras etapas.
5. Si existe una decisión técnica importante no resuelta, presentar una propuesta breve y esperar aprobación.
6. No solicitar aprobación para decisiones menores que ya estén cubiertas por el roadmap.

### Durante la implementación

1. Trabajar exclusivamente dentro del alcance autorizado.
2. Mantener compatibilidad con las interfaces compartidas.
3. No modificar el algoritmo matemático de compresión sin autorización.
4. Evitar condiciones de carrera, bloqueos y esperas activas innecesarias.
5. Mantener el código organizado y modular.
6. No alterar pruebas para ocultar errores.
7. Documentar las limitaciones de las aproximaciones por software.

### Después de implementar

1. Compilar los componentes afectados.
2. Ejecutar las pruebas disponibles y pertinentes.
3. Comparar los resultados contra la versión secuencial cuando corresponda.
4. Revisar posibles regresiones.
5. Actualizar el README con el progreso real.
6. Informar archivos modificados, pruebas realizadas, resultados y pendientes.
7. No avanzar automáticamente a la siguiente etapa.

## 4. Reglas de validación

Una tarea solamente puede marcarse como completada `[x]` cuando:

- Su implementación está terminada.
- Compila correctamente.
- Las pruebas correspondientes se han ejecutado satisfactoriamente.
- Cumple sus criterios de aceptación.
- No existen problemas conocidos que invaliden su funcionamiento.

Si una prueba no puede ejecutarse, registrar el motivo y mantener pendiente la validación.

Nunca inventar resultados, benchmarks, mediciones de hardware ni pruebas exitosas.

## 5. Reglas específicas de concurrencia

- **Fine-Grained:** Aproximar la alternancia por ciclos mediante cuantums mínimos y Round-Robin cooperativo.
- **Coarse-Grained:** Evento aprobado: más de 2/3 de los dominios asignados al hilo para
  el range sin mejorar consecutivamente el menor error local. Conservar mínimo/posición
  y reanudar sin omitir candidatos; reiniciar racha al mejorar o ceder por stall. La
  finalización del intervalo tiene prioridad. Es una heurística simulada, no un stall
  físico medido. No sustituirla por Fine-Grained con un cuantum más largo.
- **CMP:** Única versión para evaluación en Ubuntu nativo, AMD Ryzen,
  8 núcleos/16 lógicos (8 con SMT inactivo).
  Utilizar pthreads concurrentes y verificar afinidad efectiva; no reemplazar núcleos
  físicos por hermanos SMT al exceder el presupuesto. Los datos WSL no validan hardware físico.
- **SMT:** Preparar para la misma computadora Ubuntu/AMD Ryzen de evaluación de RayTracing.
  Diferenciar concurrencia por software del SMT de hardware; verificar topología y estado.
  `--smt-state on|off` exige el estado real del kernel; no lo modifica. Conservar 16
  trabajadores por defecto (4×4), ON sobre 16 CPU y OFF sobre 8 núcleos sobresuscritos.
  Compartir el motor paralelo con CMP sin cambiar su política de un hilo por núcleo.
  Registrar validaciones realizadas por el usuario como tales. No atribuirles mediciones
  detalladas ni artefactos que no se hayan proporcionado.
  No sustituir validaciones físicas por pruebas locales. Implementar únicamente al solicitarse.

No afirmar que una simulación por software reproduce exactamente el comportamiento microarquitectónico del procesador.

La desactivación de SMT en BIOS/UEFI y las pruebas físicas correspondientes requieren intervención y validación del usuario.

## 6. Benchmarking y profiling

Automatización vigente: Makefile raíz incluye `scripts/Makefile`; CMake sigue siendo
la única definición de compilación. Conservar objetivos `all`, `run_*`, `verify_*` y
`test` similares a RayTracing/gray-scott. `scripts/test_models.py` es una suite funcional,
no un benchmark. La suite completa debe fallar si falta CMP/SMT o el estado solicitado;
`test_local` prueba únicamente lo permitido por el entorno. Los objetivos individuales
no modifican SMT; `make run` es la excepción autorizada para la campaña ON/OFF/ON.
Guardar registros por corrida y propagar errores/timeouts. No marcar la matriz nueva
validada en Ryzen hasta ejecutarla allí, aunque los modelos individuales ya se hayan probado.

La entrada habitual es `make setup`, `make quick`, `make run` y `make report`,
documentada en `docs/running.md`. Conservar la ayuda principal breve y las opciones
previas en `help_advanced`. `workflow.py` reutiliza los motores de captura/análisis;
no duplicar algoritmos ni sustituir CMake. `quick` siempre es diagnóstico y `run`
exige 200 repeticiones para benchmark/escalabilidad y 20 para Perf por modelo/grupo
(mínimo aceptado de Perf: 15). Por autorización explícita del usuario (2026-10-17),
`run` debe activar SMT, capturar ON, desactivarlo mediante sysfs, capturar OFF y
restaurarlo a ON antes del informe. Usar un ayudante privilegiado limitado a ON/OFF,
con exclusión mutua y restauración ante EOF/error/interrupción; verificar el estado
real. Pedir sudo sólo al inicio y mantener los compresores y sus salidas sin privilegios.
`quick`, los motores individuales y `report` no cambian SMT. No ejecutar cambios
físicos durante tests locales: utilizar archivos temporales y contratos.
Compartir la paleta institucional en `scripts/plot_style.py`, conservando 300 DPI.
Detectar el estado y
comprobar Perf antes de iniciar campañas largas. Conservar datos parciales ante
errores y usar nombres únicos para nuevas campañas e informes. `report` analiza
CSV existentes compatibles sin recapturar ni mezclar cargas/estados inválidos.
El directorio predeterminado sigue siendo `build`; Make prefiere el Python de
`.venv` cuando existe, sin exigir activación manual.


El destino de SMT y Linux Perf es la computadora de evaluación de RayTracing: Ubuntu,
AMD Ryzen 7 7730U (diagnósticos recibidos: Ubuntu 25.10), topología 8/16 confirmada. Las
pruebas locales de contratos no sustituyen la ejecución física. No dar por medido SMT/Perf sobre esa máquina
si solo se ejecutó en WSL. Esta regla no autoriza implementar etapas futuras.

- Automatización de etapa 7: `benchmark_full`/`benchmark_fast`, `scalability` y `plot`.
  Conservar CSV individuales antes de analizar, hashes/metadatos y etiquetas físicas,
  diagnósticas o sintéticas. No mezclar estados SMT ni campañas incompatibles.
- Campañas formales: Release, Ryzen real, 200 corridas mínimas para benchmark y
  escalabilidad; Perf usa 20 por modelo/grupo (mínimo 15, autorizado por el usuario).
  Los diagnósticos cortos se etiquetan y no sustituyen la campaña formal. Sólo el
  orquestador `make run` cambia SMT. CMP no se sobresuscribe a 16 trabajadores.
- Estadísticas: Student-t para IC de medias y método de incertidumbre del speedup
  documentado; no eliminar outliers ni inventar intervalos con una sola muestra.
- Los contadores cooperativos son de software. IPC, caché y contexto del SO se capturan
  con etapa 8/Perf; no llenar esos campos con estimaciones ni ceros supuestamente medidos.
- Perf: conservar `setup_perf`, `profile_fast`, `profile_perf` y `profile_plot`, captura
  incremental y estado físico verificado. No cambiar permisos ni capacidades; los
  motores Perf no modifican SMT (lo gestiona exclusivamente el orquestador autorizado).
  El perfilado usa 20 repeticiones por modelo/grupo de contadores (mínimo 15).
- Contar parejas PMU simultáneamente y separar grupos. Distinguir alcance del comando
  completo de Perf frente al tiempo exclusivo del algoritmo. CPU efectivas usa task-clock
  dividido por duration_time de la misma pasada; si falta un evento, registrar N/D.
- Guardar unidad, runtime y porcentaje efectivo; excluir muestras inválidas o de runtime
  insuficiente sin borrarlas. No sustituir LLC por caché genérica. Conservar ceros medidos.
  Los tests sintéticos no validan hardware; los diagnósticos físicos no sustituyen
  las campañas de benchmark/escalabilidad de 200 repeticiones ni el perfilado de 20.
- Perf conserva por defecto cada salida cruda en `perf-stat.jsonl`, con ruta original y
  texto íntegro, y flush tras cada pasada. `PERF_RAW_STORAGE=files` conserva la disposición
  anterior. No borrar ni corregir retroactivamente campañas recibidas para ocultar fallos.
  Una unidad vacía de task-clock/duration_time en Perf 6.17 representa conteos crudos ns;
  normalizar a ms/ns respectivamente. Unidades explícitas desconocidas siguen siendo N/D.
- Exportar campañas terminadas con `archive_metrics`; incluir CSV aunque Git los ignore,
  conservar originales y no sobrescribir archivos de exportación existentes.
- Mantener entradas y parámetros comparables.
- Utilizar la versión secuencial como referencia.
- Guardar resultados individuales antes de calcular estadísticas.
- Cumplir el mínimo de 200 repeticiones por configuración de benchmark/escalabilidad;
  para Perf, usar 20 por defecto y mínimo 15 por modelo/grupo.
- Evitar incluir compilación u operaciones externas dentro del tiempo de cómputo cuando se mida exclusivamente el algoritmo.
- No ejecutar los benchmarks principales en entornos virtualizados.
- No asumir que todos los eventos de Linux Perf están disponibles.
- Distinguir entre resultados medidos, estimados y simulados.

## 7. Gestión de README.md

Después de cada tarea:

1. Actualizar exclusivamente los checkboxes correspondientes.
2. Marcar `[x]` solo cuando exista validación.
3. Registrar problemas nuevos en la sección de pendientes.
4. Documentar decisiones técnicas importantes.
5. Actualizar los comandos de ejecución si cambiaron.
6. Mantener un historial breve del progreso.

No reescribir todo el README innecesariamente.

## 8. Control de versiones

- Respetar el flujo de ramas definido por el proyecto.
- Utilizar cambios y commits incrementales.
- No ejecutar operaciones destructivas de Git sin autorización.
- No realizar commits, merges o pushes automáticamente salvo solicitud explícita.
- Mantener los cambios relacionados con cada tarea separados de modificaciones ajenas.

## 9. Formato de respuesta al finalizar

Entregar un resumen breve con:

**Etapa o tarea:** Qué se trabajó.

**Cambios realizados:** Archivos y funcionalidades modificadas.

**Validación:** Comandos ejecutados y resultados reales.

**README:** Checkboxes actualizados.

**Pendientes:** Problemas, limitaciones o pruebas faltantes.

**Siguiente paso:** Próxima tarea recomendada, sin implementarla automáticamente.
