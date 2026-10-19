# Ray Tracing 
**CE 4302 — Arquitectura de Computadores II**  
**Instituto Tecnológico de Costa Rica (TEC)**  
**Semestre II - 2026**

---

## Resumen de Navegación para la Evaluación

Esta guía orienta al lector para localizar de forma inmediata cada uno de los requerimientos solicitados:

| # | Requerimiento de la Evaluación | Ubicación en el Repositorio | Descripción Breve |
|---|---|---|---|
| **1** | **Identificación en código** de variables y puntos de paralelización | [`src/camera.h`](src/camera.h) y [`sequential/main.cpp`](sequential/main.cpp) | Comentarios marcando variables críticas, bucles espaciales de rayos, resolución de contención de I/O y framebuffer en memoria. |
| **2** | **Diagrama Arquitectónico** (Secuencial vs. Paralelo) | [`docs/Secuencial_vs_paralelo.png`](docs/Secuencial_vs_paralelo.png) | Esquema visual del desacoplamiento . |
| **3** | **2 Propuestas de Diseño comparadas por criterios** | [`docs/Propuestas.md`](docs/Propuestas.md) | Descomposición 1D (Scanlines) vs. 2D (Dynamic Tiles $16 \times 16$) evaluadas bajo balance de carga, localidad L1/L2 y sincronización. |
| **4** | **5 Modelos de Ejecución Multithreading** | [`sequential/`](sequential/), [`CMP/`](CMP/), [`SMT/`](SMT/), [`coarse-grained/`](coarse-grained/), [`fine-grained/`](fine-grained/) | Implementaciones nativas en de Secuencial, CMP, SMT (Hardware y Software), Coarse-Grained y Fine-Grained (Cooperativo y Paralelo). |
| **5** | **Campaña de 200 Corridas Estadísticas** | [`metricas/raytracing_benchmark.csv`](metricas/raytracing_benchmark.csv) y [`metricas/resumen_estadistico.csv`](metricas/resumen_estadistico.csv) | 1,400 ejecuciones totales (200 corridas × 7 variantes) con medias, desviación estándar e intervalos de confianza al 95%. |
| **6** | **Barrido de Escalabilidad Fuerte (Strong Scaling)** | [`metricas/raytracing_strong_scaling.csv`](metricas/raytracing_strong_scaling.csv) | Evaluación de aceleración de 1 a 16 hilos ($p \in \{1, 2, 4, 8, 16\}$) contrastada con la Ley de Amdahl. |
| **7** | **Perfilado Microarquitectónico de Hardware (`perf`)** | [`metricas/raytracing_perf_metrics.csv`](metricas/raytracing_perf_metrics.csv) | Medición de contadores PMU: IPC, tasas de fallo L1-D y LLC/L3, ramas y conmutaciones de contexto del SO. |
| **8** | **Suite Gráfica Estandarizada (10 Figuras a 300 DPI)** | [`metricas/graficas/`](metricas/graficas/) | Colección completa de 10 visualizaciones científicas numeradas de `01_` a `10_` con la paleta oficial institucional de CE 4302. |

---

## 1. Identificación de Variables y Puntos de Paralelización en Código

Los puntos clave de análisis de concurrencia fueron documentados de forma directa en el código fuente:

### A. Variables Críticas del Problema
Ubicadas en [`src/camera.h`](src/camera.h) y archivos fuente de cada modelo:
* **`image_width` e `image_height`:** Determinan el espacio discreto bidimensional total de rayos independientes a trazar ($N = W \times H$).
* **`samples_per_pixel`:** Dicta la granularidad computacional por píxel para anti-aliasing; cada muestra lanza un rayo con jitter estocástico independiente.
* **`max_depth`:** Limita la profundidad de recursión de rebotes de rayos (reflexión y refracción), definiendo la dispersión temporal del cómputo.

### B. Puntos de Paralelización (Bucles) y Cuello de Botella
* **Bucle `j` (Filas / Scanlines):** Descomposición 1D natural para CMP y Coarse-Grained. Cada hilo procesa un bloque de filas contiguas.
* **Bucles `j` e `i` (Píxeles / Mosaicos 2D):** Descomposición por bloques (*tiles*) que mitiga el desbalance de carga inherente a zonas complejas de la escena.
* **Cuello de Botella Resuelto:** En la versión base original, la escritura se realizaba en consola mediante `std::cout`, serializando la ejecución e induciendo condiciones de carrera (*data races*). Se reemplazó por un `Framebuffer` contiguo en RAM donde cada hilo escribe directamente en su posición asignada sin colisiones (*lock-free*).

