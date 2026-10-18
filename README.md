# CE 4302 — Arquitectura de Computadores II
## Proyecto Grupal 1: Evaluación y Análisis Comparativo de Modelos Multithreading sobre Arquitecturas Multinúcleo Modernas

**Instituto Tecnológico de Costa Rica (TEC)**  
**Escuela de Ingeniería en Computadores**  
**Semestre II - 2026**

### Integrantes del Grupo:
* **Emanuel Chavarría Hernández** (Ray Tracing)
* **Mauricio Navarro Obando** (Compresión Fractal RGB)
* **Yonathan Monge Sanabria** (Reacción-Difusión Gray-Scott)
* **Fernando Fuchs Mora** (Difusión Térmica Heat2D)

---

## 1. Visión General del Proyecto

El objetivo central de este proyecto de investigación aplicada es evaluar, contrastar y caracterizar el comportamiento microarquitectónico de cuatro paradigmas fundamentales de concurrencia y multihilo frente a una línea base secuencial:

1. **Chip Multiprocessing (CMP):** Paralelismo real mediante asignación estricta de hilos a núcleos físicos independientes con afinidad de CPU (`pthread_setaffinity_np`), eliminando contención por contextos lógicos.
2. **Simultaneous Multithreading (SMT):** Ejecución simultánea sobre los contextos lógicos del procesador. Se evalúan dos variantes:
   * *SMT de Hardware (SMT HW):* Mapeo explícito a contextos lógicos hermanos (*siblings*) compartiendo unidades funcionales y cachés privadas en silicio.
   * *SMT por Sobre-suscripción Software (SMT SW):* Mapeo de hilos con SMT deshabilitado a nivel de kernel (`/sys/devices/system/cpu/smt/control`), forzando al planificador del sistema operativo a alternar hilos mediante *time-slicing*.
3. **Multithreading de Grano Grueso (Coarse-Grained):** Procesamiento en ráfagas largas de trabajo continuo, sincronizado periódicamente mediante barreras o simulando latencias de bloqueo.
4. **Multithreading de Grano Fino (Fine-Grained):** Alternancia rápida entre contextos con cuantum mínimo ($Q=1$), evaluando variantes cooperativas en espacio de usuario (corutinas/planificador *round-robin*) y paralelas cíclicas intercaladas para medir la degradación de localidad de caché.

Para confrontar estos modelos, se implementaron **cuatro problemas científicos y de procesamiento gráfico** con naturalezas computacionales disímiles, abarcando desde algoritmos fuertemente limitados por memoria (*Memory-Bound*) hasta cargas intensivas en unidades de punto flotante (*Compute-Bound*).

---

## 2. Entorno Experimental y Plataforma de Evaluación

Todos los experimentos, campañas de benchmarking y capturas de contadores microarquitectónicos fueron ejecutados sobre un entorno homogéneo en hardware nativo:

* **Procesador:** AMD Ryzen 7 7730U (Microarquitectura Zen 3, litografía de 7 nm).
* **Topología de Cómputo:** 8 núcleos físicos (CMP), 16 contextos lógicos con SMT activo.
* **Frecuencias:** Base a 2.0 GHz, Boost hasta 4.5 GHz.
* **Jerarquía de Memoria:**
  * Caché L1-D: 32 KB por núcleo (línea de 64 bytes).
  * Caché L2: 512 KB por núcleo (privada).
  * Caché L3 (LLC): 16 MB unificada y compartida entre los 8 núcleos del CCX.
* **Memoria RAM:** 16 GB DDR4.
* **Sistema Operativo:** Ubuntu 25.10 (Linux Kernel nativo).
* **Herramientas de Compilación y Perfilado:** `g++` (C++17, `-O3 -DNDEBUG`), `pthreads`, `make`, CMake y Linux `perf` (`perf_events`).

### Metodología de Medición Rigurosa:
* **Campañas de 200 Repeticiones:** Cada modelo fue ejecutado $N = 200$ veces de forma independiente, reportando medias aritméticas, desviación estándar e intervalos de confianza formal al 95% ($IC_{95}$) calculados con distribución Student-$t$.
* **Barridos de Escalabilidad Fuerte (*Strong Scaling*):** Variación sistemática de hilos ($P \in \{1, 2, 4, 8, 16\}$) manteniendo el tamaño del problema constante, contrastando con la aceleración ideal y la Ley de Amdahl.
* **Perfilado PMU con Linux `perf`:** Captura no invasiva de contadores de hardware: ciclos, instrucciones retiradas, IPC (*Instructions Per Cycle*), tasas de fallo en L1-D y LLC/L3, branches fallidos y conmutaciones de contexto voluntarias e involuntarias.

