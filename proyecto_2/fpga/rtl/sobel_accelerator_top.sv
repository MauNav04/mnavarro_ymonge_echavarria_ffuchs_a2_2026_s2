// ============================================================================
// Proyecto Grupal 2 - Framework Experimental DLP (Sobel RGBA32)
// Archivo: sobel_accelerator_top.sv
// Descripcion:
//   Modulo top-level del acelerador Sobel RGBA32 para Cyclone V (DE1-SoC).
//   Integra:
//     1. sobel_control_fsm  -> FSM + ISA minimo (CONFIG/START/STATUS/RESET)
//     2. cycle_counter      -> Medicion exacta de ciclos activos en RUN
//     3. line_buffer_window -> 2 buffers de linea BRAM + ventana deslizante 3x3
//     4. sobel_core_rgb     -> Datapath segmentado Sobel R/G/B + saturacion
// ============================================================================

module sobel_accelerator_top
    import sobel_isa_pkg::*;
#(
    parameter int MAX_WIDTH = 512
) (
    input  logic                     clk,
    input  logic                     rst_n,

    // Interfaz de control e ISA minimo (conectable a JTAG / avalon-mm)
    input  logic                     cmd_valid_i,
    input  isa_opcode_t              cmd_opcode_i,
    input  logic [DIM_WIDTH-1:0]     cfg_width_i,
    input  logic [DIM_WIDTH-1:0]     cfg_height_i,
    input  logic [ADDR_WIDTH-1:0]    cfg_src_addr_i,
    input  logic [ADDR_WIDTH-1:0]    cfg_dst_addr_i,

    // Interfaz de flujo de pixeles RGBA32 (conectable a controlador SDRAM)
    input  logic                     pixel_in_valid_i,
    input  logic [PIXEL_WIDTH-1:0]   pixel_in_i,
    output logic                     pixel_out_valid_o,
    output logic [PIXEL_WIDTH-1:0]   pixel_out_o,

    // Salidas de estado y perfilado
    output accel_state_t             state_o,
    output logic [31:0]              status_reg_o,
    output logic [COUNTER_WIDTH-1:0] cycle_count_o,
    output logic [31:0]              processed_pixels_o
);

    // Senales internas entre bloques
    logic [DIM_WIDTH-1:0]   img_width;
    logic [DIM_WIDTH-1:0]   img_height;
    logic [ADDR_WIDTH-1:0]  src_base_addr;
    logic [ADDR_WIDTH-1:0]  dst_base_addr;

    logic                   datapath_en;
    logic                   datapath_clear;
    logic                   counter_en;
    logic                   counter_clear;

    logic                   window_valid;
    logic [PIXEL_WIDTH-1:0] w00, w01, w02;
    logic [PIXEL_WIDTH-1:0] w10, w11, w12;
    logic [PIXEL_WIDTH-1:0] w20, w21, w22;

    // ------------------------------------------------------------------------
    // 1. FSM de control + decodificador ISA minimo
    // ------------------------------------------------------------------------
    sobel_control_fsm u_control_fsm (
        .clk                (clk),
        .rst_n              (rst_n),
        .cmd_valid_i        (cmd_valid_i),
        .cmd_opcode_i       (cmd_opcode_i),
        .cfg_width_i        (cfg_width_i),
        .cfg_height_i       (cfg_height_i),
        .cfg_src_addr_i     (cfg_src_addr_i),
        .cfg_dst_addr_i     (cfg_dst_addr_i),
        .pixel_out_valid_i  (pixel_out_valid_o),
        .img_width_o        (img_width),
        .img_height_o       (img_height),
        .src_base_addr_o    (src_base_addr),
        .dst_base_addr_o    (dst_base_addr),
        .datapath_en_o      (datapath_en),
        .datapath_clear_o   (datapath_clear),
        .counter_en_o       (counter_en),
        .counter_clear_o    (counter_clear),
        .state_o            (state_o),
        .status_reg_o       (status_reg_o),
        .processed_pixels_o (processed_pixels_o)
    );

    // ------------------------------------------------------------------------
    // 2. Contador de ciclos para perfilado sin sesgo de transferencia
    // ------------------------------------------------------------------------
    cycle_counter #(
        .WIDTH (COUNTER_WIDTH)
    ) u_cycle_counter (
        .clk      (clk),
        .rst_n    (rst_n),
        .clear_i  (counter_clear),
        .enable_i (counter_en),
        .count_o  (cycle_count_o)
    );

    // ------------------------------------------------------------------------
    // 3. Buffers de linea en BRAM + ventana deslizante 3x3
    // ------------------------------------------------------------------------
    line_buffer_window #(
        .MAX_WIDTH (MAX_WIDTH)
    ) u_line_buffer (
        .clk            (clk),
        .rst_n          (rst_n),
        .clear_i        (datapath_clear),
        .enable_i       (datapath_en),
        .img_width_i    (img_width),
        .pixel_valid_i  (pixel_in_valid_i),
        .pixel_in_i     (pixel_in_i),
        .window_valid_o (window_valid),
        .w00_o (w00), .w01_o (w01), .w02_o (w02),
        .w10_o (w10), .w11_o (w11), .w12_o (w12),
        .w20_o (w20), .w21_o (w21), .w22_o (w22)
    );

    // ------------------------------------------------------------------------
    // 4. Datapath Sobel 3x3 multicanal (R, G, B en paralelo) + saturacion
    // ------------------------------------------------------------------------
    sobel_core_rgb u_sobel_core (
        .clk         (clk),
        .rst_n       (rst_n),
        .clear_i     (datapath_clear),
        .valid_i     (window_valid),
        .w00_i (w00), .w01_i (w01), .w02_i (w02),
        .w10_i (w10), .w11_i (w11), .w12_i (w12),
        .w20_i (w20), .w21_i (w21), .w22_i (w22),
        .valid_o     (pixel_out_valid_o),
        .pixel_out_o (pixel_out_o)
    );

endmodule : sobel_accelerator_top
