# Gray-Scott — Guía de Evaluación (Avance 2 / Demostración 2)
**CE 4302 — Arquitectura de Computadores II**  
**Instituto Tecnológico de Costa Rica (TEC)**  
**Semestre II - 2026**

---

## Resumen de Navegación para Demostración 2

Esta guía orienta al profesor y evaluadores para localizar de forma inmediata cada uno de los requerimientos solicitados para la **Demostración 2 (Semana 8)**:

| # | Requerimiento de la Evaluación | Ubicación en el Repositorio | Descripción Breve |
|---|---|---|---|
| **1** | **Identificación en código** de variables y puntos de paralelización | [`src/gray_scott_core.cpp`](src/gray_scott_core.cpp) y [`sequential/main.cpp`](sequential/main.cpp) | Comentarios inline marcando variables críticas, bucles espaciales paralelizables, cuello de botella temporal y doble búfer. |
| **2** | **Diagrama Arquitectónico Preliminar** (Secuencial vs. Paralelo) | [`docs/images/diagrama_simulacion-paralelo-secuencial_gray-scott.png`](docs/images/diagrama_simulacion-paralelo-secuencial_gray-scott.png) | Esquema visual del cambio arquitectónico: desacoplamiento en franjas contiguas (1D) y sincronización con barreras globales. |
| **3** | **2 Propuestas de Diseño comparadas por criterios** | [`docs/Propuestas.md`](docs/Propuestas.md) | Descomposición 1D (Filas) vs. 2D (Mosaicos / Tiles) evaluadas bajo Localidad de Caché L1/L2, False Sharing y Barreras. |

---

## 1. Identificación de Variables y Puntos de Paralelización en Código

Los puntos clave fueron documentados de forma directa en el código fuente:

### A. Variables Críticas del Problema
Ubicadas en [`include/gray_scott_core.hpp`](include/gray_scott_core.hpp) y [`sequential/main.cpp`](sequential/main.cpp):
* **`width` y `height`:** Determinan el espacio discreto total de datos independientes en memoria ($N = W \times H$).
* **`total_steps`:** Determina la longitud de la cadena de dependencias temporales estrictas ($t \to t+1$).
* **`Du`, `Dv`, `F`, `K`, `dt`:** Parámetros físicos que dictan la difusión y tasas cinéticas de reacción.

### B. Puntos de Paralelización (Bucles) y Cuello de Botella
Ubicados en [`src/gray_scott_core.cpp`](src/gray_scott_core.cpp) y [`sequential/main.cpp`](sequential/main.cpp):
* **Punto 1 (Bucle `y` - Franjas de Filas):** Descomposición 1D ideal para CMP y SMT, preservando la localidad contigua en memoria (*Row-Major*).
* **Punto 2 (Bucle `x` - Columnas):** Permite descomposición en bloques o mosaicos 2D para arquitecturas de grano fino (*Fine-Grained*).
* **Resolución de Data Races:** Implementación estricta de *Double Buffering* (`current` y `next`), permitiendo lecturas concurrentes sin necesidad de cerrojos (*locks*).
* **Cuello de Botella Identificado:** La dependencia temporal estricta ($t \to t+1$) exige una barrera de sincronización por paso (`std::barrier`) antes de alternar buffers (`std::swap`).

---

## 2. Diagrama Arquitectónico Preliminar (Secuencial vs. Paralelo)

El siguiente esquema compara el flujo del baseline secuencial con la arquitectura paralelizable basada en descomposición de dominio 1D:

![Diagrama Secuencial vs. Paralelo](docs/images/diagrama_simulacion-paralelo-secuencial_gray-scott.png)

### Justificación Arquitectónica:
1. **Descomposición 1D por Filas ($H/N$):** La matriz se divide horizontalmente en franjas continuas asignadas a hilos persistentes, maximizando los aciertos en las líneas de caché L1/L2 (64 bytes) al recorrer la memoria de forma lineal (*Row-Major*).
2. **Double Buffering:** Evita condiciones de carrera entre hilos adyacentes al leer únicamente de `current` y escribir exclusivamente en `next`.
3. **Barrera de Sincronización:** Asegura que ningún núcleo inicie el cómputo del paso $(t + 1)$ hasta que todos los hilos hayan concluido sus respectivas franjas del paso $(t)$.

---

## 3. Requisitos, Compilación y Ejecución del Sistema Base

El proyecto está implementado en **C++17** con optimizaciones de compilador `-O3`, temporización de alta precisión mediante `std::chrono` y generación de video automatizada con `ffmpeg`.

### Requisitos en Linux:
```bash
sudo apt update
sudo apt install -y g++ make ffmpeg fonts-dejavu-core python3 python3-pip python3-venv python3.14-venv
```

