# Módulo FPGA (`fpga/`)

Diseño del acelerador dedicado para la placa **Terasic DE1-SoC (Cyclone V SoC)**.

* **Componentes esperados:**
  * RTL (Verilog/SystemVerilog/VHDL) del datapath Sobel 3x3 entero + buffers de línea / ventana deslizante.
  * FSM de control e **ISA mínimo** (`CONFIG`, `START`, `STATUS`, contador de ciclos).
  * Interfaz de transferencia y extracción de resultados por **JTAG** / SDRAM.
  * Pruebas de simulación y validación bit a bit frente a `sobel_sequential`.
