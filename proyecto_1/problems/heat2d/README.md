# Heat Diffusion 2D — Multithreading Execution Models

**CE 4302 — Arquitectura de Computadores II**  
**Instituto Tecnológico de Costa Rica (TEC)**  
**Semestre II — 2026**

---

## 1. Descripción General

Este proyecto implementa y evalúa diferentes **modelos de ejecución multithreading** sobre una simulación de difusión de calor en dos dimensiones.

El objetivo principal es mantener constante el problema computacional y modificar únicamente la estrategia utilizada para distribuir y ejecutar el trabajo.

Los modelos implementados son:

- **Sequential**
- **Fine-Grained Multithreading**
- **Coarse-Grained Multithreading**
- **Chip Multiprocessing (CMP)**
- **Simultaneous Multithreading (SMT)**

Todos los modelos utilizan:

- la misma ecuación física;
- la misma representación de la superficie;
- la misma condición inicial;
- las mismas condiciones de frontera;
- el mismo kernel numérico;
- la misma cantidad de pasos temporales.

De esta forma, las diferencias observadas durante los experimentos corresponden principalmente al **modelo de ejecución y al overhead asociado a cada estrategia**.

---

# 2. Problema Computacional

El problema consiste en simular la **difusión de calor sobre una superficie bidimensional**.

La superficie se representa mediante una malla rectangular donde cada posición almacena una temperatura:

```math
T[i][j]
```

La evolución de la temperatura está descrita por la ecuación del calor bidimensional:

```math
\frac{\partial T}{\partial t}
=
\alpha
\left(
\frac{\partial^2 T}{\partial x^2}
+
\frac{\partial^2 T}{\partial y^2}
\right)
```

donde:

- $T$ representa la temperatura;
- $\alpha$ representa la difusividad térmica;
- $x$ y $y$ representan las coordenadas espaciales;
- $t$ representa el tiempo.

Para resolver numéricamente la ecuación se utiliza un esquema explícito de **diferencias finitas**.

Para cada celda interior:

```math
T_{i,j}^{n+1}
=
T_{i,j}^{n}
+
\alpha \Delta t
\left[
\frac{
T_{i,j+1}^{n}
-
2T_{i,j}^{n}
+
T_{i,j-1}^{n}
}{
\Delta x^2
}
+
\frac{
T_{i+1,j}^{n}
-
2T_{i,j}^{n}
+
T_{i-1,j}^{n}
}{
\Delta y^2
}
\right]
```

Cada actualización utiliza únicamente la celda central y sus cuatro vecinos:

```text
                    T(i-1,j)
                        ↑
                        │
T(i,j-1)  ←         T(i,j)         →  T(i,j+1)
                        │
                        ↓
                    T(i+1,j)
```

Por lo tanto, durante un mismo timestep las celdas interiores pueden calcularse independientemente siempre que:

- todas lean desde el estado `current`;
- cada trabajador escriba únicamente su celda correspondiente en `next`;
- el siguiente timestep no comience hasta completar el actual.

---

# 3. Representación de Datos

La superficie se almacena mediante un único:

```cpp
std::vector<double>
```

utilizando almacenamiento contiguo en **row-major order**.

La transformación de coordenadas bidimensionales a memoria lineal es:

```math
index(i,j)
=
i \cdot N_{columnas}
+
j
```

Este diseño favorece la localidad espacial cuando las columnas se recorren consecutivamente.

La simulación utiliza además **double buffering**:

```text
             lectura                     escritura
current ───────────────► kernel ─────────────────────► next
                                                     │
                                                     ▼
                                                  swap()
                                                     │
                                                     ▼
                                      siguiente timestep
```

De esta forma, los valores requeridos por el stencil no son modificados mientras todavía están siendo utilizados.

---

# 4. Condición Inicial y Fronteras

La condición inicial utilizada es:

```math
T(x,y,0)
=
T_{\max}
\sin
\left(
\frac{\pi x}{L_x}
\right)
\sin
\left(
\frac{\pi y}{L_y}
\right)
```

