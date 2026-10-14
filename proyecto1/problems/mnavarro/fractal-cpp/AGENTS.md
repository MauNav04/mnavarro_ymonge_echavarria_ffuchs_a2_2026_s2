# AGENTS.md — Instrucciones de desarrollo

## 1. Contexto

Este módulo tiene como objetivo implementar un framework experimental para comparar Fine-Grained, Coarse-Grained, CMP y SMT utilizando compresión fractal de imágenes en C++.

El archivo `README.md` contiene el roadmap oficial, la arquitectura, las tareas pendientes y los criterios de aceptación.

Estas reglas se aplican a `fractal-cpp/` y todos sus subdirectorios.
Antes de modificar código, consultar [README.md](README.md), fuente de verdad del progreso.
[README_Cod.md](README_Cod.md) es la referencia original de criterios, no un segundo tablero.
La guía histórica en `sequential/README.md` no autoriza avanzar automáticamente de etapa.

El punto de entrada secuencial reside en `sequential/`; el núcleo único compartido está en
`src/` e `include/` y la E/S compartida en `app/`, extraídos durante la etapa 1 autorizada.
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
- **Coarse-Grained:** Modelar cambios ante bloqueos o stalls significativos. No presentarlo como equivalente a Fine-Grained con un cuantum más largo.
- **CMP:** Utilizar ejecución real sobre múltiples núcleos físicos, verificando afinidad.
- **SMT:** Diferenciar concurrencia por software del SMT proporcionado por hardware. Verificar topología y estado del sistema.

No afirmar que una simulación por software reproduce exactamente el comportamiento microarquitectónico del procesador.

La desactivación de SMT en BIOS/UEFI y las pruebas físicas correspondientes requieren intervención y validación del usuario.

## 6. Benchmarking y profiling

- Mantener entradas y parámetros comparables.
- Utilizar la versión secuencial como referencia.
- Guardar resultados individuales antes de calcular estadísticas.
- Cumplir el mínimo de 200 repeticiones por configuración experimental.
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
