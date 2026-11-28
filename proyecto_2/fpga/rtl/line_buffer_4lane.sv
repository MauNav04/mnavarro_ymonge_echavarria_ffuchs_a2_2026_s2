// ============================================================================
// Proyecto Grupal 2 - Framework Experimental DLP (Sobel RGBA32)
// Archivo: line_buffer_4lane.sv
// Descripcion:
//   Modulo generador de ventanas deslizantes 3x6 (para 4 lanes Sobel en
//   paralelo) con 2 buffers de linea en BRAM M10K (960 x 128b c/u, hasta 4K).
//
// Caracteristicas arquitectonicas:
//   - Lee BRAM_IN a razon de 1 palabra de 128 bits (4 pixeles RGBA32) por ciclo.
//   - Los 2 line buffers se instancian via `bram_simple_dp` (lectura sincrona
//     de 1 ciclo, 100% inferibles en M10K).
//   - Zero-padding exacto en los 4 bordes del frame (arriba, abajo, izq, der),
//     incluyendo anchos NO multiplos de 4 (ej. 1x1, 7x5, 13x9) mediante
//     enmascaramiento de pixeles >= img_width_i al entrar desde BRAM_IN.
//   - Fase de flush automatica (1 fila virtual + 1 columna virtual por fila):
//       * Emite exactamente `words_per_row * img_height` ventanas validas.
//       * Garantiza cero hazards read-during-write en M10K incluso para
//         words_per_row == 1 (width <= 4).
//
// Organizacion de la ventana 3x6 (18 pixeles en registros Flip-Flop):
//   Para cada fila r in {0 (y-1), 1 (y), 2 (y+1)}:
//     col 0 : prev_px3 (px3 de la palabra anterior, o 0 si out_w == 0)
//     col 1 : cur_word[ 31: 0] (px0)
//     col 2 : cur_word[ 63:32] (px1)
//     col 3 : cur_word[ 95:64] (px2)
//     col 4 : cur_word[127:96] (px3)
//     col 5 : next_word[31:0]  (px0 de la palabra siguiente, o 0 al final)
//
//   Lane k (k = 0..3) recibe la subventana 3x3 formada por cols [k, k+1, k+2].
// ============================================================================

module line_buffer_4lane
    import sobel_isa_pkg::*;