Para una superficie de:

```math
L_x = L_y = 1
```

y:

```math
T_{\max}=100^\circ C
```

la temperatura máxima se encuentra en el centro del dominio.

Se utilizan condiciones de frontera de **Dirichlet**:

```math
T_{borde}=0^\circ C
```

por lo que los cuatro bordes permanecen a temperatura constante durante toda la simulación.

---

# 5. Estabilidad Numérica

El esquema explícito debe satisfacer:

```math
\alpha \Delta t
\left(
\frac{1}{\Delta x^2}
+
\frac{1}{\Delta y^2}
\right)
\leq
\frac{1}{2}
```

Cuando:

```math
\Delta x = \Delta y = h
```

la condición se reduce a:

```math
\frac{\alpha \Delta t}{h^2}
\leq
\frac{1}{4}
```

La implementación valida esta condición antes de iniciar la simulación.

---

# 6. Arquitectura del Proyecto

La implementación separa el problema físico del modelo de ejecución.

```text
                         Heat2D Core
                             │
                ┌────────────┼────────────┐
                │            │            │
                ▼            ▼            ▼
              Grid       Initial       Diffusion
                         Conditions       Kernel
                             │
                             ▼
                    Execution Models
                             │
         ┌──────────┬────────┼────────┬──────────┐
         │          │        │        │          │
         ▼          ▼        ▼        ▼          ▼
    Sequential     FG       CG       CMP        SMT
```

La estructura principal es:

```text
proyecto1/problems/heat2d/
│
├── include/
│   ├── config.hpp
│   ├── grid.hpp
│   ├── heat2d.hpp
│   ├── initial_conditions.hpp
│   ├── sequential.hpp
│   ├── fine_grained/
│   ├── coarse_grained/
│   ├── cmp/
│   └── smt/
│
├── src/
│   ├── core/
│   │   ├── grid.cpp
│   │   ├── heat2d.cpp
│   │   └── initial_conditions.cpp
│   │
│   ├── benchmark/
│   │   └── benchmark.cpp
│   │
│   └── models/
│       ├── sequential.cpp
│       ├── fine_grained/
│       ├── coarse_grained/
│       ├── cmp/
│       └── smt/
│
├── benchmarks/
│   └── benchmark_heat2d.cpp
│
├── tests/
│   ├── fine_grained/
│   ├── coarse_grained/
│   ├── cmp/
│   └── smt/
│
└── metricas/
    ├── heat2d_benchmark.csv
    ├── resumen_estadistico.csv
    └── graficas/
```

---

# 7. Modelos de Ejecución

## 7.1 Sequential

La implementación secuencial constituye la referencia computacional para las comparaciones de rendimiento.

Para cada timestep:

```text
for each interior row
    for each interior column
        computeCell()

applyFixedBoundary()

swap(current, next)
```

No existe paralelismo ni sincronización entre trabajadores.

---

## 7.2 Fine-Grained Multithreading

El modelo Fine-Grained utiliza **contextos lógicos cooperativos** administrados por un scheduler Round-Robin.

Los componentes principales son:

```text
LogicalThread
      │
      ▼
RoundRobinScheduler
      │
      ▼
small execution quantum
      │
      ▼
next logical context
```

Cada contexto lógico mantiene su posición dentro de la región asignada.

El cambio entre contextos ocurre con una granularidad muy pequeña, lo cual permite estudiar el costo de una planificación extremadamente frecuente.

Este modelo representa una simulación de **Fine-Grained Multithreading**, no ejecución paralela real sobre múltiples núcleos.

---

## 7.3 Coarse-Grained Multithreading

El modelo Coarse-Grained divide el dominio entre trabajadores reales utilizando `std::thread`.

Cada trabajador procesa una región amplia de filas.

El modelo incorpora eventos de bloqueo configurables para representar el comportamiento de un sistema donde un hilo puede ceder el procesador ante una operación costosa.

La estructura general es:

