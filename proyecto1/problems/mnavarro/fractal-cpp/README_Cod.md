# Framework Experimental de Modelos Multithreading

> Referencia original de criterios. El progreso vigente, las validaciones y los comandos
> actualizados se mantienen en [README.md](README.md), única fuente de verdad.

## 1. Descripción del proyecto

Proyecto de Arquitectura de Computadores II (CE4302) del Instituto Tecnológico de Costa Rica.

El objetivo es implementar y evaluar experimentalmente cuatro modelos de ejecución concurrente aplicados a la compresión fractal de imágenes:

- Fine-Grained Multithreading
- Coarse-Grained Multithreading
- Chip Multiprocessing (CMP)
- Simultaneous Multithreading (SMT)

El proyecto parte de una implementación secuencial existente, utilizada como referencia para medir el rendimiento de las versiones concurrentes.

Considera que yo no tengo una computadora con LINUX, así que la versión de SMT y Linux perf no se va a poder probar de la manera en que lo pide el profesor. Implementa esto adaptando lo que existen en la carpeta ../../echavarria/RayTrancing/SMT, ../../echavarria/RayTrancing/scripts/Makefile y ../../echavarria/RayTrancing/scripts/profile_perf.py .

## 2. Tecnologías y entorno

- **Lenguaje principal:** C++
- **Concurrencia:** POSIX Threads (pthreads)
- **Compilación:** CMake
- **Automatización:** Makefile y Python
- **Profiling:** Linux Perf
- **Sistema operativo:** Linux
- **Hardware de referencia:** Intel Core i5-1335U, 12 procesadores lógicos reportados por `lscpu`

La topología real de núcleos físicos, P-cores, E-cores e hilos SMT debe verificarse antes de configurar afinidad y realizar benchmarks.

## 3. Arquitectura del proyecto

Mantener la estructura existente:

- `sequential/`: implementación secuencial de referencia.
- `fine-grained/`: simulación de planificación de grano fino.
- `coarse-grained/`: implementación de grano grueso.
- `CMP/`: ejecución paralela sobre múltiples núcleos.
- `SMT/`: ejecución utilizando hilos lógicos y comparación de configuraciones de hardware.
- `src/`: implementación compartida de compresión fractal.
- `include/`: interfaces y declaraciones compartidas.
- `scripts/`: automatización de compilación, pruebas, benchmarks, métricas y profiling.
- `metrics/`: resultados experimentales y gráficas.
- `docs/`: documentación técnica complementaria.

**Principio arquitectónico:** Todos los modelos deben reutilizar el mismo algoritmo de compresión y diferenciarse principalmente por su política de ejecución y distribución de trabajo.

## 4. Roadmap y seguimiento

Estados permitidos para las tareas:

- `[ ]` Pendiente o en desarrollo.
- `[x]` Implementada y validada.

Una etapa se considera completa únicamente cuando se cumplen sus criterios de aceptación.

### Etapa 1 — Arquitectura y punto de entrada

- [ ] Analizar la implementación secuencial existente.
- [ ] Identificar operaciones paralelizables.
- [ ] Extraer la lógica compartida a `src/` e `include/`.
- [ ] Establecer una interfaz común de ejecución.
- [ ] Implementar el punto de entrada y sus parámetros.
- [ ] Validar que la versión secuencial mantiene su funcionalidad.

**Aceptación:** Núcleo de compresión reutilizable y ejecución secuencial validada.

### Etapa 2 — Fine-Grained

- [ ] Implementar ejecución mediante pthreads.
- [ ] Implementar planificación cooperativa Round-Robin.
- [ ] Definir cuantums mínimos de trabajo.
- [ ] Implementar sincronización entre trabajadores.
- [ ] Verificar la alternancia de ejecución.
- [ ] Validar resultados contra la versión secuencial.

**Aceptación:** Simulación funcional de alternancia por cuantums, con comportamiento verificable y resultados correctos.

### Etapa 3 — Coarse-Grained

- [ ] Implementar ejecución mediante pthreads.
- [ ] Definir eventos de bloqueo o estancamiento.
- [ ] Implementar la política de cambio ante estos eventos.
- [ ] Evitar cambios periódicos incondicionales de Fine-Grained.
- [ ] Validar resultados contra la versión secuencial.

**Aceptación:** Política de ejecución documentada, comprobable y diferenciada de Fine-Grained.

### Etapa 4 — CMP

