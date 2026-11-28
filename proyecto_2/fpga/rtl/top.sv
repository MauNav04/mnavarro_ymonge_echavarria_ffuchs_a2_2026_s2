// ============================================================================
// Proyecto Grupal 2 - Framework Experimental DLP (Sobel RGBA32)
// Archivo: top.sv
// Descripcion:
//   Modulo de nivel superior para la placa Terasic DE1-SoC (Cyclone V
//   5CSEMA5F31C6). Conecta:
//     1. `vjtag_wrapper`         -> Intel Virtual JTAG IP (IR de 4 bits)
//     2. `jtag_bridge`           -> Shift-register 128b + CDC 3-FF (tck -> clk)
//     3. `sobel_accelerator_top` -> FSM + cycle_counter + BRAM_IN/OUT (128 KB
//                                   c/u) + line_buffer_4lane + 4x sobel_core_rgb
//
//   (En Fase 3 se conectara el PLL de 100 MHz para reemplazar CLOCK_50).
//
// Controles fisicos e indicadores en DE1-SoC:
//   - CLOCK_50  : Reloj base de 50 MHz
//   - KEY[0]    : Reset fisico activo en bajo
//   - LEDR[2:0] : Estado de la FSM (0=IDLE, 1=CFG, 2=RUN, 3=DONE, 4=ERR)
//   - LEDR[3]   : Bandera BUSY
//   - LEDR[4]   : Bandera DONE
//   - LEDR[5]   : Bandera ERROR
//   - LEDR[6]   : Actividad JTAG (conmuta en cada comando recibido)
//   - LEDR[9:7] : 3 bits bajos de `cycle_count[2:0]`
// ============================================================================

module top
    import sobel_isa_pkg::*;
(
    input  logic       CLOCK_50,
    input  logic [3:0] KEY,
    input  logic [9:0] SW,
    output logic [9:0] LEDR
);

    logic clk_sys;
    logic rst_n;

    assign clk_sys = CLOCK_50;
    assign rst_n   = KEY[0];

    // Pines sin uso en modo JTAG (evita warnings de puertos desconectados)
    logic unused_io;
    assign unused_io = (&KEY[3:1]) & (&SW);

    // ------------------------------------------------------------------------
    // Senales Virtual JTAG (dominio `tck`)
    // ------------------------------------------------------------------------
    logic       jtag_tck;
    logic       jtag_tdi;
    logic       jtag_tdo;
    logic [3:0] jtag_ir_in;
    logic       jtag_v_cdr;
    logic       jtag_v_sdr;
    logic       jtag_v_e1dr;
    logic       jtag_v_pdr;
    logic       jtag_v_e2dr;
    logic       jtag_v_udr;
    logic       jtag_v_cir;
    logic       jtag_v_uir;

    vjtag_wrapper u_vjtag (
        .tdo_i    (jtag_tdo),
        .ir_out_i (4'b0000),
        .tck_o    (jtag_tck),
        .tdi_o    (jtag_tdi),
        .ir_in_o  (jtag_ir_in),
        .v_cdr_o  (jtag_v_cdr),
        .v_sdr_o  (jtag_v_sdr),
        .v_e1dr_o (jtag_v_e1dr),
        .v_pdr_o  (jtag_v_pdr),
        .v_e2dr_o (jtag_v_e2dr),
        .v_udr_o  (jtag_v_udr),
        .v_cir_o  (jtag_v_cir),
        .v_uir_o  (jtag_v_uir)
    );

    // ------------------------------------------------------------------------
    // Puente JTAG + CDC 3-FF (`tck` -> `clk_sys`)
    // ------------------------------------------------------------------------
    logic                     cmd_valid;
    isa_opcode_t              cmd_opcode;
    logic [DIM_WIDTH-1:0]     cfg_width;
    logic [DIM_WIDTH-1:0]     cfg_height;
    logic                     jtag_wr_en;
    logic [WORD_WIDTH-1:0]    jtag_wr_data;
    logic [WORD_WIDTH-1:0]    jtag_rd_data;
    logic                     jtag_activity;

    accel_state_t             state;
    logic [31:0]              status_reg;
    logic [COUNTER_WIDTH-1:0] cycle_count;
    logic [31:0]              processed_words;

    jtag_bridge u_jtag_bridge (
        .clk_sys         (clk_sys),
        .rst_n           (rst_n),
        .tck             (jtag_tck),
        .tdi             (jtag_tdi),
        .ir_in           (jtag_ir_in),
        .v_cdr           (jtag_v_cdr),
        .v_sdr           (jtag_v_sdr),
        .v_udr           (jtag_v_udr),
        .v_uir           (jtag_v_uir),
        .tdo             (jtag_tdo),
        .status_reg_i    (status_reg),
        .cycle_count_i   (cycle_count),
        .rd_data_i       (jtag_rd_data),
        .cmd_valid_o     (cmd_valid),
        .cmd_opcode_o    (cmd_opcode),
        .cfg_width_o     (cfg_width),
        .cfg_height_o    (cfg_height),
        .jtag_wr_en_o    (jtag_wr_en),
        .wr_data_o       (jtag_wr_data),
        .jtag_activity_o (jtag_activity)
    );

    // ------------------------------------------------------------------------
    // Acelerador Sobel DLP 4-Lane + BRAM_IN / BRAM_OUT (M10K)
    // ------------------------------------------------------------------------
    sobel_accelerator_top #(
        .P_MAX_WIDTH  (MAX_WIDTH),
        .P_BRAM_DEPTH (BRAM_DEPTH)
    ) u_accel (
        .clk               (clk_sys),
        .rst_n             (rst_n),
        .cmd_valid_i       (cmd_valid),
        .cmd_opcode_i      (cmd_opcode),
        .cfg_width_i       (cfg_width),
        .cfg_height_i      (cfg_height),
        .jtag_wr_data_i    (jtag_wr_data),
        .jtag_rd_data_o    (jtag_rd_data),
        .state_o           (state),
        .status_reg_o      (status_reg),
        .cycle_count_o     (cycle_count),
        .processed_words_o (processed_words)
    );

    // ------------------------------------------------------------------------
    // LEDs de diagnostico en tiempo real en la DE1-SoC
    // ------------------------------------------------------------------------
    assign LEDR[2:0] = state;
    assign LEDR[3]   = status_reg[STATUS_BIT_BUSY];
    assign LEDR[4]   = status_reg[STATUS_BIT_DONE];
    assign LEDR[5]   = status_reg[STATUS_BIT_ERROR];
    assign LEDR[6]   = jtag_activity;
    assign LEDR[9:7] = cycle_count[2:0];

endmodule : top