#(
    parameter int P_MAX_WORDS_ROW = MAX_WORDS_ROW, // 960 palabras = 3840 px
    parameter int P_BRAM_ADDRW    = BRAM_ADDR_W    // 13 bits (8192 palabras)
) (
    input  logic                     clk,
    input  logic                     rst_n,
    input  logic                     clear_i,
    input  logic                     enable_i,

    // Dimensiones activas del tile/imagen (configuradas por ISA_CONFIG)
    input  logic [DIM_WIDTH-1:0]     img_width_i,
    input  logic [DIM_WIDTH-1:0]     img_height_i,
    input  logic [WPR_WIDTH-1:0]     words_per_row_i,

    // Interfaz de lectura sincrona hacia BRAM_IN (128 bits = 4 pixeles)
    output logic [P_BRAM_ADDRW-1:0]  bram_in_addr_o,
    input  logic [WORD_WIDTH-1:0]    bram_in_data_i,

    // Salida registrada hacia los 4 nucleos `sobel_core_rgb` en paralelo
    // Indice: win_o[lane][fila 0..2][col 0..2]
    output logic                     win_valid_o,
    output logic [LANES-1:0][2:0][2:0][PIXEL_WIDTH-1:0] win_o
);

    localparam int P_LB_ADDRW = $clog2(P_MAX_WORDS_ROW);

    // ------------------------------------------------------------------------
    // Etapa A (Ciclo T): Generacion de direcciones para BRAM_IN y LineBuffers
    // ------------------------------------------------------------------------
    logic                    feed_active_q;
    logic [DIM_WIDTH-1:0]    in_y_q;      // 0 .. img_height_i (H = fila flush)
    logic [WPR_WIDTH-1:0]    col_q;       // 0 .. words_per_row_i (WPR = flush)
    logic [P_BRAM_ADDRW-1:0] rd_addr_q;   // Direccion lineal en BRAM_IN

    // Registros de pipeline Etapa A -> Etapa B (alineados con latencia BRAM=1)
    logic                    s1_valid_q;
    logic [DIM_WIDTH-1:0]    s1_in_y_q;
    logic [WPR_WIDTH-1:0]    s1_col_q;
    logic                    s1_is_flush_row_q;
    logic                    s1_is_flush_col_q;
    logic                    s1_is_last_real_col_q;

    logic                    is_flush_row_a;
    logic                    is_flush_col_a;
    logic [P_LB_ADDRW-1:0]   lb_rd_addr;

    assign is_flush_row_a = (in_y_q == img_height_i);
    assign is_flush_col_a = (col_q  == words_per_row_i);

    assign bram_in_addr_o = rd_addr_q;
    assign lb_rd_addr     = col_q[P_LB_ADDRW-1:0];

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            feed_active_q         <= 1'b0;
            in_y_q                <= '0;
            col_q                 <= '0;
            rd_addr_q             <= '0;
            s1_valid_q            <= 1'b0;
            s1_in_y_q             <= '0;
            s1_col_q              <= '0;
            s1_is_flush_row_q     <= 1'b0;
            s1_is_flush_col_q     <= 1'b0;
            s1_is_last_real_col_q <= 1'b0;
        end else if (clear_i) begin
            // Al recibir ISA_START/RESET, prepara el barrido desde (0, 0)
            feed_active_q         <= 1'b1;
            in_y_q                <= '0;
            col_q                 <= '0;
            rd_addr_q             <= '0;
            s1_valid_q            <= 1'b0;
            s1_in_y_q             <= '0;
            s1_col_q              <= '0;
            s1_is_flush_row_q     <= 1'b0;
            s1_is_flush_col_q     <= 1'b0;
            s1_is_last_real_col_q <= 1'b0;
        end else if (enable_i && feed_active_q) begin
            // Captura metadatos de la lectura lanzada en este ciclo
            s1_valid_q            <= 1'b1;
            s1_in_y_q             <= in_y_q;
            s1_col_q              <= col_q;
            s1_is_flush_row_q     <= is_flush_row_a;
            s1_is_flush_col_q     <= is_flush_col_a;
            s1_is_last_real_col_q <= ({2'b0, col_q} + 16'd1 == {2'b0, words_per_row_i});

            // Avanza direccion lineal de BRAM_IN solo en lecturas reales
            if (!is_flush_row_a && !is_flush_col_a) begin
                rd_addr_q <= rd_addr_q + {{(P_BRAM_ADDRW-1){1'b0}}, 1'b1};
            end

            // Avanza punteros de columna y fila (incluyendo columna y fila flush)
            if (is_flush_col_a) begin
                col_q <= '0;
                if (is_flush_row_a) begin
                    feed_active_q <= 1'b0;
                end else begin
                    in_y_q <= in_y_q + 16'd1;
                end
            end else begin
                col_q <= col_q + {{(WPR_WIDTH-1){1'b0}}, 1'b1};
            end
        end else begin
            s1_valid_q <= 1'b0;
        end
    end

    // ------------------------------------------------------------------------
    // Dos buffers de linea en BRAM M10K (960 x 128 bits c/u)
    //   - line_buf_0 almacena la fila (y)
    //   - line_buf_1 almacena la fila (y - 1)
    // ------------------------------------------------------------------------
    logic                  lb0_we;
    logic                  lb1_we;
    logic [P_LB_ADDRW-1:0] lb_wr_addr;
    logic [WORD_WIDTH-1:0] in_word_masked;
    logic [WORD_WIDTH-1:0] lb0_rd_data;
    logic [WORD_WIDTH-1:0] lb1_rd_data;

    // Enmascara a cero los pixeles de relleno (x >= img_width_i) en la ultima
    // palabra de cada fila para garantizar zero-padding exacto en borde derecho
    always_comb begin
        if (s1_is_flush_row_q || s1_is_flush_col_q) begin
            in_word_masked = '0;
        end else if (s1_is_last_real_col_q) begin
            case (img_width_i[1:0])
                2'd1:    in_word_masked = {96'd0, bram_in_data_i[31:0]};
                2'd2:    in_word_masked = {64'd0, bram_in_data_i[63:0]};
                2'd3:    in_word_masked = {32'd0, bram_in_data_i[95:0]};
                default: in_word_masked = bram_in_data_i;  // Multiplo exacto de 4
            endcase
        end else begin
            in_word_masked = bram_in_data_i;
        end
    end

    assign lb0_we     = enable_i && s1_valid_q && !s1_is_flush_row_q && !s1_is_flush_col_q;
    assign lb1_we     = lb0_we && (s1_in_y_q >= 16'd1);
    assign lb_wr_addr = s1_col_q[P_LB_ADDRW-1:0];

    bram_simple_dp #(
        .DATA_WIDTH (WORD_WIDTH),
        .DEPTH      (P_MAX_WORDS_ROW),
        .ADDR_WIDTH (P_LB_ADDRW)
    ) u_line_buf_0 (
        .clk      (clk),
        .we_a_i   (lb0_we),
        .addr_a_i (lb_wr_addr),
        .din_a_i  (in_word_masked),
        .addr_b_i (lb_rd_addr),
        .dout_b_o (lb0_rd_data)
    );

    bram_simple_dp #(
        .DATA_WIDTH (WORD_WIDTH),
        .DEPTH      (P_MAX_WORDS_ROW),
        .ADDR_WIDTH (P_LB_ADDRW)
    ) u_line_buf_1 (
        .clk      (clk),
        .we_a_i   (lb1_we),
        .addr_a_i (lb_wr_addr),
        .din_a_i  (lb0_rd_data),
        .addr_b_i (lb_rd_addr),
        .dout_b_o (lb1_rd_data)
    );

    // ------------------------------------------------------------------------
    // Etapa B (Ciclo T+1): Seleccion de los 3 pisos + Ventana deslizante 3x6
    // ------------------------------------------------------------------------
    logic [2:0][WORD_WIDTH-1:0]  next_w;
    logic [2:0][PIXEL_WIDTH-1:0] prev_px3_q;  // Vecino izquierdo p(-1)
    logic [2:0][WORD_WIDTH-1:0]  cur_word_q;  // 4 pixeles centrales p(0..3)

    // Seleccion combinacional de la palabra entrante en cada piso (con
    // zero-padding en borde superior out_y==0, borde inferior out_y==H-1
    // y columna de flush derecha)
    always_comb begin
        if (s1_is_flush_col_q) begin
            next_w[0] = '0;
            next_w[1] = '0;
            next_w[2] = '0;
        end else begin
            next_w[0] = (s1_in_y_q == 16'd1) ? '0 : lb1_rd_data;
            next_w[1] = lb0_rd_data;
            next_w[2] = s1_is_flush_row_q    ? '0 : in_word_masked;
        end
    end

    // Matriz temporal de 3 filas x 6 columnas para cablear los 4 lanes
    logic [2:0][5:0][PIXEL_WIDTH-1:0] row_px;

    always_comb begin
        for (int r = 0; r < 3; r++) begin
            row_px[r][0] = prev_px3_q[r];
            row_px[r][1] = cur_word_q[r][31:0];
            row_px[r][2] = cur_word_q[r][63:32];
            row_px[r][3] = cur_word_q[r][95:64];
            row_px[r][4] = cur_word_q[r][127:96];
            row_px[r][5] = next_w[r][31:0];
        end
    end

    // Registro de desplazamiento de la ventana y registro de salida hacia
    // los 4 nucleos `sobel_core_rgb`
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            prev_px3_q  <= '0;
            cur_word_q  <= '0;
            win_valid_o <= 1'b0;
            win_o       <= '0;
        end else if (clear_i) begin
            prev_px3_q  <= '0;
            cur_word_q  <= '0;
            win_valid_o <= 1'b0;
            win_o       <= '0;
        end else if (enable_i && s1_valid_q && (s1_in_y_q >= 16'd1)) begin
            if (s1_col_q == '0) begin
                // Primera palabra de la fila `out_y`: precarga ventana
                for (int r = 0; r < 3; r++) begin
                    prev_px3_q[r] <= '0;
                    cur_word_q[r] <= next_w[r];
                end
                win_valid_o <= 1'b0;
            end else begin
                // Emite 4 ventanas 3x3 en paralelo para out_w = s1_col_q - 1
                for (int l = 0; l < LANES; l++) begin
                    for (int r = 0; r < 3; r++) begin
                        win_o[l][r][0] <= row_px[r][l + 0];
                        win_o[l][r][1] <= row_px[r][l + 1];
                        win_o[l][r][2] <= row_px[r][l + 2];
                    end
                end
                win_valid_o <= 1'b1;

                // Desplaza ventana hacia el siguiente grupo de 4 pixeles
                for (int r = 0; r < 3; r++) begin
                    prev_px3_q[r] <= cur_word_q[r][127:96];
                    cur_word_q[r] <= next_w[r];
                end
            end
        end else begin
            win_valid_o <= 1'b0;
        end
    end

endmodule : line_buffer_4lane