```text
Worker 0 ──► rows A
Worker 1 ──► rows B
Worker 2 ──► rows C
Worker N ──► rows N

              │
              ▼
            barrier
              │
              ▼
             swap
```

Se utiliza una barrera reutilizable para coordinar los timesteps.

---

## 7.4 Chip Multiprocessing — CMP

CMP utiliza múltiples threads del sistema operativo para ejecutar diferentes regiones del dominio concurrentemente.

Las filas interiores se distribuyen de forma estática entre los trabajadores.

Cada timestep requiere dos fases de sincronización:

```text
Workers compute
      │
      ▼
Barrier 1
      │
      ▼
Boundary update / swap
      │
      ▼
Barrier 2
      │
      ▼
Next timestep
```

Cada trabajador:

- lee desde `current`;
- escribe en una región independiente de `next`;
- permanece activo durante la simulación;
- participa en las barreras globales.

---

## 7.5 Simultaneous Multithreading — SMT

El modelo SMT extiende el modelo paralelo incorporando información explícita de la topología del procesador.

La implementación detecta:

- logical CPUs visibles para el proceso;
- núcleos físicos;
- relación entre núcleos físicos y hardware threads;
- disponibilidad de SMT.

Los workers utilizan afinidad explícita mediante Linux:

```text
pthread_setaffinity_np()
```

La política de placement utiliza primero un logical CPU de cada núcleo físico:

```text
Core 0 → HW thread 0
Core 1 → HW thread 0
Core 2 → HW thread 0
...
```

y posteriormente incorpora los hardware threads adicionales:

```text
Core 0 → HW thread 1
Core 1 → HW thread 1
Core 2 → HW thread 1
...
```

Esto permite controlar de forma reproducible la distribución de los workers sobre la topología disponible.

---

# 8. Configuración Experimental

La configuración principal utilizada durante la campaña final fue:

```text
Surface:                  1.0 m x 1.0 m
Grid:                     801 x 801
Interior cells/timestep:  638 401
alpha:                    0.05
dt:                       0.000005 s
Timesteps:                1000
Total simulated time:     0.005 s
Maximum temperature:      100 °C
Boundary temperature:     0 °C
```

La cantidad de actualizaciones realizadas en una ejecución es:

```math
N_{updates}
=
(801-2)
(801-2)
(1000)
```

por lo que:

```math
\boxed{
N_{updates}
=
638\,401\,000
}
```

Cada ejecución procesa aproximadamente **638.4 millones de actualizaciones de celda**.

---

# 9. Validación Numérica

La condición sinusoidal utilizada posee una solución analítica conocida.

Para un dominio rectangular:

```math
T(x,y,t)
=
T_{\max}
\sin
\left(
\frac{\pi x}{L_x}
\right)
\sin
\left(
\frac{\pi y}{L_y}
\right)
e^{
-\alpha\pi^2
\left(
\frac{1}{L_x^2}
+
\frac{1}{L_y^2}
\right)t
}
```

Para:

```math
L_x=L_y=1
```

se obtiene:

```math
\boxed{
T(x,y,t)
=
T_{\max}
\sin(\pi x)
\sin(\pi y)
e^{-2\pi^2\alpha t}
}
```

Esto permite comparar la solución numérica directamente contra una referencia matemática.

---

## 9.1 Error L2 RMS

```math
L_{2,RMS}
=
\sqrt{
\frac{1}{N}
\sum_{k=1}^{N}
\left(
T_k^{num}
-
T_k^{exact}
\right)^2
}
```

---

## 9.2 Error L-infinity

```math
L_\infty
=
\max_k
\left|
T_k^{num}
-
T_k^{exact}
\right|
```

---

## 9.3 Stress Test Secuencial

La configuración robusta utiliza:

```text
Grid:                801 x 801
dx:                  0.00125 m
dy:                  0.00125 m
dt:                  0.000005 s
Timesteps:           1000
```

Resultados:

```text
Initial center temperature: 100.000000000 C
Final center temperature:    99.507734813 C

L2 RMS analytical error:     0.000000290 C
L-infinity analytical error: 0.000000581 C
```