---

## 3. Módulos y Problemas Implementados

A continuación se detalla la especificación técnica de cada uno de los cuatro problemas desarrollados, sus estructuras de datos, estrategias de paralelización y enlaces directos a sus directorios y documentación:

```
┌────────────────────────────────────────────────────────────────────────┐
│                        PROYECTO 1 - PROBLEMAS                         │
├─────────────────────┬─────────────────────┬────────────────────────────┤
│ Problema            │ Naturaleza          │ Responsable                │
├─────────────────────┼─────────────────────┼────────────────────────────┤
│ Gray-Scott          │ Memory-Bound        │ Yonathan Monge Sanabria    │
│ Ray Tracing         │ Compute-Bound       │ Emanuel Chavarría H.       │
│ Heat 2D             │ Memory-Bound        │ Fernando Fuchs Mora        │
│ Fractal Compression │ Compute-Bound       │ Mauricio Navarro Obando    │
└─────────────────────┴─────────────────────┴────────────────────────────┘
```

---

### 3.1 Reacción-Difusión de Gray-Scott
* **Responsable:** Yonathan Monge Sanabria
* **Módulo:** [`proyecto1/problems/gray-scott/`](proyecto1/problems/gray-scott/)
* **Guía y Documentación:** [`proyecto1/problems/gray-scott/README.md`](proyecto1/problems/gray-scott/README.md) | [`DOCUMENTACION-gray-scott.md`](proyecto1/problems/gray-scott/DOCUMENTACION-gray-scott.md)

#### ¿En qué consistió el experimento?
Simula la interacción espaciotemporal no lineal entre dos especies químicas ($U$ nutriente y $V$ catalizador) sobre una malla bidimensional homogénea de $512 \times 512$ celdas ($N = 262{,}144$). Se resuelven dos ecuaciones diferenciales parciales acopladas mediante un esquema explícito FTCS (*Forward-Time Central-Space*) con stencil espacial Laplaciano de cinco puntos a lo largo de **16,000 pasos temporales**, totalizando más de $4.16 \times 10^9$ actualizaciones de stencil interior.

#### Aspectos Técnicos y Arquitectura de Código:
* **Clasificación:** *Memory-Bound* (intensidad aritmética moderada dominada por accesos repetitivos a memoria y stencil de vecinos).
* **Gestión de Memoria y Data Races:** Se implementó una arquitectura de doble búfer (*double buffering / ping-pong*) con intercambio de punteros en $O(1)$ (`grid_A` y `grid_B`), permitiendo lecturas concurrentes sin necesidad de cerrojos (*lock-free*).
* **Localidad Espacial:** Almacenamiento continuo en *Row-Major* ($\text{idx} = y \cdot W + x$). Los bucles internos recorren el eje $X$, activando el *Hardware Stream Prefetcher* y aprovechando el ancho de línea de caché L1-D de 64 bytes.
* **Descomposición de Dominio:** Particionamiento 1D por franjas horizontales contiguas de filas, espaciando las regiones de escritura de cada hilo por más de 2 KB para erradicar el falso compartimiento (*false sharing*).
* **Hallazgo Clave:** CMP dedicado alcanzó el mejor rendimiento ($4.09\times$ de speedup, IPC de 2.40). SMT HW ($3.94\times$) superó a SMT SW ($3.01\times$) en un **24% de aceleración neta**, demostrando que alternar contextos por software forzado en el kernel dispara más de 237,000 conmutaciones de contexto que saturan las cachés L1/L2.

#### Compilación y Ejecución Rápida:
```bash
cd proyecto1/problems/gray-scott/scripts
make all             # Compila los 5 binarios (sequential, cmp, smt, coarse, fine)
./gray_scott_cmp     # Ejecución rápida en CMP
make campaign_200    # Ejecuta la campaña estadística formal de 200 repeticiones
```

---

### 3.2 Trazado de Rayos (Ray Tracing)
* **Responsable:** Emanuel Chavarría Hernández
* **Módulo:** [`proyecto1/problems/echavarria/RayTracing/`](proyecto1/problems/echavarria/RayTracing/)
* **Guía y Documentación:** [`proyecto1/problems/echavarria/RayTracing/README.md`](proyecto1/problems/echavarria/RayTracing/README.md) | [`docs/Propuestas.md`](proyecto1/problems/echavarria/RayTracing/docs/Propuestas.md)

