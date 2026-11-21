// ============================================================================
// Proyecto Grupal 2 - Framework Experimental DLP (Sobel RGBA32)
// Archivo: tb_sobel_dummy.sv
// Descripcion:
//   Testbench de verificacion para Avance 2 (compatible con ModelSim-Altera).
//   Prueba:
//     1. Datapath `sobel_core_rgb` bit a bit contra casos conocidos del
//        baseline secuencial (constante, impulso ortogonal/diagonal, borde).
//     2. Flujo completo de `sobel_accelerator_top` con el ISA minimo:
//        ISA_CONFIG -> ISA_START -> stream dummy -> ST_DONE -> lectura ciclos.
// ============================================================================

`timescale 1ns/1ps

module tb_sobel_dummy;
    import sobel_isa_pkg::*;

    logic clk;
    logic rst_n;

    // Reloj de 50 MHz (periodo = 20 ns), identico a CLOCK_50 de DE1-SoC
    initial clk = 1'b0;
    always #10 clk = ~clk;

    // ========================================================================
    // 1. Instancia directa del datapath `sobel_core_rgb` (validacion exacta)
    // ========================================================================
    logic                   core_valid_i;
    logic [PIXEL_WIDTH-1:0] w00, w01, w02;
    logic [PIXEL_WIDTH-1:0] w10, w11, w12;
    logic [PIXEL_WIDTH-1:0] w20, w21, w22;
    logic                   core_valid_o;
    logic [PIXEL_WIDTH-1:0] core_out;

    sobel_core_rgb u_dut_core (
        .clk         (clk),
        .rst_n       (rst_n),
        .clear_i     (1'b0),
        .valid_i     (core_valid_i),
        .w00_i (w00), .w01_i (w01), .w02_i (w02),
        .w10_i (w10), .w11_i (w11), .w12_i (w12),
        .w20_i (w20), .w21_i (w21), .w22_i (w22),
        .valid_o     (core_valid_o),
        .pixel_out_o (core_out)
    );

    // ========================================================================
    // 2. Instancia completa del acelerador `sobel_accelerator_top` (flujo ISA)
    // ========================================================================
    logic                     cmd_valid;
    isa_opcode_t              cmd_opcode;
    logic [DIM_WIDTH-1:0]     cfg_width, cfg_height;
    logic [ADDR_WIDTH-1:0]    cfg_src_addr, cfg_dst_addr;
    logic                     pix_in_valid;
    logic [PIXEL_WIDTH-1:0]   pix_in;
    logic                     pix_out_valid;
    logic [PIXEL_WIDTH-1:0]   pix_out;
    accel_state_t             state;
    logic [31:0]              status_reg;
    logic [COUNTER_WIDTH-1:0] cycle_count;
    logic [31:0]              processed_pixels;

    sobel_accelerator_top #(
        .MAX_WIDTH (16)
    ) u_dut_top (
        .clk                (clk),
        .rst_n              (rst_n),
        .cmd_valid_i        (cmd_valid),
        .cmd_opcode_i       (cmd_opcode),
        .cfg_width_i        (cfg_width),
        .cfg_height_i       (cfg_height),
        .cfg_src_addr_i     (cfg_src_addr),
        .cfg_dst_addr_i     (cfg_dst_addr),
        .pixel_in_valid_i   (pix_in_valid),
        .pixel_in_i         (pix_in),
        .pixel_out_valid_o  (pix_out_valid),
        .pixel_out_o        (pix_out),
        .state_o            (state),
        .status_reg_o       (status_reg),
        .cycle_count_o      (cycle_count),
        .processed_pixels_o (processed_pixels)
    );

    int errors = 0;

    task automatic check_pixel(
        input logic [31:0] actual,
        input logic [7:0]  exp_r,
        input logic [7:0]  exp_g,
        input logic [7:0]  exp_b,
        input string       label
    );
        logic [31:0] expected;
        begin
            expected = pack_rgba(exp_r, exp_g, exp_b, 8'hFF);
            if (actual !== expected) begin
                $display("[FAIL] %s: esperado=%h, obtenido=%h", label, expected, actual);
                errors++;
            end else begin
                $display("[PASS] %s -> RGBA=%h", label, actual);
            end
        end
    endtask

    initial begin
        // Inicializacion
        rst_n        = 1'b0;
        core_valid_i = 1'b0;
        w00 = '0; w01 = '0; w02 = '0;
        w10 = '0; w11 = '0; w12 = '0;
        w20 = '0; w21 = '0; w22 = '0;
        cmd_valid    = 1'b0;
        cmd_opcode   = ISA_NOP;
        cfg_width    = 16'd0;
        cfg_height   = 16'd0;
        cfg_src_addr = 32'd0;
        cfg_dst_addr = 32'd0;
        pix_in_valid = 1'b0;
        pix_in       = 32'd0;

        repeat (3) @(posedge clk);
        rst_n = 1'b1;
        @(posedge clk);

        $display("========================================================");
        $display(" Test 1: Validacion bit a bit de sobel_core_rgb");
        $display("========================================================");

        // Caso 1A: Ventana constante -> gradiente 0, Alpha = 255
        w00 = pack_rgba(100, 150, 200, 128); w01 = w00; w02 = w00;
        w10 = w00; w11 = w00; w12 = w00;
        w20 = w00; w21 = w00; w22 = w00;
        core_valid_i = 1'b1;
        @(posedge clk);
        core_valid_i = 1'b0;
        repeat (2) @(posedge clk);
        #1;
        check_pixel(core_out, 8'd0, 8'd0, 8'd0, "Ventana constante");

        // Caso 1B: Impulso ortogonal en w21=(10,20,30) -> |Gx|+|Gy| = 2*V = (20,40,60)
        w00 = '0; w01 = '0; w02 = '0;
        w10 = '0; w11 = '0; w12 = '0;
        w20 = '0; w21 = pack_rgba(10, 20, 30, 255); w22 = '0;
        core_valid_i = 1'b1;
        @(posedge clk);
        core_valid_i = 1'b0;
        repeat (2) @(posedge clk);
        #1;
        check_pixel(core_out, 8'd20, 8'd40, 8'd60, "Impulso ortogonal");

        // Caso 1C: Impulso diagonal en w22=(10,20,30) -> |Gx|+|Gy| = 2*V = (20,40,60)
        w21 = '0;
        w22 = pack_rgba(10, 20, 30, 255);
        core_valid_i = 1'b1;
        @(posedge clk);
        core_valid_i = 1'b0;
        repeat (2) @(posedge clk);
        #1;
        check_pixel(core_out, 8'd20, 8'd40, 8'd60, "Impulso diagonal");

        // Caso 1D: Borde vertical puro (col izq=0, col der=255) -> satura a 255
        w00 = '0; w01 = '0; w02 = pack_rgba(255, 255, 255, 255);
        w10 = '0; w11 = '0; w12 = pack_rgba(255, 255, 255, 255);
        w20 = '0; w21 = '0; w22 = pack_rgba(255, 255, 255, 255);
        core_valid_i = 1'b1;
        @(posedge clk);
        core_valid_i = 1'b0;
        repeat (2) @(posedge clk);
        #1;
        check_pixel(core_out, 8'd255, 8'd255, 8'd255, "Borde vertical saturado");

        $display("========================================================");
        $display(" Test 2: Flujo ISA minimo en sobel_accelerator_top (4x4)");
        $display("========================================================");

        // 1) Enviar ISA_CONFIG (4x4 -> 16 pixeles objetivo)
        cmd_valid    = 1'b1;
        cmd_opcode   = ISA_CONFIG;
        cfg_width    = 16'd4;
        cfg_height   = 16'd4;
        cfg_src_addr = 32'h0000_0000;
        cfg_dst_addr = 32'h0010_0000;
        @(posedge clk);
        cmd_valid    = 1'b0;
        @(posedge clk);
        #1;
        if (state !== ST_CONFIGURED) begin
            $display("[FAIL] Estado esperado ST_CONFIGURED, obtenido=%0d", state);
            errors++;
        end else begin
            $display("[PASS] ISA_CONFIG -> ST_CONFIGURED");
        end

        // 2) Enviar ISA_START
        cmd_valid  = 1'b1;
        cmd_opcode = ISA_START;
        @(posedge clk);
        cmd_valid  = 1'b0;

        // 3) Alimentar pixeles dummy hasta completar ST_DONE
        pix_in_valid = 1'b1;
        for (int idx = 0; idx < 80; idx++) begin
            if ((idx % 4) < 2) begin
                pix_in = pack_rgba(8'd0, 8'd0, 8'd0, 8'd255);
            end else begin
                pix_in = pack_rgba(8'd255, 8'd255, 8'd255, 8'd255);
            end
            @(posedge clk);
            if (state == ST_DONE) begin
                break;
            end
        end
        pix_in_valid = 1'b0;
        @(posedge clk);
        #1;

        if (state !== ST_DONE || cycle_count == 0) begin
            $display("[FAIL] Top no alcanzo ST_DONE o cycle_count=0 (state=%0d, cycles=%0d)",
                     state, cycle_count);
            errors++;
        end else begin
            $display("[PASS] Flujo ISA completo -> ST_DONE | pixeles=%0d | ciclos=%0d",
                     processed_pixels, cycle_count);
        end

        if (errors == 0) begin
            $display("========================================================");
            $display(" [OK] TODAS LAS PRUEBAS DUMMY DE FPGA PASARON (0 errores)");
            $display("========================================================");
        end else begin
            $display(" [ERROR] Fallaron %0d pruebas.", errors);
        end

        $finish;
    end

endmodule : tb_sobel_dummy