Las validaciones comprueban:

```text
[PASS] Numerical stability
[PASS] Finite temperatures
[PASS] Dirichlet boundaries
[PASS] Physical temperature range
[PASS] Spatial symmetry
[PASS] Thermal energy decreases every step
[PASS] Analytical L2 error
[PASS] Analytical L-infinity error
[PASS] Requested simulation time
```

---

# 10. Compilación

Desde la raíz del repositorio:

```bash
make clean
make release
```

Esto genera los ejecutables principales dentro de:

```text
build/bin/
```

incluyendo:

```text
heat2d_sequential
heat2d_fine_grained
heat2d_coarse_grained
heat2d_cmp
heat2d_smt
heat2d_benchmark
```

La compilación de release utiliza C++17 y optimización:

```text
-O3
-DNDEBUG
-Wall
-Wextra
-Wpedantic
```

---

# 11. Ejecución de Pruebas

La suite completa de pruebas puede ejecutarse mediante:

```bash
make test
```

La suite valida:

- Fine-Grained logical threads;
- Round-Robin scheduler;
- integración Fine-Grained;
- reusable barrier;
- integración Coarse-Grained;
- integración CMP;
- detección de topología;
- thread affinity;
- integración SMT;
- rechazo de configuraciones inválidas;
- comportamiento con cero timesteps;
- equivalencia numérica contra la versión secuencial.

Una ejecución correcta finaliza con:

```text
All Heat2D tests PASSED.
```

---

# 12. Stress Tests

También se incluyen pruebas de estrés específicas para los diferentes modelos.

## Fine-Grained

```bash
make stress-fg
```

## Coarse-Grained

```bash
make stress-cg CG_THREADS=8
```

## CMP

```bash
make stress-cmp
```

## SMT

```bash
make stress-smt
```

Estas pruebas utilizan cargas mayores que los integration tests y verifican tanto propiedades numéricas como invariantes propias de cada modelo de ejecución.

---

# 13. Campaña Experimental

La campaña final de rendimiento se ejecuta directamente desde el `Makefile`.

Antes de comenzar:

```bash
make clean
make release
make test
```

Posteriormente:

```bash
make campaign-200-heat2d
```

Este target ejecuta **200 repeticiones por configuración**.

Los resultados generados se almacenan en:

```text
proyecto1/problems/heat2d/metricas/
```

Los principales archivos son:

```text
metricas/
├── heat2d_benchmark.csv
├── resumen_estadistico.csv
└── graficas/
```

---

# 14. Metodología Experimental

La campaña mantiene constante:

```text
Problema físico
Configuración numérica
Malla
Número de timesteps
Condiciones iniciales
Condiciones de frontera
Kernel computacional
```

y modifica únicamente el modelo de ejecución.

Las configuraciones principales evaluadas fueron:

| Modelo | Configuración |
|---|---:|
| Sequential | 1 worker |
| CMP | 8 threads |
| SMT Hardware | 16 threads |
| SMT Software | 16 workers |
| Coarse-Grained | 8 threads |
| Fine-Grained | 8 logical contexts |

Cada configuración fue ejecutada:

```text
200 veces
```

para analizar:

- tiempo de ejecución;
- media;
- mediana;
- desviación estándar;
- intervalo de confianza del 95 %;
- speedup;
- eficiencia;
- throughput;
- variabilidad.

---

# 15. Plataforma Experimental

Los resultados finales de rendimiento fueron obtenidos sobre una ejecución **Linux nativa** utilizando:

```text
CPU: AMD Ryzen 7 7730U
Physical cores: 8
Hardware threads: 16
```

La ejecución nativa es importante para los experimentos relacionados con:

- afinidad;
- SMT;
- hardware threads;
- contadores de rendimiento;
- comportamiento real del scheduler.

Las ejecuciones realizadas dentro de máquinas virtuales fueron utilizadas para desarrollo y validación funcional, pero **no constituyen los resultados oficiales de rendimiento**.

---

# 16. Resultados de la Campaña de 200 Ejecuciones

