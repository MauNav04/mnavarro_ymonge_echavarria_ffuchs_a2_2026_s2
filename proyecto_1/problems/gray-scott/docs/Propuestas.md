# Propuestas de Diseño de Paralelización — Gray-Scott
**CE 4302 — Arquitectura de Computadores II**  
**Instituto Tecnológico de Costa Rica (TEC)**  
**Semestre II - 2026**

---

## 1. Contexto del Problema y Espacio de Paralelización

La simulación de reacción-difusión de Gray-Scott discretizada mediante un stencil de 5 puntos en diferencias finitas (FTCS) presenta:
1. **Paralelismo de Datos Masivo (Intra-paso):** El cómputo del estado de cada celda en el instante $t + 1$ es independiente de sus vecinas gracias a la técnica de *Double Buffering*.
2. **Dependencia Temporal Estricta (Inter-paso):** El paso $t + 1$ no puede iniciar hasta que todas las celdas del paso $t$ hayan sido actualizadas, requiriendo un punto de sincronización global (barrera).

A continuación se analizan dos estrategias arquitectónicas de descomposición de dominio para distribuir el cálculo entre los núcleos del procesador.

---

## 2. Propuesta 1: Descomposición de Dominio 1D (Por Franjas de Filas)

### Descripción:
Se particiona la matriz bidimensional horizontalmente a lo largo del eje $Y$. Dado un número $N$ de hilos de ejecución (*worker threads*) y una altura $H$, cada hilo se encarga exclusivamente de computar un bloque continuo de filas contiguas:
$$\text{Rango del Hilo } i = \left[ i \times \frac{H}{N},\; (i + 1) \times \frac{H}{N} \right)$$

### Ventajas Técnicas:
* **Localidad Espacial y Caché L1/L2 Óptima:** Dado que C++ indexa arreglos contiguos en formato *Row-Major* ($idx = y \times W + x$), cada hilo recorre direcciones de memoria físicamente consecutivas. Esto maximiza el aprovechamiento del *Hardware Prefetcher* del procesador y reduce drásticamente las fallas de caché (*cache misses*).
* **Inexistencia de Falso Compartimiento (*False Sharing*):** Las fronteras entre hilos se producen únicamente a nivel de filas completas. Como una fila de 512 o 1024 floats ocupa varios kilobytes (muy por encima de los 64 bytes de una línea de caché típica), los hilos jamás invalidan las líneas de caché de sus vecinos durante la fase de escritura.
* **Sobrecarga de Sincronización Mínima:** Solo se requiere una barrera global de sincronización al final de cada paso temporal antes de intercambiar los punteros del doble búfer (`swap`).

### Desventajas:
* La escalabilidad queda limitada si el número de procesadores excede la cantidad de filas disponibles ($N > H$).

---

## 3. Propuesta 2: Descomposición de Dominio 2D (Por Mosaicos / Tiles)

### Descripción:
Se divide la matriz en una cuadrícula bidimensional de bloques cuadrados de tamaño fijo $B \times B$ (por ejemplo, bloques de $32 \times 32$ o $64 \times 64$). Los hilos toman la responsabilidad de computar bloques individuales distribuidos estáticamente o mediante una cola dinámica de tareas.

### Ventajas Técnicas:
* **Razón Perímetro / Superficie Favorable:** Teóricamente, un bloque cuadrado de área $B^2$ minimiza el número de celdas perimetrales ($4B$) en relación con las celdas interiores computadas, lo cual es ventajoso en sistemas distribuidos con alto coste de comunicación entre nodos.
* **Granularidad Fina y Balanceo Dinámico:** Permite la implementación de esquemas *Work-Stealing* en arquitecturas con desbalance de carga heterogéneo.

### Desventajas:
* **Degradación de Localidad Espacial:** Al procesar un bloque verticalmente, el puntero de memoria debe dar saltos periódicos de $W$ elementos para pasar a la siguiente línea del bloque, rompiendo el flujo secuencial en RAM e incrementando los fallos en la caché L1.
* **Riesgo Severo de False Sharing en Bordes Verticales:** Las celdas limítrofes entre dos bloques adyacentes en el eje $X$ pueden residir en la misma línea de caché de 64 bytes (16 elementos float de 4 bytes). La escritura concurrente de ambos hilos sobre la misma línea generará invalidaciones continuas en el protocolo de coherencia (MESI/MOESI), degradando el rendimiento.

---

## 4. Tabla Comparativa de Criterios

| Criterio Arquitectónico | Propuesta 1: Descomposición 1D (Filas) | Propuesta 2: Descomposición 2D (Mosaicos) |
|---|---|---|
| **Alineamiento con Memoria Row-Major** | **Óptimo** (Accesos lineales continuos) | **Pobre** (Accesos con zancada / stride) |
| **Aprovechamiento de Caché L1/L2** | **Máximo** (Prefetching automático de líneas de 64 B) | **Subóptimo** (Saltos de dirección recurrentes) |
| **Mitigación de False Sharing** | **Garantizada** (Fronteras separadas por miles de bytes) | **Alta probabilidad** en límites de columnas |
| **Sobrecarga de Sincronización** | **Baja** (1 barrera por iteración temporal) | **Media/Alta** (Gestión de mosaicos o colas) |
| **Complejidad de Implementación** | **Baja / Robusta** | **Alta** |

---

## 5. Selección y Justificación del Diseño

Para las implementaciones multihilo (**CMP**, **SMT** y **Coarse-Grained**) se selecciona la **Propuesta 1 (Descomposición 1D por Filas)**.

**Justificación:** El algoritmo de Gray-Scott es un problema clasificado como **Memory-Bound** (limitado por el ancho de banda y la latencia del subsistema de memoria). Dado que la intensidad aritmética por celda es moderada, la velocidad de ejecución depende directamente de qué tan rápido los núcleos pueden leer del búfer actual y escribir en el nuevo búfer. La descomposición 1D respeta de manera natural el mapeo lineal en RAM, eliminando el falso compartimiento y permitiendo que los núcleos físicos trabajen a máxima tasa de transferencia sin fricciones en el bus de coherencia de caché.