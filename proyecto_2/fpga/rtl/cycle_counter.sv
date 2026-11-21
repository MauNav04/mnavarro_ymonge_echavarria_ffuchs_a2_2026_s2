// ============================================================================
// Proyecto Grupal 2 - Framework Experimental DLP (Sobel RGBA32)
// Archivo: cycle_counter.sv
// Descripcion:
//   Contador de ciclos de reloj para perfilado interno de la FPGA.
//   Solo cuenta mientras la FSM mantiene activa la senal `enable_i` (estado RUN),
//   permitiendo aislar el tiempo de computo del tiempo de transferencia JTAG.
// ============================================================================

module cycle_counter #(
    parameter int WIDTH = 32
) (
    input  logic             clk,
    input  logic             rst_n,
    input  logic             clear_i,    // Limpia el contador (al recibir START/RESET)
    input  logic             enable_i,   // Habilita incremento (solo durante RUN)
    output logic [WIDTH-1:0] count_o
);

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            count_o <= '0;
        end else if (clear_i) begin
            count_o <= '0;
        end else if (enable_i) begin
            count_o <= count_o + {{(WIDTH-1){1'b0}}, 1'b1};
        end
    end

endmodule : cycle_counter
