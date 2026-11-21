// ============================================================================
// Proyecto Grupal 2 - Framework Experimental DLP (Sobel RGBA32)
// Archivo: sobel_isa_pkg.sv
// Descripcion:
//   Definicion del ISA minimo, mapa de comandos, bits de estado y constantes
//   del formato RGBA32 para el acelerador Sobel en FPGA (Terasic DE1-SoC).
//
// Convencion de formato RGBA32 (little-endian, compatible con ImageRGBA C++):
//   bits [ 7: 0] -> Canal R (8 bits sin signo)
//   bits [15: 8] -> Canal G (8 bits sin signo)
//   bits [23:16] -> Canal B (8 bits sin signo)
//   bits [31:24] -> Canal A (salida fija en 8'hFF = 255)
// ============================================================================

package sobel_isa_pkg;

    // ------------------------------------------------------------------------
    // Parametros generales del formato de pixel y arquitectura
    // ------------------------------------------------------------------------
    localparam int PIXEL_WIDTH   = 32;
    localparam int CHANNEL_WIDTH = 8;
    localparam int DIM_WIDTH     = 16;   // Soporta imagenes hasta 65535 x 65535
    localparam int ADDR_WIDTH    = 32;   // Direccionamiento de SDRAM externa (64 MB)
    localparam int COUNTER_WIDTH = 32;   // ~85.8 s a 50 MHz antes de desbordar

    // ------------------------------------------------------------------------
    // ISA minimo del acelerador (opcodes de 4 bits)
    // ------------------------------------------------------------------------
    typedef enum logic [3:0] {
        ISA_NOP    = 4'h0,  // Sin operacion
        ISA_CONFIG = 4'h1,  // Carga ancho (W), alto (H) y direcciones base
        ISA_START  = 4'h2,  // Inicia procesamiento y limpia/arranca contador de ciclos
        ISA_STATUS = 4'h3,  // Consulta registro de estado y contador de ciclos
        ISA_RESET  = 4'hF   // Reinicio por software de la FSM y contadores
    } isa_opcode_t;

    // ------------------------------------------------------------------------
    // Estados principales de la FSM de control del acelerador
    // ------------------------------------------------------------------------
    typedef enum logic [2:0] {
        ST_IDLE       = 3'd0,  // Esperando configuracion (ISA_CONFIG)
        ST_CONFIGURED = 3'd1,  // Parametros validos cargados; listo para ISA_START
        ST_RUN        = 3'd2,  // Procesando stream de pixeles y contando ciclos
        ST_DONE       = 3'd3,  // Frame completo; resultado y ciclos disponibles
        ST_ERROR      = 3'd4   // Error de configuracion (ej. W=0 o H=0)
    } accel_state_t;

    // ------------------------------------------------------------------------
    // Posiciones de bits del registro STATUS (32 bits)
    // ------------------------------------------------------------------------
    localparam int STATUS_BIT_IDLE       = 0;
    localparam int STATUS_BIT_CONFIGURED = 1;
    localparam int STATUS_BIT_BUSY       = 2;
    localparam int STATUS_BIT_DONE       = 3;
    localparam int STATUS_BIT_ERROR      = 4;

    // ------------------------------------------------------------------------
    // Funciones auxiliares de extraccion/empaquetado RGBA32
    // ------------------------------------------------------------------------
    function automatic logic [7:0] rgba_r(input logic [31:0] px);
        return px[7:0];
    endfunction

    function automatic logic [7:0] rgba_g(input logic [31:0] px);
        return px[15:8];
    endfunction

    function automatic logic [7:0] rgba_b(input logic [31:0] px);
        return px[23:16];
    endfunction

    function automatic logic [31:0] pack_rgba(
        input logic [7:0] r,
        input logic [7:0] g,
        input logic [7:0] b,
        input logic [7:0] a
    );
        return {a, b, g, r};
    endfunction

endpackage : sobel_isa_pkg
