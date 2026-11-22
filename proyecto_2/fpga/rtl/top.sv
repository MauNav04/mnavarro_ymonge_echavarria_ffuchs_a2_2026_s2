// ============================================================================
// Proyecto Grupal 2 - Framework Experimental DLP (Sobel RGBA32)
// Archivo: top.sv
// Descripcion:
//   Modulo top-level para la placa Terasic DE1-SoC (Avance 2 - Dummy).
//   Permite probar en hardware real el flujo ISA (CONFIG -> START -> DONE)
//   usando los botones y switches de la placa, e inyecta un patron sintetico
//   4x4 para verificar el datapath Sobel y el contador de ciclos en LEDR.
//
// Controles fisicos en DE1-SoC:
//   - CLOCK_50 : Reloj principal de 50 MHz
//   - KEY[0]   : Reset activo en bajo
//   - KEY[1]   : Pulso ISA_CONFIG (configura frame dummy de 4x4)
//   - KEY[2]   : Pulso ISA_START  (inicia inyeccion de pixeles y conteo)
//   - SW[0]    : Patron dummy (0 = borde vertical, 1 = imagen constante)
//
// Indicadores LEDR[9:0]:
//   - LEDR[2:0] : Estado actual de la FSM (0=IDLE, 1=CFG, 2=RUN, 3=DONE, 4=ERR)
//   - LEDR[3]   : Bandera BUSY
//   - LEDR[4]   : Bandera DONE
//   - LEDR[5]   : Ultimo pixel de salida tuvo canal R > 0 (borde detectado)
//   - LEDR[9:6] : Bits bajos del contador de ciclos [3:0]
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

    // Sincronizadores y detectores de flanco de bajada para botones KEY[1] y KEY[2]
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

    // Generacion de comandos ISA desde los botones
    logic        cmd_valid;
    isa_opcode_t cmd_opcode;

    always_comb begin
        cmd_valid  = 1'b0;
        cmd_opcode = ISA_NOP;
        if (key1_fall) begin
            cmd_valid  = 1'b1;
            cmd_opcode = ISA_CONFIG;
        end else if (key2_fall) begin
            cmd_valid  = 1'b1;
            cmd_opcode = ISA_START;
        end
    end

    // Generador dummy de stream de pixeles mientras la FSM este en ST_RUN
    accel_state_t             state;
    logic [31:0]              status_reg;
    logic [COUNTER_WIDTH-1:0] cycle_count;
    logic [31:0]              processed_pixels;
    logic                     pixel_out_valid;
    logic [PIXEL_WIDTH-1:0]   pixel_out;

    logic [4:0]               feed_cnt_q;
    logic                     pixel_in_valid;
    logic [PIXEL_WIDTH-1:0]   pixel_in;
    logic [PIXEL_WIDTH-1:0]   last_out_q;

    always_ff @(posedge CLOCK_50 or negedge rst_n) begin
        if (!rst_n) begin
            feed_cnt_q <= '0;
            last_out_q <= '0;
        end else begin
            if (state == ST_RUN) begin
                feed_cnt_q <= feed_cnt_q + 5'd1;
            end else begin
                feed_cnt_q <= '0;
            end

            if (pixel_out_valid) begin
                last_out_q <= pixel_out;
            end
        end
    end

    assign pixel_in_valid = (state == ST_RUN);

    // Patron dummy:
    //   SW[0] == 0 -> borde vertical (mitad izq=0, mitad der=255 en columnas 2..3)
    //   SW[0] == 1 -> imagen constante (R=100, G=150, B=200) -> salida interior = 0
    always_comb begin
        if (SW[0]) begin
            pixel_in = pack_rgba(8'd100, 8'd150, 8'd200, 8'd255);
        end else begin
            if (feed_cnt_q[1:0] < 2'd2) begin
                pixel_in = pack_rgba(8'd0, 8'd0, 8'd0, 8'd255);
            end else begin
                pixel_in = pack_rgba(8'd255, 8'd255, 8'd255, 8'd255);
            end
        end
    end

    // Instancia del acelerador con imagen dummy de 4x4 (16 pixeles)
    sobel_accelerator_top #(
        .MAX_WIDTH (16)
    ) u_accel (
        .clk                (CLOCK_50),
        .rst_n              (rst_n),
        .cmd_valid_i        (cmd_valid),
        .cmd_opcode_i       (cmd_opcode),
        .cfg_width_i        (16'd4),
        .cfg_height_i       (16'd4),
        .cfg_src_addr_i     (32'h0000_0000),
        .cfg_dst_addr_i     (32'h0010_0000),
        .pixel_in_valid_i   (pixel_in_valid),
        .pixel_in_i         (pixel_in),
        .pixel_out_valid_o  (pixel_out_valid),
        .pixel_out_o        (pixel_out),
        .state_o            (state),
        .status_reg_o       (status_reg),
        .cycle_count_o      (cycle_count),
        .processed_pixels_o (processed_pixels)
    );

    // Mapeo visual a LEDs de la DE1-SoC
    assign LEDR[2:0] = state;
    assign LEDR[3]   = status_reg[STATUS_BIT_BUSY];
    assign LEDR[4]   = status_reg[STATUS_BIT_DONE];
    assign LEDR[5]   = (rgba_r(last_out_q) != 8'd0);
    assign LEDR[9:6] = cycle_count[3:0];

endmodule : top
