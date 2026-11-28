// ============================================================================
// Proyecto Grupal 2 - Framework Experimental DLP (Sobel RGBA32)
// Archivo: pll_100mhz.sv
// Descripcion:
//   Envoltorio sintetizable de la megafuncion nativa `altera_pll` de Intel
//   Cyclone V (DE1-SoC `5CSEMA5F31C6`) para multiplicar el reloj de entrada
//   `CLOCK_50` (50 MHz) a `clk_100_o` (100 MHz).
//
//   A 100 MHz con 4 lanes Sobel en paralelo, el throughput teorico del
//   acelerador alcanza 400 MPix/s (~3.3x sobre el baseline secuencial CPU).
// ============================================================================

module pll_100mhz (
    input  logic refclk_i,   // Reloj de referencia 50 MHz (CLOCK_50)
    input  logic rst_i,      // Reset activo en alto del PLL
    output logic clk_100_o,  // Reloj sintetizado de 100 MHz (clk_sys)
    output logic locked_o    // 1 cuando el PLL esta enganchado y estable
);

    logic [0:0] outclk_bus;

    altera_pll #(
        .fractional_vco_multiplier ("false"),
        .reference_clock_frequency ("50.0 MHz"),
        .operation_mode            ("direct"),
        .number_of_clocks          (1),
        .output_clock_frequency0   ("100.000000 MHz"),
        .phase_shift0              ("0 ps"),
        .duty_cycle0               (50),
        .pll_type                  ("General"),
        .pll_subtype               ("General")
    ) u_altera_pll (
        .refclk   (refclk_i),
        .rst      (rst_i),
        .outclk   (outclk_bus),
        .locked   (locked_o)
    );

    assign clk_100_o = outclk_bus[0];

endmodule : pll_100mhz