#### ¿En qué consistió el experimento?
Renderizador por transporte de luz basado en óptica física realista sobre una escena tridimensional con esferas dieléctricas (vidrio), metálicas y difusas Lambertianas. Para cada píxel de la imagen ($400 \times 225$ en benchmarking, $1200 \times 675$ en alta resolución), se lanzan múltiples rayos de luz con dispersión estocástica de Monte Carlo y recursión de rebotes hasta una profundidad máxima (`max_depth = 50`).

#### Aspectos Técnicos y Arquitectura de Código:
* **Clasificación:** *Compute-Bound* (cálculos trigonométricos continuos, álgebra vectorial 3D e intersecciones geométricas con escasa dependencia del subsistema de memoria DRAM).
* **Desacoplamiento de E/S:** Se eliminó la escritura serial bloqueante en consola (`std::cout`) reemplazándola por un *Framebuffer* contiguo prealocado en memoria RAM ($W \times H \times 3$ bytes), logrando renderizado paralelo *lock-free*.
* **Descomposición de Dominio:** Se diseñaron e implementaron dos estrategias: partición 1D por líneas de escaneo (*scanlines*) y descomposición 2D por mosaicos dinámicos (*Dynamic Tiles* de $16 \times 16$ píxeles coordinados por cola atómica), equilibrando la carga estocástica donde los píxeles complejos requieren hasta 50 rebotes frente a 1 rebote en el cielo.
* **Hallazgo Clave:** CMP alcanzó un speedup cuasi-ideal de **$7.67\times$ sobre 8 núcleos** con un elevadísimo IPC de 3.39. En contraste, SMT HW se estancó en **$7.65\times$** y su IPC colapsó a 2.18: al ser una carga 100% de cómputo, dos hilos en el mismo núcleo físico saturaron los puertos de despacho y las unidades de punto flotante (FPUs) del silicio Zen 3.

#### Compilación y Ejecución Rápida:
```bash
cd proyecto1/problems/echavarria/RayTracing/scripts
make all             # Compila secuencial, cmp, smt, coarse y fine
make run_cmp         # Ejecuta CMP y genera la imagen final (Output.png)
make campaign_200    # Ejecuta el benchmark formal de 200 repeticiones
```

---

### 3.3 Difusión Térmica Bidimensional (Heat 2D)
* **Responsable:** Fernando Fuchs Mora
* **Módulo:** [`proyecto1/problems/heat2d/`](proyecto1/problems/heat2d/)
* **Guía y Documentación:** [`proyecto1/problems/heat2d/README.md`](proyecto1/problems/heat2d/README.md)

#### ¿En qué consistió el experimento?
Simulación de la conducción de calor homogénea sobre una placa bidimensional descrita por la ecuación de difusión térmica $\frac{\partial T}{\partial t} = \alpha \nabla^2 T$. Se discretiza sobre una malla de $801 \times 801$ puntos ($638{,}401$ celdas interiores activas) con condiciones de contorno de Dirichlet constantes y una condición inicial en modo analítico seno-seno a lo largo de 1,000 pasos de tiempo ($6.38 \times 10^8$ evaluaciones de stencil).

#### Aspectos Técnicos y Arquitectura de Código:
* **Clasificación:** *Memory-Bound* (stencil lineal regular de 5 puntos con alta tasa de transferencias de memoria).
* **Gestión de Memoria y Sincronización:** Estructura `Grid` en precisión doble (`double`), almacenamiento contiguo en *Row-Major* y doble búfer para evitar carreras de datos. La sincronización inter-paso se implementó con una barrera reutilizable de alto rendimiento (`ReusableBarrier`).
* **Modelado de Latencias y Grano:** En Coarse-Grained se modeló un evento sintético de latencia ($100\,\mu\text{s}$ cada 8 filas) para estudiar la degradación ante retrasos de E/S o fallos masivos; en Fine-Grained se implementó un planificador *Round-Robin* cooperativo celda por celda.
* **Hallazgo Clave:** A diferencia de Ray Tracing, en Heat2D **SMT HW fue la configuración más rápida** ($7.151\times$ de speedup, $0.8738$~s, 730.8 MU/s), superando a CMP ($5.949\times$, $1.0531$~s). La latencia pura de acceso a DRAM dejó ciclos ociosos en el núcleo físico que el segundo contexto lógico aprovechó eficazmente para mantener el cauce productivo.