- [ ] Implementar distribución paralela de trabajo.
- [ ] Ejecutar trabajadores sobre núcleos físicos.
- [ ] Implementar afinidad de CPU.
- [ ] Evitar condiciones de carrera.
- [ ] Verificar escalabilidad con diferentes cantidades de trabajadores.

**Aceptación:** Paralelismo real sobre múltiples núcleos y resultados correctos.

### Etapa 5 — SMT

- [ ] Detectar topología de hilos lógicos y núcleos físicos.
- [ ] Implementar configuración de afinidad para SMT.
- [ ] Preparar ejecución con SMT habilitado y deshabilitado.
- [ ] Registrar y comparar ambas configuraciones.
- [ ] Validar resultados contra la versión secuencial.

**Aceptación:** Comparación reproducible de ambas configuraciones de hardware, con mediciones verificadas.

### Etapa 6 — Integración y pruebas

- [ ] Configurar CMake para todos los ejecutables.
- [ ] Integrar Makefile para automatización.
- [ ] Implementar `test_models.py`.
- [ ] Validar resultados para diferentes cantidades de trabajadores.
- [ ] Comprobar errores, sincronización y ausencia de bloqueos.

**Aceptación:** Todos los modelos compilan, ejecutan y superan sus pruebas funcionales.

### Etapa 7 — Benchmarking y métricas

- [ ] Implementar `benchmark.py`.
- [ ] Ejecutar al menos 200 repeticiones por configuración experimental.
- [ ] Guardar resultados individuales en CSV.
- [ ] Calcular tiempo, speedup, eficiencia y escalabilidad.
- [ ] Calcular media, desviación estándar e intervalos de confianza del 95 %.
- [ ] Implementar `metrics.py` para generar gráficas automáticamente.
- [ ] Generar boxplots y analizar convergencia.

**Aceptación:** Resultados estadísticos y gráficas reproducibles desde los CSV.

### Etapa 8 — Profiling

- [ ] Implementar `profile.py` utilizando Linux Perf.
- [ ] Recolectar contadores de hardware disponibles.
- [ ] Analizar ciclos, instrucciones, fallos de caché y cambios de contexto.
- [ ] Comparar el comportamiento de los modelos.
- [ ] Analizar el impacto de SMT habilitado/deshabilitado.

**Aceptación:** Resultados de profiling organizados y comparables.

## 5. Metodología experimental

Todas las implementaciones deben utilizar entradas y condiciones de trabajo equivalentes.

Métricas principales:

- Tiempo total de ejecución.
- Speedup respecto a la versión secuencial.
- Eficiencia paralela.
- Escalabilidad según cantidad de trabajadores.

Los benchmarks deben registrar modelo, cantidad de trabajadores, parámetros de entrada, afinidad, estado SMT, tiempo y configuración del hardware.

Las mediciones principales deben ejecutarse sobre hardware físico, no en máquinas virtuales o contenedores.

Los experimentos deben distinguir entre tiempo total y tiempo exclusivo de cómputo cuando se midan ambos.

## 6. Compilación y ejecución

Documentar los comandos reales y verificados para:

- Compilar mediante CMake.
- Ejecutar cada modelo.
- Ejecutar pruebas funcionales.
- Generar benchmarks.
- Generar gráficas.
- Ejecutar Linux Perf.

**Estado inicial:** Pendiente de completar después de inspeccionar y validar la configuración real del repositorio.

## 7. Decisiones técnicas

Registrar únicamente decisiones relevantes y aprobadas.

Cada decisión debe incluir:

- Decisión tomada.
- Justificación.
- Alternativas consideradas cuando corresponda.
- Consecuencias o limitaciones.

**Decisiones iniciales:**

- C++ como lenguaje de implementación.
- pthreads para concurrencia.
- Reutilización del núcleo de compresión fractal.
- Implementación incremental de los modelos.
- CMake para compilación y Python/Makefile para automatización.

## 8. Pendientes y validaciones

Registrar problemas conocidos, tareas bloqueadas y validaciones que requieren intervención manual.

**Pendientes iniciales:**

- Confirmar topología física y lógica de la CPU.
- Definir el tamaño de cuantum de Fine-Grained.
- Definir cómo se modelarán los stalls de Coarse-Grained.
- Establecer configuraciones equivalentes de comparación.
- Verificar los requisitos académicos adicionales de la especificación.

## 9. Historial de progreso

Registrar actualizaciones relevantes con el siguiente formato:

**Fecha — Etapa — Resumen**

- Cambios implementados.
- Pruebas ejecutadas y resultados.
- Tareas pendientes.
- Próximo paso.

Mantener este historial breve. Git conserva el historial detallado de modificaciones.
