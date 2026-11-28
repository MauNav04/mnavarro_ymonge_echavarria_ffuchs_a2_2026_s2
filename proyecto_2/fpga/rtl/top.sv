// ============================================================================
// Proyecto Grupal 2 - Framework Experimental DLP (Sobel RGBA32)
// Archivo: top.sv
// Descripcion:
//   Envoltorio temporal de Fase 1 para la placa Terasic DE1-SoC.
//   Instancia el acelerador completo 4-lane con BRAM_IN y BRAM_OUT (128 KB c/u)
//   y permite sintetizarlo en Quartus Prime 20.1 para verificar recursos M10K
//   y timing antes de conectar Virtual JTAG (Fase 2) y PLL 100 MHz (Fase 3).
//
// Controles fisicos en DE1-SoC (prueba local previa a JTAG):
//   - CLOCK_50 : Reloj principal de 50 MHz
//   - KEY[0]   : Reset activo en bajo
//   - KEY[1]   : Configura 4x4 (ISA_CONFIG) y carga 4 filas en BRAM_IN
//   - KEY[2]   : Ejecuta ISA_START y muestra estado + ciclos en LEDR
//   - SW[0]    : Patron de entrada (0 = borde vertical, 1 = imagen constante)
// ============================================================================

module top
    import sobel_isa_pkg::*;
(
    input  logic       CLOCK_50,
    input  logic [3:0] KEY,
    input  logic [9:0] SW,
    output logic [9:0] LEDR
);

    logic rst_n;
    assign rst_n = KEY[0];

    // Sincronizadores y detectores de flanco de bajada para KEY[1] y KEY[2]
    logic [1:0] key1_sync, key2_sync;
    logic       key1_fall, key2_fall;

    always_ff @(posedge CLOCK_50 or negedge rst_n) begin
        if (!rst_n) begin
            key1_sync <= 2'b11;
            key2_sync <= 2'b11;
        end else begin
            key1_sync <= {key1_sync[0], KEY[1]};
            key2_sync <= {key2_sync[0], KEY[2]};
        end
    end

    assign key1_fall = (key1_sync == 2'b10);
    assign key2_fall = (key2_sync == 2'b10);

    // Mini secuenciador de carga local de 4 palabras (frame 4x4) tras KEY[1]
    logic [2:0] load_cnt_q;
    logic       loading_q;

    always_ff @(posedge CLOCK_50 or negedge rst_n) begin
        if (!rst_n) begin
            loading_q  <= 1'b0;
            load_cnt_q <= 3'd0;
        end else if (key1_fall) begin
            loading_q  <= 1'b1;
            load_cnt_q <= 3'd0;
        end else if (loading_q) begin
            if (load_cnt_q == 3'd3) begin
                loading_q <= 1'b0;
            end else begin
                load_cnt_q <= load_cnt_q + 3'd1;
            end
        end
    end

    // Generacion de comandos ISA hacia el acelerador 4-lane
    logic                  cmd_valid;
    isa_opcode_t           cmd_opcode;
    logic [WORD_WIDTH-1:0] wr_word;
    logic [WORD_WIDTH-1:0] rd_word;

    always_comb begin
        cmd_valid  = 1'b0;
        cmd_opcode = ISA_NOP;
        if (key1_fall) begin
            cmd_valid  = 1'b1;
            cmd_opcode = ISA_CONFIG;
        end else if (loading_q) begin
            cmd_valid  = 1'b1;
            cmd_opcode = ISA_WRITE_DATA;
        end else if (key2_fall) begin
            cmd_valid  = 1'b1;
            cmd_opcode = ISA_START;
        end
    end

    // Patron 4 pixeles/fila (128 bits):
    //   SW[0] == 0 -> borde vertical (px0=0, px1=0, px2=255, px3=255)
    //   SW[0] == 1 -> imagen constante (100, 150, 200, 255)
    logic [31:0] px_const, px_black, px_white;
    assign px_const = pack_rgba(8'd100, 8'd150, 8'd200, 8'd255);
    assign px_black = pack_rgba(8'd0,   8'd0,   8'd0,   8'd255);
    assign px_white = pack_rgba(8'd255, 8'd255, 8'd255, 8'd255);
    assign wr_word  = SW[0] ? {px_const, px_const, px_const, px_const}
                            : {px_white, px_white, px_black, px_black};

    // Instancia del acelerador completo 4-lane con BRAM M10K real (128 KB x 2)
    accel_state_t             state;
    logic [31:0]              status_reg;
    logic [COUNTER_WIDTH-1:0] cycle_count;
    logic [31:0]              processed_words;

    sobel_accelerator_top #(
        .P_MAX_WIDTH  (MAX_WIDTH),
        .P_BRAM_DEPTH (BRAM_DEPTH)
    ) u_accel (
        .clk               (CLOCK_50),
        .rst_n             (rst_n),
        .cmd_valid_i       (cmd_valid),
        .cmd_opcode_i      (cmd_opcode),
        .cfg_width_i       (16'd4),
        .cfg_height_i      (16'd4),
        .jtag_wr_data_i    (wr_word),
        .jtag_rd_data_o    (rd_word),
        .state_o           (state),
        .status_reg_o      (status_reg),
        .cycle_count_o     (cycle_count),
        .processed_words_o (processed_words)
    );

    // Mapeo visual a LEDs de la DE1-SoC
    assign LEDR[2:0] = state;
    assign LEDR[3]   = status_reg[STATUS_BIT_BUSY];
    assign LEDR[4]   = status_reg[STATUS_BIT_DONE];
    assign LEDR[5]   = (rgba_r(rd_word[63:32]) != 8'd0); // px1 de fila 0
    assign LEDR[9:6] = cycle_count[3:0];

endmodule : top