#### Compilación y Ejecución Rápida:
```bash
# Desde la raíz del repositorio:
make release         # Compila todos los ejecutables de Heat2D en build/bin/
make test            # Ejecuta la suite de pruebas unitarias y de integración
./build/bin/heat2d_cmp    # Ejecuta el modelo CMP
./build/bin/heat2d_smt    # Ejecuta el modelo SMT
```

---

### 3.4 Compresión Fractal de Imágenes RGB (fractal-cpp)
* **Responsable:** Mauricio Navarro Obando
* **Módulo:** [`proyecto1/problems/mnavarro/fractal-cpp/`](proyecto1/problems/mnavarro/fractal-cpp/)
* **Guía y Documentación:** [`proyecto1/problems/mnavarro/fractal-cpp/README.md`](proyecto1/problems/mnavarro/fractal-cpp/README.md)

#### ¿En qué consistió el experimento?
Codificación y compresión con pérdidas de imágenes RGB basada en Sistemas de Funciones Iteradas Particionadas (PIFS). La imagen se particiona en bloques destino no solapados (*range blocks*, ej. $8 \times 8$) y bloques fuente mayores (*domain blocks*, ej. $16 \times 16$). Para cada bloque *range*, el algoritmo busca exhaustivamente entre miles de combinaciones espaciales la transformación afín óptima (contracción, 8 isometrías de rotación/reflexión y ajuste de contraste/brillo) que minimice el error cuadrático medio (MSE).

#### Aspectos Técnicos y Arquitectura de Código:
* **Clasificación:** *Compute-Bound* (búsqueda combinatoria y operaciones matriciales por píxel altamente intensivas).
* **Paralelización:** Descomposición independiente del conjunto de bloques *range* entre trabajadores, permitiendo concurrencia masiva sin necesidad de sincronización entre bloques.
* **Control de Topología SMT:** Integra scripts de automatización (`smt_control.py`) que manipulan la interfaz del kernel Linux `/sys/devices/system/cpu/smt/control` con privilegios controlados para ejecutar campañas comparativas reproducibles entre SMT activo (ON) e inactivo (OFF).
* **Hallazgo Clave:** CMP y SMT HW alcanzaron speedups notables ($5.36\times$ y $5.91\times$ respectivamente). Por el contrario, el modelo de Grano Fino cooperativo colapsó drásticamente ($0.25\times$, siendo **4 veces más lento que el secuencial**) debido a que la alternancia constante de tareas en software desperdició un 71% de las instrucciones en sobrecarga de planificación y disparó los fallos de caché LLC al 50.6%.

#### Compilación y Ejecución Rápida:
```bash
cd proyecto1/problems/mnavarro/fractal-cpp
make setup           # Prepara el entorno Python y dependencias
make quick           # Ejecuta una prueba rápida de validación con gráficas
make run             # Ejecuta la campaña completa (200 corridas y perf)
```

---

## 4. Cuadro Comparativo Global de Rendimiento

A continuación se consolidan los resultados experimentales más representativos obtenidos en la campaña de 200 repeticiones sobre el AMD Ryzen 7 7730U:

| Métrica / Problema | Gray-Scott (512×512) | Ray Tracing (400×225) | Heat 2D (801×801) | Compresión Fractal |
| :--- | :---: | :---: | :---: | :---: |
| **Naturaleza Principal** | *Memory-Bound* | *Compute-Bound* | *Memory-Bound* | *Compute-Bound* |
| **Tiempo Secuencial Base** | 4.344 s | 22.84 s | 6.247 s | 0.274 s |
| **Speedup CMP (8 Cores)** | **4.09x** | **7.67x** | 5.95x | 5.36x |
| **Speedup SMT HW (16 Hilos)** | 3.94x | 7.65x | **7.15x** | **5.91x** |
| **Speedup SMT SW (16 Hilos)** | 3.01x | 7.55x | 5.52x | 4.78x |
| **Speedup Coarse-Grained** | 3.42x | 7.61x | 1.98x$^*$ | 0.61x |
| **Speedup Fine-Grained** | 0.97x (Coop) / 2.77x (Cícl) | 0.08x (Coop) / 7.58x (Cícl) | 0.67x (Coop) | 0.25x (Coop) |
| **Modelo Ganador** | **CMP (Dedicado)** | **CMP (Cuasi-ideal)** | **SMT HW (Oculta DRAM)** | **SMT HW (Cómputo)** |