Cada ejecución procesa:

```text
638 401 000 cell updates
```

Los resultados principales fueron:

| Modelo | Workers | Tiempo medio (s) | IC 95 % (s) | Speedup | Throughput (MUpdates/s) |
|---|---:|---:|---:|---:|---:|
| Sequential | 1 | 6.2467 | [6.2342, 6.2593] | 1.00x | 102.22 |
| CMP | 8 | 1.0531 | [1.0446, 1.0616] | 5.93x | 607.77 |
| SMT Hardware | 16 | 0.8738 | [0.8720, 0.8756] | 7.15x | 730.77 |
| SMT Software | 16 | 1.1368 | [1.1256, 1.1479] | 5.50x | 563.50 |
| Coarse-Grained | 8 | 3.1581 | [3.1554, 3.1608] | 1.98x | 202.16 |
| Fine-Grained | 8 | 9.3796 | [9.3676, 9.3916] | 0.67x | 68.07 |

El speedup mostrado en esta tabla se calcula utilizando la relación entre los tiempos medios:

```math
S(p)
=
\frac{\overline{T}_{Sequential}}
{\overline{T}_{p}}
```

---

# 17. Speedup

Tomando la versión secuencial como referencia:

```text
Sequential       1.00x
CMP              5.93x
SMT Hardware     7.15x
SMT Software     5.50x
Coarse-Grained   1.98x
Fine-Grained     0.67x
```

La configuración con menor tiempo de ejecución fue:

```text
SMT Hardware — 16 threads
```

con aproximadamente:

```text
0.874 s
```

y un speedup cercano a:

```text
7.15x
```

respecto a la versión secuencial.

---

# 18. Eficiencia

La eficiencia se define como:

```math
E(p)
=
\frac{S(p)}{p}
```

Para las configuraciones paralelas principales:

| Modelo | Workers | Speedup | Eficiencia aproximada |
|---|---:|---:|---:|
| CMP | 8 | 5.93x | 74.1 % |
| SMT Hardware | 16 | 7.15x | 44.7 % |
| SMT Software | 16 | 5.50x | 34.3 % |
| Coarse-Grained | 8 | 1.98x | 24.7 % |
| Fine-Grained | 8 | 0.67x | 8.3 % |

En Fine-Grained esta relación se presenta únicamente como una normalización respecto a los **ocho contextos lógicos simulados** y no debe interpretarse como eficiencia de ocho núcleos físicos.

---

# 19. Interpretación de Resultados

## 19.1 Sequential

La versión secuencial requirió aproximadamente:

```text
6.25 s
```

por ejecución y constituye la referencia para las demás configuraciones.

---

## 19.2 CMP

CMP con ocho workers redujo el tiempo medio hasta aproximadamente:

```text
1.05 s
```

alcanzando:

```text
5.93x speedup
```

y un throughput superior a:

```text
607 millones de actualizaciones/s
```

El resultado muestra que el kernel Heat2D dispone de suficiente paralelismo espacial para aprovechar varios trabajadores concurrentes.

---

## 19.3 SMT Hardware

SMT Hardware con 16 threads obtuvo el mejor tiempo observado:

```text
0.874 s
```

con:

```text
7.15x speedup
```

y aproximadamente:

```text
731 millones de actualizaciones/s
```

Esta configuración utilizó los hardware threads disponibles mediante placement explícito sobre la topología detectada.

---

## 19.4 SMT Software

La configuración SMT Software obtuvo:

```text
1.137 s
```

y aproximadamente:

```text
5.50x speedup
```

El resultado fue inferior al de SMT Hardware y al de la configuración CMP utilizada en la campaña.

---

## 19.5 Coarse-Grained

Coarse-Grained obtuvo aproximadamente:

```text
3.16 s
```

y:

```text
1.98x speedup
```

Los eventos de bloqueo y la coordinación entre workers introducen overhead adicional respecto a CMP.

---

## 19.6 Fine-Grained

Fine-Grained obtuvo aproximadamente:

```text
9.38 s
```

por ejecución.

