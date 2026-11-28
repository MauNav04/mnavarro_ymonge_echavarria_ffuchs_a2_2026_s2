// ============================================================================
// Proyecto Grupal 2 - Framework Experimental DLP (Sobel RGBA32)
// Archivo: vjtag_wrapper.sv
// Descripcion:
//   Envoltorio sintetizable para el IP Core Intel Virtual JTAG
//   (`sld_virtual_jtag`) configurado con ancho de Instruction Register de
//   4 bits (`sld_ir_width = 4`), compatible con Quartus Prime 20.1.1 y la
//   placa Terasic DE1-SoC (Cyclone V).
//
// Referencia:
//   - Virtual JTAG Intel FPGA IP Core User Guide
//   - GuiaJtag-master (Abner Arroyo, curso CE4302)
// ============================================================================

module vjtag_wrapper (
    input  logic       tdo_i,
    input  logic [3:0] ir_out_i,

    output logic       tck_o,
    output logic       tdi_o,
    output logic [3:0] ir_in_o,
    output logic       v_cdr_o,
    output logic       v_sdr_o,
    output logic       v_e1dr_o,
    output logic       v_pdr_o,
    output logic       v_e2dr_o,
    output logic       v_udr_o,
    output logic       v_cir_o,
    output logic       v_uir_o
);

    sld_virtual_jtag #(
        .sld_auto_instance_index ("YES"),
        .sld_instance_index      (0),
        .sld_ir_width            (4),
        .sld_sim_action          (""),
        .sld_sim_n_scan          (0),
        .sld_sim_total_length    (0)
    ) u_sld_vjtag (
        .tdo                (tdo_i),
        .ir_out             (ir_out_i),
        .tck                (tck_o),
        .tdi                (tdi_o),
        .ir_in              (ir_in_o),
        .virtual_state_cdr  (v_cdr_o),
        .virtual_state_sdr  (v_sdr_o),
        .virtual_state_e1dr (v_e1dr_o),
        .virtual_state_pdr  (v_pdr_o),
        .virtual_state_e2dr (v_e2dr_o),
        .virtual_state_udr  (v_udr_o),
        .virtual_state_cir  (v_cir_o),
        .virtual_state_uir  (v_uir_o)
    );

endmodule : vjtag_wrapper