---

## 2. Diagramas y Propuestas de Diseño

Todos los diagramas y análisis conceptuales se encuentran en la carpeta [`docs/`](docs/):

1. **Diagrama Arquitectónico (Secuencial vs. Paralelo):**  
   [`docs/Secuencial_vs_paralelo.png`](docs/Secuencial_vs_paralelo.png) ilustra la transición desde la emisión secuencial con I/O bloqueante hacia un pipeline multihilo con renderizado en paralelo a memoria compartida.
2. **Documento de Propuestas Detalladas:**  
   [`docs/Propuestas.md`](docs/Propuestas.md) contiene la especificación formal y cuantitativa de:
   * **Propuesta 1:** Descomposición 1D por Líneas de Escaneo Intercaladas.
   * **Propuesta 2:** Descomposición 2D por Mosaicos Dinámicos (*Dynamic Tiles* de $16 \times 16$).
   * **Criterios de Comparación:** Balance de Carga, Localidad de Caché L1/L2 y Sobrecarga de Sincronización.

---

## 3. Requisitos, Compilación y Ejecución

El framework está programado en **C++17** con flags de optimización agresiva (`-O3`), advertencias estrictas (`-Wall -Wextra`), bibliotecas nativas de hilos (`-pthread`) y temporización precisa mediante `std::chrono`.

### Requisitos en Linux:
```bash
sudo apt update
sudo apt install -y g++ make ffmpeg python3 python3-pip python3-venv
```

### Configuración del Entorno de Python:
Para la campaña de benchmarking automatizado, análisis estadístico y generación de gráficas a 300 DPI:
```bash
# Desde la raíz del repositorio:
pip install -r proyecto1/requirements.txt
```

---

### 3.1 Compilación de los Ejecutables
Todas las reglas de compilación se encuentran automatizadas en [`scripts/Makefile`](scripts/Makefile):

```bash
cd scripts/

# Compilar todos los modelos simultáneamente (5 binarios):
make all

# Compilación individual de binarios específicos:
make seq     # Genera: raytracing_seq
make cmp     # Genera: raytracing_cmp
make smt     # Genera: raytracing_smt
make coarse  # Genera: raytracing_coarse
make fine    # Genera: raytracing_fine

# Limpieza de binarios e imágenes intermedias:
make clean
```

---

### 3.2 Ejecución de los Modelos y Generación de Renders (PNG/PPM)
El proyecto cuenta con targets automáticos que ejecutan la simulación, miden el tiempo en milisegundos y convierten el archivo `.ppm` a `.png` mediante `ffmpeg`:

```bash
cd scripts/

# 1. Secuencial (Baseline de referencia, 1 hilo):
make run_seq

# 2. CMP (Chip Multiprocessing, 8 núcleos físicos dedicados):
make run_cmp

# 3. SMT Hardware Real (16 hilos lógicos compitiendo en hardware):
make run_smt

# 4. SMT Sobre-suscripción por Software (16 hilos con afinidad o control de kernel):
make run_smt_sw

# 5. Coarse-Grained (Multihilo de grano grueso en ráfagas de bloques):
make run_coarse

# 6. Fine-Grained Cooperativo (Emulación de conmutación round-robin con quantum Q=1):
make run_fine

# 7. Fine-Grained Paralelo Cíclico (Distribución cíclica intercalada sobre hardware):
make run_fine_parallel
```

*Las imágenes renderizadas se exportan automáticamente en la carpeta `images/` (ej. `images/output_cmp.png`).*

---

### 3.3 Verificación Numérica y Exactitud
Cada modelo incluye una rutina de validación cruzada (`--verify`) que compara el búfer de píxeles resultante contra la imagen base secuencial para descartar cualquier artefacto o degradación visual:

```bash
cd scripts/
make verify_cmp
make verify_smt
make verify_coarse
make verify_fine
```

---

## 4. Campaña Experimental de Benchmarking (200 Corridas)

Para garantizar validez estadística y cumplir con la rúbrica del curso, se implementó un ejecutor automatizado en [`scripts/benchmark.py`](scripts/benchmark.py) que realiza **200 corridas independientes por cada configuración** (1,400 ejecuciones totales), calculando medias, intervalos de confianza al 95% y variabilidad.