> $^*$ *Nota:* El modelo Coarse-Grained de Heat 2D incluye una pausa sintética intencional de $100\,\mu\text{s}$ para emular bloqueos de alta latencia.

---

## 5. Organización del Repositorio

La estructura del código fuente, documentación y artefactos de experimentación está organizada de forma modular:

```text
mnavarro_ymonge_echavarria_ffuchs_a2_2026_s2/
├── README.md                              # Documentación general y técnica del repositorio
├── Makefile                               # Reglas de compilación y campañas globales de Heat2D
├── paper.tex                              # Artículo científico formal en formato IEEEtran
├── reference.bib                          # Referencias bibliográficas sincronizadas (BibLaTeX)
├── video_guion.md                         # Guion técnico de locución para el video de presentación
├── graphs/                                # Suite estandarizada de figuras y dashboards (300 DPI)
│
├── proyecto1/
│   ├── requirements.txt                   # Dependencias globales de Python (numpy, scipy, matplotlib, seaborn)
│   ├── common/                            # Módulos compartidos y temporizadores de alta precisión
│   │
│   └── problems/                          # Directorios de los 4 problemas de experimentación
│       │
│       ├── gray-scott/                    # [Yonathan Monge] Simulación Reacción-Difusión
│       │   ├── README.md                  # Guía técnica específica de Gray-Scott
│       │   ├── DOCUMENTACION-gray-scott.md# Especificación matemática y microarquitectónica profunda
│       │   ├── src/ / include/            # Núcleo cinético, grilla 2D y modelos multihilo
│       │   ├── scripts/                   # Makefile de Gray-Scott y scripts de benchmarking
│       │   └── metricas/                  # Datos CSV de 200 corridas, perf y gráficas
│       │
│       ├── echavarria/RayTracing/         # [Emanuel Chavarría] Trazador de Rayos 3D
│       │   ├── README.md                  # Guía técnica específica de Ray Tracing
│       │   ├── src/ / include/            # Framebuffer, cámara, materiales e intersecciones
│       │   ├── scripts/                   # Makefile automatizado de compilación y ejecución
│       │   ├── docs/                      # Propuestas de diseño y diagramas
│       │   └── metricas/                  # CSVs, análisis estadístico y figuras generadas
│       │
│       ├── heat2d/                        # [Fernando Fuchs] Difusión de Calor 2D
│       │   ├── README.md                  # Guía técnica específica de Heat2D
│       │   ├── src/ / include/            # Modelos CMP, SMT, FG, CG, Grid y ReusableBarrier
│       │   ├── tests/ / benchmarks/       # Pruebas de estrés y arnés de medición
│       │   └── benchmark_results/         # Registros de campañas y metadatos de hardware
│       │
│       └── mnavarro/fractal-cpp/          # [Mauricio Navarro] Compresión Fractal RGB
│           ├── README.md                  # Guía técnica y fuente de verdad de Fractal
│           ├── app/ / src/ / include/     # Motor de búsqueda de bloques afines y códec
│           ├── scripts/                   # Workflow automático, smt_control y visualización
│           └── metrics/                   # Informes JSON/CSV, perf y gráficas generadas
│
└── test_cases/                            # Banco de pruebas dummy preliminar de Avance 2
    ├── main.cpp                           # Criba segmentada de primos multithreading
    └── dummy_metricas.png                 # Validación preliminar
```

---

## 6. Instalación y Requisitos Globales

### Requisitos de Sistema (Ubuntu / Debian Linux):
```bash
sudo apt update
sudo apt install -y build-essential g++ make cmake ffmpeg linux-tools-common linux-tools-generic python3 python3-pip python3-venv
```

### Configuración del Entorno de Python:
Para reproducir el análisis estadístico, el cálculo de intervalos de confianza al 95% y la generación de figuras:

```bash
# Crear y activar entorno virtual en proyecto1/
cd proyecto1
python3 -m venv venv
source venv/bin/activate
pip install -r requirements.txt
```

---

## 7. Entregables Complementarios

* **Artículo Científico (Paper):** Especificación formal de la metodología, deducciones matemáticas, tablas estadísticas completas (campañas de 200 repeticiones) y análisis microarquitectónico comparativo en formato estándar IEEEtran.

