// ============================================================================
// Proyecto Grupal 2 - Framework Experimental DLP (Sobel RGBA32)
// Archivo: top.sv
// Descripcion:
//   Modulo de nivel superior final para la placa Terasic DE1-SoC (Cyclone V
//   5CSEMA5F31C6). Integra:
//     1. `pll_100mhz`            -> PLL nativo Cyclone V (50 MHz -> 100 MHz)
//     2. Reset bridge sincrono   -> Liberacion limpia de reset tras PLL lock
//     3. `vjtag_wrapper`         -> Intel Virtual JTAG IP (IR de 4 bits)
//     4. `jtag_bridge`           -> Shift-register 128b + CDC 3-FF (tck -> clk)
//     5. `sobel_accelerator_top` -> FSM + cycle_counter + BRAM_IN/OUT (128 KB
//                                   c/u en M10K) + line_buffer_4lane +
//                                   4x sobel_core_rgb en paralelo
//
// Controles fisicos e indicadores en DE1-SoC:
//   - CLOCK_50  : Reloj base de 50 MHz (alimenta PLL -> clk_sys = 100 MHz)
//   - KEY[0]    : Reset fisico activo en bajo
//   - LEDR[2:0] : Estado de la FSM (0=IDLE, 1=CFG, 2=RUN, 3=DONE, 4=ERR)
//   - LEDR[3]   : Bandera BUSY
//   - LEDR[4]   : Bandera DONE
//   - LEDR[5]   : Bandera ERROR
//   - LEDR[6]   : Actividad JTAG (conmuta en cada comando recibido)
//   - LEDR[7]   : PLL Locked (1 = reloj 100 MHz estable)
//   - LEDR[9:8] : 2 bits bajos de `cycle_count[1:0]`
// ============================================================================

module top
    import sobel_isa_pkg::*;
(
    input  logic       CLOCK_50,
    input  logic [3:0] KEY,
    input  logic [9:0] SW,
    output logic [9:0] LEDR
);

    // ------------------------------------------------------------------------
    // 1. PLL 50 MHz -> 100 MHz y puente de liberacion sincrona de reset
    // ------------------------------------------------------------------------
    logic clk_sys;
    logic pll_locked;
    logic async_rst_n;
    logic [1:0] rst_sync_q;
    logic rst_sys_n;

    pll_100mhz u_pll_100 (
        .refclk_i  (CLOCK_50),
        .rst_i     (~KEY[0]),
        .clk_100_o (clk_sys),
        .locked_o  (pll_locked)
    );

    // Reset asincrono en asercion, sincrono en liberacion respecto a `clk_sys`
    assign async_rst_n = KEY[0] & pll_locked;

    always_ff @(posedge clk_sys or negedge async_rst_n) begin
        if (!async_rst_n) begin
            rst_sync_q <= 2'b00;
        end else begin
            rst_sync_q <= {rst_sync_q[0], 1'b1};
        end
    end

    assign rst_sys_n = rst_sync_q[1];

    // ------------------------------------------------------------------------
    // 2. Virtual JTAG IP (`sld_virtual_jtag`, IR = 4 bits, dominio `tck`)
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
    // 3. Puente JTAG + CDC 3-FF (`tck` -> `clk_sys` 100 MHz)
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
        .rst_n           (rst_sys_n),
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
    // 4. Acelerador Sobel DLP 4-Lane + BRAM_IN / BRAM_OUT (M10K)
    // ------------------------------------------------------------------------
    sobel_accelerator_top #(
        .P_MAX_WIDTH  (MAX_WIDTH),
        .P_BRAM_DEPTH (BRAM_DEPTH)
    ) u_accel (
        .clk               (clk_sys),
        .rst_n             (rst_sys_n),
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
    // 5. LEDs de diagnostico en tiempo real en la DE1-SoC
    // ------------------------------------------------------------------------
    assign LEDR[2:0] = state;
    assign LEDR[3]   = status_reg[STATUS_BIT_BUSY];
    assign LEDR[4]   = status_reg[STATUS_BIT_DONE];
    assign LEDR[5]   = status_reg[STATUS_BIT_ERROR];
    assign LEDR[6]   = jtag_activity;
    assign LEDR[7]   = pll_locked;
    assign LEDR[9:8] = cycle_count[1:0];

endmodule : top