### Ejecución de la Campaña:
```bash
cd scripts/

# Ejecutar la campaña completa de 200 corridas y generar todas las gráficas:
make campaign_200

# Prueba rápida de verificación de pipeline (5 corridas por modelo):
make campaign_smoke
```

### Resultados Estadísticos Oficiales ($N = 200$ corridas por modelo, AMD Ryzen 7 7730U):

| Modelo | Modo Arquitectónico | Hilos | Media $\mu$ (ms) | Margen Error $\pm\text{ME}_{95}$ | Intervalo Confianza 95% (ms) | Speedup $S_p$ | Eficiencia $E_p$ |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **SMT (Sobre-suscripción SW)** | `software_oversubscription` | 16 | **925.59** | $\pm 8.34$ | **[917.24, 933.93]** | **4.78x** | 29.9% |
| **Fine-Grained (Paralelo Cíclico)** | `cyclic_threads` | 8 | **967.75** | $\pm 6.20$ | **[961.55, 973.95]** | **4.57x** | **57.1%** |
| **CMP** | `dedicated_cores` | 8 | **1069.13** | $\pm 6.53$ | **[1062.59, 1075.66]** | **4.14x** | 51.7% |
| **SMT (HW Real)** | `hardware_smt` | 16 | **1108.48** | $\pm 4.47$ | **[1104.01, 1112.95]** | **3.99x** | 24.9% |
| **Coarse-Grained** | `burst_block` | 8 | **1146.80** | $\pm 3.65$ | **[1143.16, 1150.45]** | **3.86x** | 48.2% |
| **Secuencial (Baseline)** | `baseline` | 1 | **4421.48** | $\pm 8.13$ | **[4413.35, 4429.61]** | **1.00x** | 100.0% |
| **Fine-Grained (Cooperativo)** | `cooperative_quantum` | 8 | **4762.36** | $\pm 13.49$ | **[4748.88, 4775.85]** | **0.93x** | 11.6% |

> **Análisis Crítico:**  
> En Ray Tracing (altamente intensivo en cómputo de coma flotante y sin dependencias entre píxeles), el modelo de grano fino paralelo cíclico y la sobre-suscripción logran los mejores tiempos al distribuir de forma homogénea las líneas complejas de la escena. Por el contrario, el modelo cooperativo por software incurre en una penalización del 7.7% respecto al secuencial debido a la sobrecarga del planificador en espacio de usuario.

---

## 5. Catálogo Oficial de Gráficas Estandarizadas (300 DPI)

Las 10 figuras científicas se generan en calidad de publicación en [`metricas/graficas/`](metricas/graficas/):

```bash
cd scripts/
# Generar todas las gráficas de rendimiento y microarquitectura:
make plot_all
```

| # | Archivo de Gráfica | Descripción y Relevancia Arquitectónica |
|:---:|:---|:---|
| **01** | `01_tiempos_ejecucion_boxplot.png` | Diagrama de cajas (Tukey Boxplot) de las 200 corridas por modelo. Muestra la dispersión, valores atípicos (*outliers*), medianas y medias. |
| **02** | `02_speedup_comparativo.png` | Factor de aceleración relativo ($S_p = T_{\text{sec}} / T_p$) con barras de error de Intervalos de Confianza al 95% y línea base secuencial (1.0x). |
| **03** | `03_eficiencia_paralela.png` | Eficiencia paralela ($E = \frac{S_p}{p} \times 100\%$) contrastada con la línea ideal (100%) y el umbral de referencia (50%). |
| **04** | `04_escalabilidad_hilos.png` | Curvas de escalabilidad fuerte con barrido de $p \in \{1, 2, 4, 8, 16\}$ hilos, contrastadas contra la aceleración lineal ideal y curvas de la Ley de Amdahl. |
| **05** | `05_perf_ipc_comparativo.png` | Instrucciones por ciclo (IPC) extraídas mediante la PMU con `perf stat`, reflejando la eficiencia del pipeline por modelo. |
| **06** | `06_perf_cache_miss_rates.png` | Análisis de la jerarquía de memoria en dos paneles: Tasa de fallos en L1 Data Cache (%) y en Last-Level Cache (LLC / L3 %). |
| **07** | `07_perf_context_switches.png` | Sobrecarga de conmutaciones de contexto del sistema operativo (`context-switches`), evidenciando el costo del *time-slicing*. |
| **08** | `08_dashboard_consolidado.png` | Dashboard multipanel $2 \times 2$ que consolida: (A) Tiempos medios con IC 95%, (B) Speedup, (C) Eficiencia, (D) Coeficiente de variación ($CV = \sigma/\mu$). |
| **09** | `09_comparativas_microarquitectonicas.png` | Comparativa directa de hipótesis arquitectónicas: Cores Físicos vs Lógicos (SMT) y Granularidad de Planificación (Coarse vs Fine). |
| **10** | `10_perf_resumen_microarquitectura.png` | Dashboard multipanel $2 \times 2$ de microarquitectura: (A) IPC, (B) Fallos L1-D %, (C) Fallos LLC %, (D) Cambios de contexto del SO. |

