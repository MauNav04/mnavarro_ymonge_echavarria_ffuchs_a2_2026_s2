# Documentación del Algoritmo Gray-Scott y Modelos de Paralelización

**CE 4302 — Arquitectura de Computadores II**  
**Instituto Tecnológico de Costa Rica (TEC)**  
**Problema:** Simulación de Reacción-Difusión de Gray-Scott (Proliferación Bacteriana)

---

## 1. Núcleo Computacional Compartido (`gray_scott_core`)

Todo el proyecto se construye sobre un núcleo común desacoplado (`include/gray_scott_core.hpp` y `src/gray_scott_core.cpp`), garantizando que todas las implementaciones utilicen exactamente la misma física, discretización y constantes numéricas:

### A. Estructuras de Datos
* **`Grid2D` (`include/grid.hpp`):**
  Representa el dominio bidimensional de la placa de Petri. Contiene dos vectores continuos en memoria (`std::vector<float> u` y `std::vector<float> v`).
  * **Formato *Row-Major*:** El acceso espacial se realiza linealmente mediante la fórmula:
    $$\text{índice}(x, y) = y \times \text{width} + x$$
  * **Beneficio Arquitectónico:** El recorrido secuencial de celdas adyacentes aprovecha las líneas de caché L1/L2 de 64 bytes y activa el *Hardware Prefetcher* del procesador.
* **`GrayScottParams` (`include/gray_scott_core.hpp`):**
  Encapsula las tasas físicas de difusión y reacción:
  * $D_u = 0.2$: Tasa de difusión del sustrato nutriente $U$.
  * $D_v = 0.1$: Tasa de difusión de la bacteria autocatalítica $V$.
  * $F = 0.034$: Tasa de alimentación (*Feed*).
  * $K = 0.065$: Tasa de mortalidad bacteriana (*Kill*).
  * $dt = 1.0$: Paso de avance temporal discreto.

### B. Métodos del Núcleo
* **`init_simulation(Grid2D& grid)`:**  
  Establece el estado inicial: satura el dominio completo con nutriente $U = 1.0, V = 0.0$ e inocula un área cuadrada central de $20 \times 20$ celdas con $U = 0.5, V = 1.0$ como semilla de proliferación bacteriana.
* **`step_simulation_slice(const Grid2D& current, Grid2D& next, int start_y, int end_y, const GrayScottParams& params)`:**  
  **Motor unificado de cálculo del stencil.** Evalúa el esquema de diferencias finitas en el tiempo (FTCS) y el operador Laplaciano de 5 puntos exclusivamente sobre la franja de filas $[start\_y, end\_y)$:
  $$\nabla^2 A_{x,y} \approx A_{x+1,y} + A_{x-1,y} + A_{x,y+1} + A_{x,y-1} - 4A_{x,y}$$
  $$\text{Reacción: } R = u \cdot v^2$$
  $$\frac{\Delta u}{\Delta t} = D_u \nabla^2 u - R + F(1 - u), \quad \frac{\Delta v}{\Delta t} = D_v \nabla^2 v + R - (F + K)v$$
  * Las lecturas se realizan de forma segura y concurrente sobre `current` (solo lectura durante el paso $t$).
  * Las escrituras se realizan de forma disjunta sobre `next`.
* **`export_frame_ppm(const Grid2D& grid, int frame_id, const std::string& output_dir)`:**  
  Serializa el estado de la bacteria $V$ a una imagen PPM ultraligera (formato ASCII P3) mapeando la concentración flotante $[0.0, 1.0]$ al canal de color verde $[0, 255]$ para la posterior generación de video.

---

## 2. Implementación Secuencial (`sequential/main.cpp`)

### Propósito
Constituye la **línea base absoluta ($T_{\text{sec}}$)** y el patrón oro de exactitud numérica para calcular el *Speedup* ($S = T_{\text{sec}} / T_{\text{par}}$) y la *Eficiencia* ($E = S / P$).

### Cómo utiliza el Núcleo (`core`)
1. **Invocación:** Llama a `step_simulation_sequential(current, next, params)`, la cual delega directamente en:
   ```cpp
   step_simulation_slice(current, next, 1, current.height - 1, params);
   ```
   computando todas las filas interiores en un único hilo de CPU.
