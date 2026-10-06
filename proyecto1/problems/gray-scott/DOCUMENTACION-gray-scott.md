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

## 4. SMT — Simultaneous Multithreading (`SMT/main.cpp`)

### Propósito
Evaluar el comportamiento microarquitectónico de 16 hilos de trabajo compitiendo sobre los 8 núcleos físicos de la CPU bajo dos regímenes de ejecución:
1. **SMT por Hardware Real (`SMT ON`):** Dos hilos lógicos coexisten en el mismo núcleo físico, compitiendo directamente por las unidades de punto flotante (FPUs), puertos de despacho y la memoria caché L1/L2 a nivel de ciclo de reloj.
2. **Sobre-suscripción por Software (`SMT OFF`):** Con SMT apagado en el kernel, solo existen 8 núcleos disponibles, forzando al planificador del sistema operativo a alternar los 16 hilos mediante conmutaciones de contexto por software (*time-slicing*).

### Cómo utiliza el Núcleo (`core`)
1. **Descomposición 1D en 16 Franjas:**
   Divide las $(H - 2)$ filas interiores entre los 16 hilos lógicos en franjas horizontales continuas:
   $$\text{start\_y} = 1 + id \times \text{filas\_base} + \min(id, \text{residuo})$$
   $$\text{end\_y} = \text{start\_y} + \text{filas\_base} + (id < \text{residuo} ? 1 : 0)$$
2. **Invocación Unificada:**
   Cada hilo ejecuta independientemente:
   ```cpp
   step_simulation_slice(cur, nxt, ctx->start_y, ctx->end_y, *(ctx->params));
   ```
   manteniendo el acceso en orden *Row-Major* y eliminando el falso compartimiento (*false sharing*).

### Métodos y Mecanismos Clave
* **Diagnóstico de Topología en Linux (`obtener_estado_smt_linux()`):**  
  Lee dinámicamente `/sys/devices/system/cpu/smt/control` para detectar si el procesador tiene SMT activo (`on`) o inactivo (`off`).
* **Mapeo de Afinidad Adaptativo (`pthread_setaffinity_np`):**  
  * Con **SMT ON** (16 CPUs online): Asigna el hilo $t$ a la CPU lógica $t$. De esta forma, el Hilo 0 (CPU 0) y el Hilo 8 (CPU 8) se ejecutan físicamente sobre el Core 0 compartiendo hardware.
  * Con **SMT OFF** (8 CPUs online): Asigna el hilo $t$ a la CPU $(t \pmod 8)$. El Hilo 0 y el Hilo 8 quedan enlazados a la misma CPU física 0, obligando al kernel de Linux a realizar *preemptive multitasking* por software.
* **Hilos Persistentes + Barrera:**  
  Los 16 hilos se crean una única vez al inicio y se sincronizan al final de cada paso temporal mediante `pthread_barrier_t`.
* **Reglas Automatizadas en el Makefile:**  
  * `make run_smt`: Ejecuta SMT con los 16 hilos lógicos en hardware real.
  * `make run_smt_sw`: Deshabilita temporalmente SMT en el kernel (`echo off`), ejecuta los 16 hilos en sobre-suscripción, y restaura SMT a `on` de forma segura mediante un manejador `trap` de bash.
  * `make verify_smt`: Comprueba la exactitud matemática contra el secuencial asegurando $|diff| < 10^{-5}$.
  * `make run_smt_video`: Genera el video con superposición de métricas de SMT.

---

## 5. Multihilo de Grano Grueso — Coarse-Grained (`coarse-grained/main.cpp`)

### Propósito
Implementar el modelo arquitectónico de multihilo de grano grueso (*Coarse-Grained / Block Multithreading*), donde los hilos de ejecución tradicionales ejecutan **ráfagas de cómputo continuas y extensas** sobre grandes bloques de datos, y el cambio de contexto o cesión del procesador ocurre **únicamente ante eventos de bloqueo costosos** (como la barrera de sincronización global inter-paso o las operaciones de E/S en disco).

### Cómo utiliza el Núcleo (`core`)
1. **Ráfagas Ininterrumpidas de Cómputo (32,640 celdas por ráfaga/hilo):**
   Cada hilo tradicional recibe un bloque contiguo de filas $[start\_y, end\_y)$ (típicamente $64$ filas en una grilla de $512 \times 512$). El hilo invoca:
   ```cpp
   step_simulation_slice(cur, nxt, ctx->start_y, ctx->end_y, *(ctx->params));
   ```
   ejecutando decenas de miles de evaluaciones de stencil consecutivas sin ceder la CPU, manteniendo las líneas de caché calientes y el pipeline saturado.
2. **Punto Estricto de Conmutación / Bloqueo:**
   El hilo solo se detiene ante eventos de bloqueo de alta latencia:
   * **Barrera Inter-paso:** `pthread_barrier_wait(ctx->barrera)` (exactamente 16,000 eventos de bloqueo en la simulación).
   * **Operación de E/S:** Bloqueo de espera cuando el hilo 0 vuelca un fotograma a disco.

### Métodos y Mecanismos Clave
* **Métricas de Ráfaga y Bloqueo:**
  El programa contabiliza explícitamente:
  * El tamaño de la ráfaga continua (`burst_cells` = $32{,}640$ celdas).
  * El número total de eventos de bloqueo/sincronización (`stall_events` = $16{,}000$).
* **Hilos Persistentes Tradicionales:**  
  Se crean al inicio mediante `pthread_create` y perduran a lo largo de toda la simulación, evitando el overhead del kernel en la creación de procesos o hilos efímeros.
* **Reglas del Makefile:**  
  * `make run_coarse`: Ejecuta el modelo de grano grueso con 8 hilos sobre $512 \times 512$ y $16{,}000$ pasos.
  * `make verify_coarse`: Valida la coherencia numérica celda a celda contra el secuencial ($|diff| < 10^{-5}$).
  * `make run_coarse_video`: Genera el video representativo con las métricas en pantalla.

---

## Tabla Resumen de Modelos

| Modelo | Unidad de Trabajo en Core | Sincronización Inter-paso | Asignación de CPU / Afinidad | Evento de Conmutación |
| :--- | :--- | :--- | :--- | :--- |
| **Secuencial** | Dominio completo $[1, H-1)$ | Ninguna | 1 solo hilo (SO por defecto) | Ninguno |
| **CMP** | Franja 1D contigua $[start\_y, end\_y)$ | `pthread_barrier_t` | 1 hilo por núcleo físico (CPUs 0 a 7) | No hay conmutación (hilos dedicados) |
| **SMT** | Franja 1D contigua (16 divisiones) | `pthread_barrier_t` | 2 hilos por core (HW) / Sobre-suscripción (SW) | Concurrente en pipeline (HW) o Time-slice (SW) |
| **Coarse-Grained** | Ráfagas continuas de grandes bloques | `pthread_barrier_t` | Hilos tradicionales (8 por defecto) | **Únicamente** ante evento de bloqueo (*stall* / barrera) |
