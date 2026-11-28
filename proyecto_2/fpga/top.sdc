# ============================================================================
# Proyecto Grupal 2 - Framework Experimental DLP (Sobel RGBA32)
# Archivo: top.sdc
# Descripcion:
#   Restricciones de tiempo (TimeQuest Timing Analyzer) para la Terasic
#   DE1-SoC (Cyclone V 5CSEMA5F31C6) con PLL 50 -> 100 MHz y Virtual JTAG.
# ============================================================================

# 1. Reloj base fisico de la placa DE1-SoC: 50 MHz (periodo 20 ns)
create_clock -name CLOCK_50 -period 20.000 [get_ports {CLOCK_50}]

# 2. Deriva automaticamente el reloj generado por `altera_pll` (100 MHz)
derive_pll_clocks
derive_clock_uncertainty

# 3. Reloj JTAG del bloque SLD Hub (USB-Blaster II hasta 24 MHz)
if {[get_collection_size [get_clocks -nowarn {altera_reserved_tck}]] == 0} {
    if {[get_collection_size [get_ports -nowarn {altera_reserved_tck}]] > 0} {
        create_clock -name {altera_reserved_tck} -period 41.667 [get_ports {altera_reserved_tck}]
    }
}

# 4. Cruce de dominios asincrono entre clk_sys (PLL 100 MHz) y JTAG (tck)
#    Manejado en hardware por sincronizador CDC 3-FF en `jtag_bridge.sv`.
set_clock_groups -asynchronous \
    -group [get_clocks {CLOCK_50 *u_altera_pll*}] \
    -group [get_clocks {altera_reserved_tck}]

# 5. Pines de E/S lentos de la placa (botones, switches, LEDs)
set_false_path -from [get_ports {KEY[*] SW[*]}]
set_false_path -to   [get_ports {LEDR[*]}]
