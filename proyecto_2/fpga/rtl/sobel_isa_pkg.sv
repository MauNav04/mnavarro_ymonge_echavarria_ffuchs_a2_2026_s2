// ============================================================================
// Proyecto Grupal 2 - Framework Experimental DLP (Sobel RGBA32)
// Archivo: sobel_isa_pkg.sv
// Descripcion:
//   Definicion del ISA minimo (IR de 4 bits), mapa de estados de la FSM,
//   bits de STATUS y parametros de la arquitectura DLP de 4 lanes en paralelo
//   con memoria interna BRAM M10K para la Terasic DE1-SoC (Cyclone V).
//
// Convencion de formato RGBA32 (little-endian, compatible con ImageRGBA C++):
//   bits [ 7: 0] -> Canal R (8 bits sin signo)
//   bits [15: 8] -> Canal G (8 bits sin signo)
//   bits [23:16] -> Canal B (8 bits sin signo)
//   bits [31:24] -> Canal A (en salida siempre 8'hFF = 255)
//
// Convencion de palabra ancha (4 lanes = 128 bits):
//   bits [ 31: 0] -> px0 (columna 4*w + 0)
//   bits [ 63:32] -> px1 (columna 4*w + 1)
//   bits [ 95:64] -> px2 (columna 4*w + 2)
//   bits [127:96] -> px3 (columna 4*w + 3)
// ============================================================================

package sobel_isa_pkg;

    // ------------------------------------------------------------------------
    // Parametros de formato de pixel y paralelismo DLP
    // ------------------------------------------------------------------------
    localparam int PIXEL_WIDTH   = 32;
    localparam int CHANNEL_WIDTH = 8;
    localparam int LANES         = 4;
    localparam int WORD_WIDTH    = LANES * PIXEL_WIDTH;  // 128 bits (4 pixeles)

    localparam int DIM_WIDTH     = 16;   // Dimensiones width/height (16 bits c/u)
    localparam int COUNTER_WIDTH = 32;   // Contador de ciclos (32 bits)

    // ------------------------------------------------------------------------
    // Parametros de memoria interna BRAM M10K (Terasic DE1-SoC 5CSEMA5F31C6)
    //   - BRAM_IN  : 8192 x 128b = 128 KB (32,768 pixeles RGBA32 por tile)
    //   - BRAM_OUT : 8192 x 128b = 128 KB (32,768 pixeles RGBA32 por tile)
    //   - LineBufs : 2 x (960 x 128b) -> soporta hasta width = 3840 (4K)
    // ------------------------------------------------------------------------
    localparam int MAX_WIDTH     = 3840;
    localparam int MAX_WORDS_ROW = MAX_WIDTH / LANES;    // 960 palabras/fila
    localparam int LB_ADDR_W     = $clog2(MAX_WORDS_ROW); // 10 bits

    localparam int BRAM_DEPTH    = 8192;
    localparam int BRAM_ADDR_W   = $clog2(BRAM_DEPTH);   // 13 bits
    localparam int WPR_WIDTH     = 14;                   // ceil(width / 4)

    // ------------------------------------------------------------------------
    // ISA minimo del acelerador (opcodes IR de 4 bits — Virtual JTAG)
    // ------------------------------------------------------------------------
    typedef enum logic [3:0] {
        ISA_NOP        = 4'h0,  // DR  1b: Bypass (sin efecto)
        ISA_CONFIG     = 4'h1,  // DR 32b: {width[15:0], height[15:0]}
        ISA_START      = 4'h2,  // DR  1b: Inicia ST_RUN y cycle_counter
        ISA_STATUS     = 4'h3,  // DR 64b: {status_reg[31:0], cycle_count[31:0]}
        ISA_WRITE_DATA = 4'h4,  // DR 128b: Escribe 4 pixeles en BRAM_IN[wr_ptr++]
        ISA_READ_DATA  = 4'h5,  // DR 128b: Lee 4 pixeles de BRAM_OUT[rd_ptr++]
        ISA_RESET      = 4'hF   // DR  1b: Reinicio por software
    } isa_opcode_t;

    // ------------------------------------------------------------------------
    // Estados principales de la FSM de control del acelerador
    // ------------------------------------------------------------------------
    typedef enum logic [2:0] {
        ST_IDLE       = 3'd0,  // Esperando configuracion (ISA_CONFIG)
        ST_CONFIGURED = 3'd1,  // Parametros validos cargados; listo para WRITE/START
        ST_RUN        = 3'd2,  // Procesando tile en 4 lanes y contando ciclos
        ST_DONE       = 3'd3,  // Tile completo en BRAM_OUT; listo para READ/STATUS
        ST_ERROR      = 3'd4   // Error de configuracion (dim=0 o excede BRAM)
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