---

## 6. Perfilado Microarquitectónico de Hardware (Linux `perf`)

El script [`scripts/profile_perf.py`](scripts/profile_perf.py) interactúa directamente con los contadores de desempeño del procesador (PMU):

```bash
cd scripts/

# Habilitar acceso a contadores PMU sin necesidad de root:
make setup_perf

# Ejecución completa de perfilado (3 corridas por modelo para promedio):
make profile_perf

# Prueba rápida de perfilado (1 corrida por modelo):
make profile_fast

# Regenerar figuras de perf directamente desde CSV sin volver a ejecutar los binarios:
make profile_plot
```

### Métricas de Hardware Recolectadas (`raytracing_perf_metrics.csv`):

| Modelo | IPC | Fallos L1-D (%) | Fallos LLC (%) | Context Switches | Branches Miss (%) |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **Secuencial** | **4.31** | 13.53% | **1.30%** | 38 | 0.15% |
| **CMP (8 cores)** | **4.18** | 15.15% | 14.47% | 37 | 0.16% |
| **Fine-Grained (Paralelo)** | **4.17** | 15.19% | 13.02% | 29 | 0.16% |
| **Fine-Grained (Coop.)** | **4.07** | 12.64% | 2.59% | 48 | 0.15% |
| **Coarse-Grained** | **3.98** | 15.28% | 14.46% | 39 | 0.16% |
| **SMT (Sobre-suscrip. SW)** | **3.98** | 15.34% | 19.85% | **1,523** | 0.17% |
| **SMT (HW Real)** | **2.18** | 15.60% | 19.78% | 222 | 0.19% |

---

## 7. Estructura de Archivos del Módulo

```text
RayTracing/
├── CMakeLists.txt                  # Configuración de compilación CMake
├── README.md                       # Este documento (Guía de evaluación completa)
├── main.cpp                        # Punto de entrada base
├── src/                            # Núcleo del trazador de rayos
│   ├── camera.h                    # Cámara, cronometría e identificación de variables críticas
│   ├── framebuffer.h               # Búfer de memoria compartida libre de carreras (lock-free)
│   ├── scene.h / rtweekend.h       # Configuración geométrica de esferas y materiales
│   └── color.h / vec3.h / ray.h    # Álgebra vectorial y utilidades ópticas
├── sequential/main.cpp             # Modelo Secuencial (Línea base sin hilos)
├── CMP/main.cpp                    # Modelo CMP (Paralelismo multinúcleo dedicado)
├── SMT/main.cpp                    # Modelo SMT (Hardware SMT vs Sobre-suscripción Software)
├── coarse-grained/main.cpp         # Modelo Coarse-Grained (Ráfagas de bloques)
├── fine-grained/main.cpp           # Modelo Fine-Grained (Cooperativo y Paralelo Cíclico)
├── docs/                           # Documentación formal y diagramas
│   ├── Propuestas.md               # 2 Propuestas de diseño analizadas por criterios
│   └── Secuencial_vs_paralelo.png  # Diagrama arquitectónico del sistema
├── images/                         # Renders de salida generados en formato PNG
├── metricas/                       # Datos experimentales oficiales
│   ├── raytracing_benchmark.csv    # 1,400 corridas crudas oficiales
│   ├── resumen_estadistico.csv     # Estadísticas consolidadas (Media, IC 95%, Speedup)
│   ├── raytracing_strong_scaling.csv # Barrido de escalabilidad de hilos (1 a 16)
│   ├── raytracing_perf_metrics.csv # Contadores PMU de hardware de Linux perf
│   └── graficas/                   # Suite de 10 figuras científicas a 300 DPI
└── scripts/                        # Automatización de compilación y benchmarking
    ├── Makefile                    # Targets de compilación, ejecución y graficación
    ├── benchmark.py                # Script de campaña estadística de 200 corridas
    ├── plot_metrics.py             # Generador de gráficas de rendimiento
    └── profile_perf.py             # Script de perfilado con Linux perf
```
