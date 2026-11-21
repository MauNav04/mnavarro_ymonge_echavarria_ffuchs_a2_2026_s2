// ============================================================================
// Proyecto Grupal 2 - Framework Experimental DLP (Sobel RGBA32)
// Archivo: line_buffer_window.sv
// Descripcion:
//   Dos buffers de linea (inferibles en BRAM M10K de Cyclone V) + matriz de
//   registros de desplazamiento 3x3 para formar una ventana deslizante de
//   pixeles RGBA32 a razon de 1 pixel por ciclo (paralelismo de flujo).
//
// Estado en Avance 2 (Dummy):
//   - Implementa la estructura real de 2 buffers de linea y registros 3x3.
//   - Produce `window_valid_o` una vez llenadas las 2 primeras filas + 2 cols.
//   - La logica completa de zero-padding en los bordes del frame se integrara
//     en la fase final junto con el controlador de memoria SDRAM.
// ============================================================================

module line_buffer_window
    import sobel_isa_pkg::*;
#(
    parameter int MAX_WIDTH = 512   // Escalable hasta 3840 en BRAM de Cyclone V
) (
    input  logic                   clk,
    input  logic                   rst_n,
    input  logic                   clear_i,
    input  logic                   enable_i,

    // Configuracion del ancho activo de la imagen (<= MAX_WIDTH)
    input  logic [DIM_WIDTH-1:0]   img_width_i,

    // Flujo de pixeles RGBA32 de entrada (1 pixel por ciclo cuando valid=1)
    input  logic                   pixel_valid_i,
    input  logic [PIXEL_WIDTH-1:0] pixel_in_i,

    // Ventana deslizante 3x3 (w00=arriba-izq ... w22=abajo-der)
    output logic                   window_valid_o,
    output logic [PIXEL_WIDTH-1:0] w00_o, w01_o, w02_o,
    output logic [PIXEL_WIDTH-1:0] w10_o, w11_o, w12_o,
    output logic [PIXEL_WIDTH-1:0] w20_o, w21_o, w22_o
);

    localparam int ADDR_W = $clog2(MAX_WIDTH);

    // Dos buffers de linea en memoria interna (Quartus infiere BRAM M10K)
    logic [PIXEL_WIDTH-1:0] line_buf_0 [0:MAX_WIDTH-1];
    logic [PIXEL_WIDTH-1:0] line_buf_1 [0:MAX_WIDTH-1];

    // Registros de la ventana 3x3
    logic [PIXEL_WIDTH-1:0] win [0:2][0:2];

    // Contadores de posicion dentro del stream
    logic [ADDR_W-1:0]      col_ptr_q;
    logic [DIM_WIDTH-1:0]   row_cnt_q;

    // Salidas leidas de los buffers de linea en la columna actual
    logic [PIXEL_WIDTH-1:0] lb0_rd_data;
    logic [PIXEL_WIDTH-1:0] lb1_rd_data;

    assign lb0_rd_data = line_buf_0[col_ptr_q];
    assign lb1_rd_data = line_buf_1[col_ptr_q];

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            col_ptr_q      <= '0;
            row_cnt_q      <= '0;
            window_valid_o <= 1'b0;
            for (int r = 0; r < 3; r++) begin
                for (int c = 0; c < 3; c++) begin
                    win[r][c] <= '0;
                end
            end
        end else if (clear_i) begin
            col_ptr_q      <= '0;
            row_cnt_q      <= '0;
            window_valid_o <= 1'b0;
            for (int r = 0; r < 3; r++) begin
                for (int c = 0; c < 3; c++) begin
                    win[r][c] <= '0;
                end
            end
        end else if (enable_i && pixel_valid_i) begin
            // Actualizacion en cascada de los 2 buffers de linea
            //   line_buf_1 guarda la fila (y - 2)
            //   line_buf_0 guarda la fila (y - 1)
            line_buf_1[col_ptr_q] <= lb0_rd_data;
            line_buf_0[col_ptr_q] <= pixel_in_i;

            // Desplazamiento horizontal de la ventana 3x3
            win[0][0] <= win[0][1];
            win[0][1] <= win[0][2];
            win[0][2] <= lb1_rd_data;

            win[1][0] <= win[1][1];
            win[1][1] <= win[1][2];
            win[1][2] <= lb0_rd_data;

            win[2][0] <= win[2][1];
            win[2][1] <= win[2][2];
            win[2][2] <= pixel_in_i;

            // Avance de columna y fila
            if ({ {(DIM_WIDTH-ADDR_W){1'b0}}, col_ptr_q } + 16'd1 >= img_width_i) begin
                col_ptr_q <= '0;
                row_cnt_q <= row_cnt_q + 16'd1;
            end else begin
                col_ptr_q <= col_ptr_q + {{(ADDR_W-1){1'b0}}, 1'b1};
            end

            // Ventana llena para posiciones interiores (Avance 2 dummy)
            window_valid_o <= (row_cnt_q >= 16'd2) &&
                              ({{(DIM_WIDTH-ADDR_W){1'b0}}, col_ptr_q} >= 16'd2);
        end else begin
            window_valid_o <= 1'b0;
        end
    end

    // Mapeo de salida de la ventana 3x3
    assign w00_o = win[0][0];
    assign w01_o = win[0][1];
    assign w02_o = win[0][2];
    assign w10_o = win[1][0];
    assign w11_o = win[1][1];
    assign w12_o = win[1][2];
    assign w20_o = win[2][0];
    assign w21_o = win[2][1];
    assign w22_o = win[2][2];

endmodule : line_buffer_window
