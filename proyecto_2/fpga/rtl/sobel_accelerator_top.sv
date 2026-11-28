// ============================================================================
// Proyecto Grupal 2 - Framework Experimental DLP (Sobel RGBA32)
// Archivo: sobel_accelerator_top.sv
// Descripcion:
//   Modulo top-level del acelerador Sobel RGBA32 DLP de 4 lanes en paralelo
//   para Cyclone V (Terasic DE1-SoC).
//   Integra exactamente la arquitectura de bloques aprobada:
//     1. sobel_control_fsm  -> FSM + ISA minimo + punteros wr_ptr/rd_ptr/out_wr_ptr
//     2. cycle_counter      -> Medicion exacta de ciclos activos en ST_RUN
//     3. BRAM_IN  (M10K)    -> 8192 x 128b (128 KB) imagen de entrada del tile
//     4. line_buffer_4lane  -> 2x LineBuf (960 x 128b) + ventana 3x6 + zero-pad
//     5. 4x sobel_core_rgb  -> 4 datapaths Sobel R/G/B en paralelo (2 etapas)
//     6. BRAM_OUT (M10K)    -> 8192 x 128b (128 KB) imagen procesada del tile
// ============================================================================

module sobel_accelerator_top
    import sobel_isa_pkg::*;
#(
    parameter int P_MAX_WIDTH  = MAX_WIDTH,
    parameter int P_BRAM_DEPTH = BRAM_DEPTH
) (
    input  logic                     clk,
    input  logic                     rst_n,

    // Interfaz de comandos del ISA minimo (desde jtag_bridge en dominio clk_sys)
    input  logic                     cmd_valid_i,
    input  isa_opcode_t              cmd_opcode_i,
    input  logic [DIM_WIDTH-1:0]     cfg_width_i,
    input  logic [DIM_WIDTH-1:0]     cfg_height_i,

    // Interfaz de datos 128 bits (4 pixeles RGBA32) hacia/desde jtag_bridge
    input  logic [WORD_WIDTH-1:0]    jtag_wr_data_i,
    output logic [WORD_WIDTH-1:0]    jtag_rd_data_o,

    // Salidas de estado y perfilado (ISA_STATUS y LEDs de diagnostico)
    output accel_state_t             state_o,
    output logic [31:0]              status_reg_o,
    output logic [COUNTER_WIDTH-1:0] cycle_count_o,
    output logic [31:0]              processed_words_o
);

    localparam int P_BRAM_ADDRW    = $clog2(P_BRAM_DEPTH);
    localparam int P_MAX_WORDS_ROW = P_MAX_WIDTH / LANES;

    // ------------------------------------------------------------------------
    // Senales internas entre modulos
    // ------------------------------------------------------------------------
    logic [DIM_WIDTH-1:0]    img_width;
    logic [DIM_WIDTH-1:0]    img_height;
    logic [WPR_WIDTH-1:0]    words_per_row;

    logic                    bram_in_we;
    logic [P_BRAM_ADDRW-1:0] wr_ptr;
    logic [P_BRAM_ADDRW-1:0] rd_ptr;
    logic                    bram_out_we;
    logic [P_BRAM_ADDRW-1:0] out_wr_ptr;

    logic                    datapath_en;
    logic                    datapath_clear;
    logic                    counter_en;
    logic                    counter_clear;

    logic [P_BRAM_ADDRW-1:0] accel_rd_addr;
    logic [WORD_WIDTH-1:0]   bram_in_rd_data;

    logic                                          win_valid;
    logic [LANES-1:0][2:0][2:0][PIXEL_WIDTH-1:0]   win_lanes;

    logic [LANES-1:0]                              lane_out_valid;
    logic [LANES-1:0][PIXEL_WIDTH-1:0]             lane_out_px;
    logic                                          word_out_valid;
    logic [WORD_WIDTH-1:0]                         dout_128;

    // ------------------------------------------------------------------------
    // 1. FSM de control + decodificador ISA minimo + punteros de BRAM
    // ------------------------------------------------------------------------
    sobel_control_fsm #(
        .P_MAX_WIDTH  (P_MAX_WIDTH),
        .P_BRAM_DEPTH (P_BRAM_DEPTH),
        .P_BRAM_ADDRW (P_BRAM_ADDRW)
    ) u_control_fsm (
        .clk               (clk),
        .rst_n             (rst_n),
        .cmd_valid_i       (cmd_valid_i),
        .cmd_opcode_i      (cmd_opcode_i),
        .cfg_width_i       (cfg_width_i),
        .cfg_height_i      (cfg_height_i),
        .word_out_valid_i  (word_out_valid),
        .img_width_o       (img_width),
        .img_height_o      (img_height),
        .words_per_row_o   (words_per_row),
        .bram_in_we_o      (bram_in_we),
        .wr_ptr_o          (wr_ptr),
        .rd_ptr_o          (rd_ptr),
        .bram_out_we_o     (bram_out_we),
        .out_wr_ptr_o      (out_wr_ptr),
        .datapath_en_o     (datapath_en),
        .datapath_clear_o  (datapath_clear),
        .counter_en_o      (counter_en),
        .counter_clear_o   (counter_clear),
        .state_o           (state_o),
        .status_reg_o      (status_reg_o),
        .processed_words_o (processed_words_o)
    );

    // ------------------------------------------------------------------------
    // 2. Contador de ciclos para perfilado sin sesgo de transferencia JTAG
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
    // 3. BRAM_IN (M10K Simple Dual-Port, 128 KB):
    //      Puerto A -> escritura desde Host/JTAG (wr_ptr)
    //      Puerto B -> lectura hacia line_buffer_4lane (accel_rd_addr)
    // ------------------------------------------------------------------------
    bram_simple_dp #(
        .DATA_WIDTH (WORD_WIDTH),
        .DEPTH      (P_BRAM_DEPTH),
        .ADDR_WIDTH (P_BRAM_ADDRW)
    ) u_bram_in (
        .clk      (clk),
        .we_a_i   (bram_in_we),
        .addr_a_i (wr_ptr),
        .din_a_i  (jtag_wr_data_i),
        .addr_b_i (accel_rd_addr),
        .dout_b_o (bram_in_rd_data)
    );

    // ------------------------------------------------------------------------
    // 4. Line Buffers en M10K + Ventana deslizante 3x6 + Zero-Padding
    // ------------------------------------------------------------------------
    line_buffer_4lane #(
        .P_MAX_WORDS_ROW (P_MAX_WORDS_ROW),
        .P_BRAM_ADDRW    (P_BRAM_ADDRW)
    ) u_line_buffer_4lane (
        .clk             (clk),
        .rst_n           (rst_n),
        .clear_i         (datapath_clear),
        .enable_i        (datapath_en),
        .img_width_i     (img_width),
        .img_height_i    (img_height),
        .words_per_row_i (words_per_row),
        .bram_in_addr_o  (accel_rd_addr),
        .bram_in_data_i  (bram_in_rd_data),
        .win_valid_o     (win_valid),
        .win_o           (win_lanes)
    );

    // ------------------------------------------------------------------------
    // 5. Datapath Sobel DLP: 4 instancias de `sobel_core_rgb` en paralelo
    // ------------------------------------------------------------------------
    genvar g;
    generate
        for (g = 0; g < LANES; g++) begin : gen_sobel_lanes
            sobel_core_rgb u_sobel_core (
                .clk         (clk),
                .rst_n       (rst_n),
                .clear_i     (datapath_clear),
                .valid_i     (win_valid),
                .w00_i       (win_lanes[g][0][0]),
                .w01_i       (win_lanes[g][0][1]),
                .w02_i       (win_lanes[g][0][2]),
                .w10_i       (win_lanes[g][1][0]),
                .w11_i       (win_lanes[g][1][1]),
                .w12_i       (win_lanes[g][1][2]),
                .w20_i       (win_lanes[g][2][0]),
                .w21_i       (win_lanes[g][2][1]),
                .w22_i       (win_lanes[g][2][2]),
                .valid_o     (lane_out_valid[g]),
                .pixel_out_o (lane_out_px[g])
            );
        end
    endgenerate

    // Todos los 4 nucleos comparten latencia fija de 2 ciclos
    assign word_out_valid = lane_out_valid[0];
    assign dout_128       = {lane_out_px[3], lane_out_px[2], lane_out_px[1], lane_out_px[0]};

    // ------------------------------------------------------------------------
    // 6. BRAM_OUT (M10K Simple Dual-Port, 128 KB):
    //      Puerto A -> escritura sincronizada desde 4x Sobel (out_wr_ptr)
    //      Puerto B -> lectura hacia Host/JTAG (rd_ptr)
    // ------------------------------------------------------------------------
    bram_simple_dp #(
        .DATA_WIDTH (WORD_WIDTH),
        .DEPTH      (P_BRAM_DEPTH),
        .ADDR_WIDTH (P_BRAM_ADDRW)
    ) u_bram_out (
        .clk      (clk),
        .we_a_i   (bram_out_we),
        .addr_a_i (out_wr_ptr),
        .din_a_i  (dout_128),
        .addr_b_i (rd_ptr),
        .dout_b_o (jtag_rd_data_o)
    );

endmodule : sobel_accelerator_top
