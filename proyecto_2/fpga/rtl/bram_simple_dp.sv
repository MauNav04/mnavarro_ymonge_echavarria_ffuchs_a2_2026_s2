// ============================================================================
// Proyecto Grupal 2 - Framework Experimental DLP (Sobel RGBA32)
// Archivo: bram_simple_dp.sv
// Descripcion:
//   Memoria RAM interna Simple Dual-Port (1 puerto de escritura + 1 puerto de
//   lectura sincronos en el mismo reloj) parametrizable para inferencia limpia
//   de bloques M10K en Intel Cyclone V (Quartus Prime 20.1).
//
// Reglas de inferencia M10K aplicadas:
//   - Sin reset asincrono/sincrono sobre el arreglo `mem`.
//   - Escritura y lectura estrictamente sincronas en `posedge clk`.
//   - Atributo `ramstyle = "M10K, no_rw_check"` (las direcciones de lectura y
//     escritura no colisionan en el mismo ciclo por construccion del datapath).
// ============================================================================

module bram_simple_dp #(
    parameter int DATA_WIDTH = 128,
    parameter int DEPTH      = 8192,
    parameter int ADDR_WIDTH = $clog2(DEPTH)
) (
    input  logic                  clk,

    // Puerto A: Escritura sincrona
    input  logic                  we_a_i,
    input  logic [ADDR_WIDTH-1:0] addr_a_i,
    input  logic [DATA_WIDTH-1:0] din_a_i,

    // Puerto B: Lectura sincrona (latencia = 1 ciclo de reloj)
    input  logic [ADDR_WIDTH-1:0] addr_b_i,
    output logic [DATA_WIDTH-1:0] dout_b_o
);

    (* ramstyle = "M10K, no_rw_check" *)
    logic [DATA_WIDTH-1:0] mem [0:DEPTH-1];

    always_ff @(posedge clk) begin
        if (we_a_i) begin
            mem[addr_a_i] <= din_a_i;
        end
        dout_b_o <= mem[addr_b_i];
    end

endmodule : bram_simple_dp
