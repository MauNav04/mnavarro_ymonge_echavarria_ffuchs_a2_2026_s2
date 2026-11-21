// ============================================================================
// Proyecto Grupal 2 - Framework Experimental DLP (Sobel RGBA32)
// Archivo: sobel_core_rgb.sv
// Descripcion:
//   Datapath segmentado (2 etapas de pipeline) para calcular el operador Sobel
//   3x3 entero en paralelo sobre los canales R, G y B de una ventana RGBA32.
//
// Especificacion matematica (igualdad exacta bit a bit con sobel_sequential):
//   Kx = [ -1  0  1 ; -2  0  2 ; -1  0  1 ]
//   Ky = [ -1 -2 -1 ;  0  0  0 ;  1  2  1 ]
//   |Gd,c| <= 1020  (caben en enteros de 12 bits con signo, sin multiplicadores)
//   O(c) = min(255, |Gx,c| + |Gy,c|),  O(A) = 255
// ============================================================================

module sobel_core_rgb
    import sobel_isa_pkg::*;
(
    input  logic                   clk,
    input  logic                   rst_n,
    input  logic                   clear_i,
    input  logic                   valid_i,

    // Ventana 3x3 de pixeles RGBA32
    input  logic [PIXEL_WIDTH-1:0] w00_i, w01_i, w02_i,
    input  logic [PIXEL_WIDTH-1:0] w10_i, w11_i, w12_i,
    input  logic [PIXEL_WIDTH-1:0] w20_i, w21_i, w22_i,

    // Pixel RGBA32 resultante (latencia fija = 2 ciclos de reloj)
    output logic                   valid_o,
    output logic [PIXEL_WIDTH-1:0] pixel_out_o
);

    // ------------------------------------------------------------------------
    // Funciones auxiliares puramente combinacionales (sin multiplicadores DSP)
    // ------------------------------------------------------------------------
    function automatic logic signed [11:0] calc_gx(
        input logic [7:0] p00, input logic [7:0] p02,
        input logic [7:0] p10, input logic [7:0] p12,
        input logic [7:0] p20, input logic [7:0] p22
    );
        logic signed [11:0] pos_term;
        logic signed [11:0] neg_term;
        begin
            pos_term = $signed({4'b0, p02}) + ($signed({4'b0, p12}) <<< 1) + $signed({4'b0, p22});
            neg_term = $signed({4'b0, p00}) + ($signed({4'b0, p10}) <<< 1) + $signed({4'b0, p20});
            return pos_term - neg_term;
        end
    endfunction

    function automatic logic signed [11:0] calc_gy(
        input logic [7:0] p00, input logic [7:0] p01, input logic [7:0] p02,
        input logic [7:0] p20, input logic [7:0] p21, input logic [7:0] p22
    );
        logic signed [11:0] pos_term;
        logic signed [11:0] neg_term;
        begin
            pos_term = $signed({4'b0, p20}) + ($signed({4'b0, p21}) <<< 1) + $signed({4'b0, p22});
            neg_term = $signed({4'b0, p00}) + ($signed({4'b0, p01}) <<< 1) + $signed({4'b0, p02});
            return pos_term - neg_term;
        end
    endfunction

    function automatic logic [7:0] manhattan_sat8(
        input logic signed [11:0] gx,
        input logic signed [11:0] gy
    );
        logic [11:0] abs_gx;
        logic [11:0] abs_gy;
        logic [11:0] mag;
        begin
            abs_gx = gx[11] ? (-gx) : gx;
            abs_gy = gy[11] ? (-gy) : gy;
            mag    = abs_gx + abs_gy;
            return (mag > 12'd255) ? 8'd255 : mag[7:0];
        end
    endfunction

    // ------------------------------------------------------------------------
    // Etapa 1 del pipeline: gradientes Gx y Gy por canal (R, G, B en paralelo)
    // ------------------------------------------------------------------------
    logic               s1_valid_q;
    logic signed [11:0] gx_r_q, gy_r_q;
    logic signed [11:0] gx_g_q, gy_g_q;
    logic signed [11:0] gx_b_q, gy_b_q;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            s1_valid_q <= 1'b0;
            gx_r_q <= '0; gy_r_q <= '0;
            gx_g_q <= '0; gy_g_q <= '0;
            gx_b_q <= '0; gy_b_q <= '0;
        end else if (clear_i) begin
            s1_valid_q <= 1'b0;
            gx_r_q <= '0; gy_r_q <= '0;
            gx_g_q <= '0; gy_g_q <= '0;
            gx_b_q <= '0; gy_b_q <= '0;
        end else begin
            s1_valid_q <= valid_i;
            if (valid_i) begin
                gx_r_q <= calc_gx(rgba_r(w00_i), rgba_r(w02_i),
                                  rgba_r(w10_i), rgba_r(w12_i),
                                  rgba_r(w20_i), rgba_r(w22_i));
                gy_r_q <= calc_gy(rgba_r(w00_i), rgba_r(w01_i), rgba_r(w02_i),
                                  rgba_r(w20_i), rgba_r(w21_i), rgba_r(w22_i));

                gx_g_q <= calc_gx(rgba_g(w00_i), rgba_g(w02_i),
                                  rgba_g(w10_i), rgba_g(w12_i),
                                  rgba_g(w20_i), rgba_g(w22_i));
                gy_g_q <= calc_gy(rgba_g(w00_i), rgba_g(w01_i), rgba_g(w02_i),
                                  rgba_g(w20_i), rgba_g(w21_i), rgba_g(w22_i));

                gx_b_q <= calc_gx(rgba_b(w00_i), rgba_b(w02_i),
                                  rgba_b(w10_i), rgba_b(w12_i),
                                  rgba_b(w20_i), rgba_b(w22_i));
                gy_b_q <= calc_gy(rgba_b(w00_i), rgba_b(w01_i), rgba_b(w02_i),
                                  rgba_b(w20_i), rgba_b(w21_i), rgba_b(w22_i));
            end
        end
    end

    // ------------------------------------------------------------------------
    // Etapa 2 del pipeline: magnitud Manhattan, saturacion a 8 bits y A = 255
    // ------------------------------------------------------------------------
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            valid_o     <= 1'b0;
            pixel_out_o <= 32'd0;
        end else if (clear_i) begin
            valid_o     <= 1'b0;
            pixel_out_o <= 32'd0;
        end else begin
            valid_o <= s1_valid_q;
            if (s1_valid_q) begin
                pixel_out_o <= pack_rgba(
                    manhattan_sat8(gx_r_q, gy_r_q),
                    manhattan_sat8(gx_g_q, gy_g_q),
                    manhattan_sat8(gx_b_q, gy_b_q),
                    8'hFF
                );
            end
        end
    end

endmodule : sobel_core_rgb