2. **Intercambio de Búferes:** Al terminar cada paso temporal $t$, ejecuta `std::swap(current, next)`.
3. **Desacoplamiento de Benchmark e I/O:** Separa la toma rigurosa de tiempos de cómputo (`std::chrono::high_resolution_clock`) de la fase de exportación de video, asegurando que las escrituras en disco no contaminen las métricas.

---

## 3. CMP — Chip Multiprocessing (`CMP/main.cpp`)

### Propósito
Implementa **paralelismo real sobre múltiples núcleos físicos independientes**, aislando la ejecución de cualquier interferencia o contención microarquitectónica por hilos lógicos (SMT).

### Cómo utiliza el Núcleo (`core`)
1. **Descomposición de Dominio 1D por Filas:**
   La grilla de $(H - 2)$ filas interiores se divide en franjas continuas horizontales asignadas a cada hilo:
   $$\text{start\_y} = 1 + id \times \text{filas\_base} + \min(id, \text{residuo})$$
   $$\text{end\_y} = \text{start\_y} + \text{filas\_base} + (id < \text{residuo} ? 1 : 0)$$
2. **Cómputo en Paralelo:** Cada hilo trabajador ejecuta de forma aislada e independiente:
   ```cpp
   step_simulation_slice(cur, nxt, ctx->start_y, ctx->end_y, *(ctx->params));
   ```
   * Al escribir en filas disjuntas y continuas, se elimina por completo el **Falso Compartimiento (*False Sharing*)**, ya que las fronteras distan miles de bytes entre sí.

### Métodos y Mecanismos Clave
* **Detección Dinámica de Núcleos (`obtener_cpus_fisicos()`):**  
  Lee `/sys/devices/system/cpu/cpu*/topology/core_id` para obtener la lista ordenada de CPUs que corresponden a núcleos físicos distintos (CPUs $0$ a $7$ en procesadores AMD Zen 3 como Ryzen 7 5700G y 7730U), evitando asignar dos hilos CMP al mismo núcleo físico.
* **Afinidad por Hardware (`pthread_setaffinity_np`):**  
  Fija cada hilo de ejecución del sistema operativo al núcleo físico que le corresponde.
* **Hilos Persistentes de Larga Duración:**  
  Los $P$ hilos se crean una única vez al inicio con `pthread_create`. Se eliminan las miles de llamadas al sistema `clone()` que ocurrirían si se crearan hilos por iteración.
* **Sincronización por Barrera (`pthread_barrier_t`):**  
  Al final de cada paso temporal, todos los hilos se sincronizan en una barrera POSIX antes de avanzar al paso $t+1$.
* **Doble Búfer Alternado:**  
  La selección de lectura y escritura se determina mediante la paridad del paso:
  ```cpp
  const Grid2D& cur = (step % 2 != 0) ? *grid_A : *grid_B;
  Grid2D& nxt       = (step % 2 != 0) ? *grid_B : *grid_A;
  ```
  Esto erradica punteros compartidos o cerrojos (*locks*).
* **Verificación Numérica Automática (`--verify`):**  
  Ejecuta el problema idéntico en secuencial y compara celda por celda $|U_{\text{cmp}} - U_{\text{seq}}| < 10^{-5}$ y $|V_{\text{cmp}} - V_{\text{seq}}| < 10^{-5}$.

---

## 4. SMT — Simultaneous Multithreading 



---

## 5. Multihilo de Grano Grueso — Coarse-Grained 

---

## 6. Multihilo de Grano Fino — Fine-Grained 
---

## Tabla Resumen de Modelos

| Modelo | Unidad de Trabajo en Core | Sincronización Inter-paso | Asignación de CPU / Afinidad | Evento de Conmutación |
| :--- | :--- | :--- | :--- | :--- |
| **Secuencial** | Dominio completo $[1, H-1)$ | Ninguna | 1 solo hilo (SO por defecto) | Ninguno |
| **CMP** | Franja 1D contigua $[start\_y, end\_y)$ | `pthread_barrier_t` | 1 hilo por núcleo físico (CPUs 0 a 7) | No hay conmutación (hilos dedicados) |