### Configuración del Entorno de Python:
Para la campaña de benchmarking automatizado, análisis estadístico y generación de gráficos, se cuenta con un archivo global de dependencias en `proyecto1/requirements.txt`.

**Opción A (Recomendada con entorno virtual):**
```bash
# Desde la raíz de proyecto1/:
cd /ruta/hacia/proyecto1
python3 -m venv venv
source venv/bin/activate
pip install -r requirements.txt
```

**Opción B (Instalación directa):**
```bash
# Desde esta carpeta (gray-scott/):
pip install -r ../../requirements.txt
```

> **Nota:** El script de medición `scripts/benchmark.py` utiliza exclusivamente la biblioteca estándar de Python para registrar las corridas y escribir los `.csv`, por lo que funcionará de forma nativa incluso sin instalar paquetes adicionales. Las dependencias externas (`pandas`, `matplotlib`, `seaborn`) son utilizadas para el análisis avanzado y la generación de gráficos en la carpeta `metricas/`.

---

### Compilación:
Desde la carpeta de scripts:
```bash
cd scripts/
make
```

---

### Modo 1: Benchmark Puro (Sin Video)
Diseñado para la toma rigurosa de métricas de hardware y tiempos de CPU sin interferencia de operaciones de I/O en disco:

```bash
make run_seq
```

---

### Modo 2: Campaña Automatizada de 200 Corridas (Generación de CSV)
Ejecuta la batería estadística completa para los modelos concurrentes (Secuencial, CMP, SMT, Coarse-Grained, Fine-Grained), exportando las métricas en tiempo real a la carpeta `metricas/`:

```bash
# Campaña completa estándar (200 corridas por modelo):
make benchmark

# O prueba rápida de verificación (3 corridas por modelo):
make benchmark_fast
```

*Los resultados se almacenan en:*
* `metricas/gray_scott_benchmark.csv`: Muestras crudas corrida por corrida.
* `metricas/resumen_estadistico.csv`: Tabla consolidada con Media, Desviación Estándar, Mínimo, Máximo, Mediana, Speedup y Eficiencia.

---

### Modo 3: Generación de Video Sincronizado
Compila la simulación, exporta los fotogramas PPM temporales e invoca `ffmpeg` con un filtro que calcula dinámicamente el tiempo de CPU por fotograma:

```bash
make run_seq_video
```

*El video final se almacena en `videos/gray-scott-secuencial.mp4`.*

#### Progresión Morfológica de la Simulación ($1024 \times 1024$):

| Estado Inicial (Paso 6,000) | Estado Intermedio (Paso 58,500) | Estado Maduro (Paso 128,000) |
|:---:|:---:|:---:|
| ![Paso 6000](docs/images/frame1_secuencial.png) | ![Paso 58500](docs/images/frame2_secuencial.png) | ![Paso 128000](docs/images/frame3_secuencial.png) |
| **Tiempo CPU: 6.58 s** | **Tiempo CPU: 64.23 s** | **Tiempo CPU: 140.55 s** |
| Formación de la corona bacteriana inicial concéntrica. | División y mitosis en colonias circulares estables. | Cobertura reticular del espacio bidimensional. |

---

## 4. Estructura de Archivos del Módulo

```text
proyecto1/
├── requirements.txt            # Dependencias globales de Python (numpy, pandas, matplotlib, seaborn)
└── problems/
    └── gray-scott/
        ├── CMP/                # Implementación con hilos físicos persistentes 
        ├── coarse-grained/     # Bloques continuos y bloqueo por barrera inter-paso
        ├── fine-grained/       # Planificación cooperativa por cuantums (Q=1) y cíclica
        ├── SMT/                # Variante para hilos simultáneos (HW real y sobre-suscripción SW)
        ├── docs/               # Documentación formal para evaluación
        ├── metricas/           # Salida de benchmarking y reproducibilidad experimental
        │   ├── gray_scott_benchmark.csv  # Datos crudos de las corridas estadísticas
        │   ├── resumen_estadistico.csv   # Estadísticas agregadas (Media, StdDev, Speedup)
        │   └── graficas/                 # Visualizaciones comparativas (.png / .pdf)
        ├── include/            # Encabezados del núcleo de reacción-difusión
        ├── scripts/            # Automatización de compilación y pruebas
        │   ├── benchmark.py    # Script maestro de benchmarking estadístico
        │   └── Makefile        # Reglas para ejecución, videos y benchmarks
        ├── sequential/         # Baseline de comparación secuencial
        ├── src/                # Implementación matemática del stencil FTCS
        ├── videos/             # Videos MP4 sincronizados con el tiempo real de CPU
        └── README.md           # Esta guía de evaluación
```