Esto corresponde a:

```text
0.67x
```

respecto a la versión secuencial.

El resultado es coherente con la naturaleza del modelo implementado: múltiples contextos lógicos son planificados cooperativamente con una granularidad muy pequeña sobre un único flujo de ejecución.

El costo del scheduling y del manejo de estados supera en este caso el beneficio potencial del modelo.

---

# 20. Métricas Microarquitectónicas

Además del tiempo total, la infraestructura experimental permite estudiar métricas de hardware como:

- ciclos;
- instrucciones;
- IPC;
- cargas de caché L1;
- misses de caché L1;
- cargas LLC;
- misses LLC;
- context switches.

Estas métricas complementan el análisis de tiempo al permitir observar diferencias en:

- overhead de instrucciones;
- comportamiento de caché;
- sincronización;
- presión sobre el scheduler;
- utilización de recursos del procesador.

Los datos de profiling se utilizan como evidencia descriptiva adicional y no sustituyen la campaña estadística de 200 ejecuciones utilizada para analizar tiempos.

---

# 21. Principales Hallazgos

La experimentación permite destacar los siguientes resultados:

1. **Heat2D posee un alto grado de paralelismo espacial.**  
   Las celdas interiores de un mismo timestep pueden calcularse independientemente.

2. **CMP obtiene una aceleración considerable.**  
   Ocho workers redujeron el tiempo de aproximadamente `6.25 s` a `1.05 s`.

3. **SMT Hardware obtuvo el mejor resultado de la campaña.**  
   Con 16 threads se alcanzó aproximadamente `7.15x` de speedup.

4. **La sobresuscripción o ejecución software no sustituye al SMT de hardware.**  
   SMT Software fue más lento que la configuración SMT Hardware utilizada.

5. **Coarse-Grained está condicionado por el costo de los eventos de bloqueo.**

6. **Fine-Grained presenta un overhead elevado en esta implementación.**  
   La frecuencia de scheduling y la administración de contextos hacen que sea más lento que el baseline secuencial.

7. **El modelo de ejecución modifica significativamente el rendimiento aun cuando el problema matemático permanece idéntico.**

---

# 22. Reproducibilidad

Para reproducir la validación completa:

```bash
make clean
make release
make test
```

Para ejecutar la campaña final:

```bash
make campaign-200-heat2d
```

Para ejecutar pruebas de estrés individuales:

```bash
make stress-fg
make stress-cg CG_THREADS=8
make stress-cmp
make stress-smt
```

Después de una ejecución exitosa, los resultados de benchmark pueden consultarse en:

```text
proyecto1/problems/heat2d/metricas/
```

---

# 23. Conclusiones

El proyecto implementa una misma simulación de difusión térmica utilizando diferentes modelos de ejecución multithreading.

El diseño mantiene separados:

```text
Modelo físico
      +
Método numérico
      +
Representación de datos
```

del:

```text
Modelo de ejecución
```

permitiendo comparar las estrategias bajo una carga computacional común.

La campaña experimental de 200 ejecuciones mostró diferencias claras entre los modelos.

La versión secuencial requirió aproximadamente:

```text
6.25 s
```

mientras que la configuración SMT Hardware con 16 threads redujo el tiempo hasta aproximadamente:

```text
0.87 s
```

alcanzando una aceleración cercana a:

```text
7.15x
```

CMP con ocho threads también obtuvo un resultado considerable:

```text
5.93x
```

Por otro lado, Coarse-Grained mostró el impacto del bloqueo y la sincronización, mientras que Fine-Grained permitió observar directamente el costo de una planificación extremadamente frecuente.

En conjunto, los resultados muestran que el rendimiento no depende únicamente de la cantidad de trabajo paralelo disponible, sino también de:

- granularidad;
- scheduling;
- sincronización;
- afinidad;
- topología del procesador;
- utilización de hardware threads;
- comportamiento de memoria y caché.

El proyecto proporciona así una comparación experimental de distintos modelos de multithreading utilizando una misma carga numérica controlada y reproducible.