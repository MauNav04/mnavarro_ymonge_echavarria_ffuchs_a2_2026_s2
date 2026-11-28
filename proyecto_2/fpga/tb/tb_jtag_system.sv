// ============================================================================
// Proyecto Grupal 2 - Framework Experimental DLP (Sobel RGBA32)
// Archivo: tb_jtag_system.sv
// Descripcion:
//   Testbench de integracion de Fase 2 (ModelSim-Altera).
//   Valida de extremo a extremo `jtag_bridge` + `sobel_accelerator_top` con
//   dos relojes estrictamente asincronos:
//     - `clk_sys` : 100.0 MHz (periodo 10.0 ns) -> dominio del acelerador
//     - `tck`     : ~13.7 MHz (periodo 73.0 ns) -> dominio Virtual JTAG
//
//   Emula las transacciones seriales LSB-first exactas de `quartus_stp`
//   (`device_virtual_ir_shift` y `device_virtual_dr_shift`) y verifica:
//     1. Cruce CDC 3-FF sin perdida ni duplicacion de comandos.
//     2. Cadena DR variable por opcode (1b, 32b, 64b, 128b).
//     3. Coherencia de lectura `ISA_STATUS` (DONE + cycle_count congelado).
//     4. Igualdad exacta bit a bit (`max_diff == 0`) frente a `sobel_sequential`
//        en multiples dimensiones (1x1, 4x4, 7x5, 13x9, 32x16) y ejecucion
//        consecutiva de multiples tiles sin reset fisico.
// ============================================================================

`timescale 1ns/1ps

module tb_jtag_system;
    import sobel_isa_pkg::*;

    // Relojes asincronos (relacion no entera para estresar el CDC 3-FF)
    logic clk_sys;
    logic tck;
    logic rst_n;

    initial clk_sys = 1'b0;
    always #5.0 clk_sys = ~clk_sys;    // 100 MHz

    initial tck = 1'b0;
    always #36.5 tck = ~tck;           // ~13.7 MHz

    // Senales Virtual JTAG (emulando `sld_virtual_jtag`)
    logic       tdi;
    logic       tdo;
    logic [3:0] ir_in;
    logic       v_cdr;
    logic       v_sdr;
    logic       v_udr;
    logic       v_uir;

    // Interconexion `jtag_bridge` <-> `sobel_accelerator_top`
    logic                     cmd_valid;
    isa_opcode_t              cmd_opcode;
    logic [DIM_WIDTH-1:0]     cfg_width;
    logic [DIM_WIDTH-1:0]     cfg_height;
    logic                     jtag_wr_en;
    logic [WORD_WIDTH-1:0]    jtag_wr_data;
    logic [WORD_WIDTH-1:0]    jtag_rd_data;
    logic                     jtag_activity;

    accel_state_t             state;
    logic [31:0]              status_reg;
    logic [COUNTER_WIDTH-1:0] cycle_count;
    logic [31:0]              processed_words;

    jtag_bridge u_bridge (
        .clk_sys         (clk_sys),
        .rst_n           (rst_n),
        .tck             (tck),
        .tdi             (tdi),
        .ir_in           (ir_in),
        .v_cdr           (v_cdr),
        .v_sdr           (v_sdr),
        .v_udr           (v_udr),
        .v_uir           (v_uir),
        .tdo             (tdo),
        .status_reg_i    (status_reg),
        .cycle_count_i   (cycle_count),
        .rd_data_i       (jtag_rd_data),
        .cmd_valid_o     (cmd_valid),
        .cmd_opcode_o    (cmd_opcode),
        .cfg_width_o     (cfg_width),
        .cfg_height_o    (cfg_height),
        .jtag_wr_en_o    (jtag_wr_en),
        .wr_data_o       (jtag_wr_data),
        .jtag_activity_o (jtag_activity)
    );

    sobel_accelerator_top #(
        .P_MAX_WIDTH  (MAX_WIDTH),
        .P_BRAM_DEPTH (BRAM_DEPTH)
    ) u_accel (
        .clk               (clk_sys),
        .rst_n             (rst_n),
        .cmd_valid_i       (cmd_valid),
        .cmd_opcode_i      (cmd_opcode),
        .cfg_width_i       (cfg_width),
        .cfg_height_i      (cfg_height),
        .jtag_wr_data_i    (jtag_wr_data),
        .jtag_rd_data_o    (jtag_rd_data),
        .state_o           (state),
        .status_reg_o      (status_reg),
        .cycle_count_o     (cycle_count),
        .processed_words_o (processed_words)
    );

    // Buffers de referencia dorada
    localparam int MAX_PIXELS = 4096;
    logic [31:0] img_in  [0:MAX_PIXELS-1];
    logic [31:0] img_ref [0:MAX_PIXELS-1];

    int total_errors = 0;
    int tests_passed = 0;

    // ------------------------------------------------------------------------
    // Emulacion del protocolo IEEE 1149.1 Virtual JTAG (identico a quartus_stp)
    // ------------------------------------------------------------------------
    task automatic jtag_ir_shift(input isa_opcode_t op);
        begin
            @(negedge tck);
            ir_in = op;
            v_uir = 1'b1;
            @(posedge tck);
            @(negedge tck);
            v_uir = 1'b0;
        end
    endtask

    task automatic jtag_dr_shift(
        input  int             length,
        input  logic [127:0]   tx_data,
        output logic [127:0]   rx_data
    );
        int b;
        begin
            rx_data = 128'd0;

            // 1) Capture-DR (`v_cdr`)
            @(negedge tck);
            v_cdr = 1'b1;
            v_sdr = 1'b0;
            v_udr = 1'b0;
            @(posedge tck);

            // 2) Shift-DR (`v_sdr`): `length` ciclos LSB-first
            for (b = 0; b < length; b++) begin
                @(negedge tck);
                v_cdr = 1'b0;
                v_sdr = 1'b1;
                tdi   = tx_data[b];
                // En IEEE 1149.1, TDO actual se muestrea antes del posedge tck
                #1;
                rx_data[b] = tdo;
                @(posedge tck);
            end

            // 3) Update-DR (`v_udr`): 1 ciclo de TCK
            @(negedge tck);
            v_sdr = 1'b0;
            v_udr = 1'b1;
            tdi   = 1'b0;
            @(posedge tck);

            // 4) Run-Test/Idle (deja margen realista como USB-Blaster entre comandos)
            @(negedge tck);
            v_udr = 1'b0;
            @(posedge tck);
        end
    endtask

    // Wrappers de alto nivel del ISA sobre JTAG (equivalentes a las procs Tcl)
    task automatic isa_jtag_reset();
        logic [127:0] dummy;
        begin
            jtag_ir_shift(ISA_RESET);
            jtag_dr_shift(1, 128'd0, dummy);
        end
    endtask

    task automatic isa_jtag_nop();
        logic [127:0] dummy;
        begin
            jtag_ir_shift(ISA_NOP);
            jtag_dr_shift(1, 128'd0, dummy);
        end
    endtask

    task automatic isa_jtag_config(input logic [15:0] w, input logic [15:0] h);
        logic [127:0] dummy;
        begin
            jtag_ir_shift(ISA_CONFIG);
            jtag_dr_shift(32, {96'd0, w, h}, dummy);
        end
    endtask

    task automatic isa_jtag_start();
        logic [127:0] dummy;
        begin
            jtag_ir_shift(ISA_START);
            jtag_dr_shift(1, 128'd0, dummy);
        end
    endtask

    task automatic isa_jtag_status(output logic [31:0] st, output logic [31:0] cyc);
        logic [127:0] rx;
        begin
            jtag_ir_shift(ISA_STATUS);
            jtag_dr_shift(64, 128'd0, rx);
            cyc = rx[31:0];
            st  = rx[63:32];
        end
    endtask

    // ------------------------------------------------------------------------
    // Modelo dorado Sobel (identico a `sobel_sequential.cpp`)
    // ------------------------------------------------------------------------
    function automatic int abs_int(input int v);
        return (v < 0) ? -v : v;
    endfunction

    function automatic logic [7:0] sat8(input int v);
        return (v > 255) ? 8'd255 : v[7:0];
    endfunction

    task automatic fill_deterministic(input int W, input int H, input int unsigned seed);
        int unsigned st;
        int x, y;
        begin
            st = (seed != 0) ? seed : 32'd1;
            for (y = 0; y < H; y++) begin
                for (x = 0; x < W; x++) begin
                    st = st ^ (st << 13);
                    st = st ^ (st >> 17);
                    st = st ^ (st << 5);
                    img_in[y * W + x] = pack_rgba(st[7:0], st[15:8], st[23:16], 8'hFF);
                end
            end
        end
    endtask

    task automatic compute_golden_sobel(input int W, input int H);
        int x, y, i, j, sx, sy;
        int wx, wy, sample;
        int gx [0:2];
        int gy [0:2];
        logic [31:0] px;
        int kx [0:2][0:2];
        int ky [0:2][0:2];
        begin
            kx[0][0] = -1; kx[0][1] = 0; kx[0][2] = 1;
            kx[1][0] = -2; kx[1][1] = 0; kx[1][2] = 2;
            kx[2][0] = -1; kx[2][1] = 0; kx[2][2] = 1;

            ky[0][0] = -1; ky[0][1] = -2; ky[0][2] = -1;
            ky[1][0] =  0; ky[1][1] =  0; ky[1][2] =  0;
            ky[2][0] =  1; ky[2][1] =  2; ky[2][2] =  1;

            for (y = 0; y < H; y++) begin
                for (x = 0; x < W; x++) begin
                    gx[0] = 0; gx[1] = 0; gx[2] = 0;
                    gy[0] = 0; gy[1] = 0; gy[2] = 0;

                    for (i = -1; i <= 1; i++) begin
                        sy = y + i;
                        if (sy < 0 || sy >= H) continue;
                        for (j = -1; j <= 1; j++) begin
                            sx = x + j;
                            if (sx < 0 || sx >= W) continue;
                            wx = kx[i + 1][j + 1];
                            wy = ky[i + 1][j + 1];
                            px = img_in[sy * W + sx];

                            sample = int'(px[7:0]);   gx[0] += sample * wx; gy[0] += sample * wy;
                            sample = int'(px[15:8]);  gx[1] += sample * wx; gy[1] += sample * wy;
                            sample = int'(px[23:16]); gx[2] += sample * wx; gy[2] += sample * wy;
                        end
                    end

                    img_ref[y * W + x] = pack_rgba(
                        sat8(abs_int(gx[0]) + abs_int(gy[0])),
                        sat8(abs_int(gx[1]) + abs_int(gy[1])),
                        sat8(abs_int(gx[2]) + abs_int(gy[2])),
                        8'hFF
                    );
                end
            end
        end
    endtask

    // ------------------------------------------------------------------------
    // Flujo completo por JTAG: CONFIG -> WRITE -> START -> STATUS -> READ -> NOP
    // ------------------------------------------------------------------------
    task automatic run_jtag_tile_test(
        input int          W,
        input int          H,
        input int unsigned seed,
        input string       label
    );
        int wpr, y, w_idx, lane, x;
        int polls, case_errors;
        logic [31:0]  st, cyc;
        logic [127:0] wr_word, rd_word, dummy;
        logic [31:0]  px_actual, px_expected;
        begin
            case_errors = 0;
            wpr = (W + 3) / 4;

            fill_deterministic(W, H, seed);
            compute_golden_sobel(W, H);

            // 1. ISA_CONFIG
            isa_jtag_config(W[15:0], H[15:0]);
            isa_jtag_status(st, cyc);
            if (!st[STATUS_BIT_CONFIGURED]) begin
                $display("[FAIL] %s: tras ISA_CONFIG esperado CONFIGURED, st=%h", label, st);
                total_errors++;
                return;
            end

            // 2. ISA_WRITE_DATA (manteniendo IR fijo en ISA_WRITE_DATA, igual que en Tcl)
            jtag_ir_shift(ISA_WRITE_DATA);
            for (y = 0; y < H; y++) begin
                for (w_idx = 0; w_idx < wpr; w_idx++) begin
                    wr_word = '0;
                    for (lane = 0; lane < LANES; lane++) begin
                        x = w_idx * 4 + lane;
                        if (x < W) begin
                            wr_word[lane*32 +: 32] = img_in[y * W + x];
                        end else begin
                            wr_word[lane*32 +: 32] = 32'hCAFE_BABE; // Basura en padding
                        end
                    end
                    jtag_dr_shift(128, wr_word, dummy);
                end
            end

            // 3. ISA_START
            isa_jtag_start();

            // 4. Polling via ISA_STATUS hasta DONE == 1
            polls = 0;
            st    = 32'd0;
            cyc   = 32'd0;
            jtag_ir_shift(ISA_STATUS);
            while (!st[STATUS_BIT_DONE] && polls < 100) begin
                jtag_dr_shift(64, 128'd0, rd_word);
                cyc = rd_word[31:0];
                st  = rd_word[63:32];
                polls++;
            end

            if (!st[STATUS_BIT_DONE] || cyc == 32'd0) begin
                $display("[FAIL] %s: polling STATUS fallo (st=%h, cyc=%0d)", label, st, cyc);
                total_errors++;
                return;
            end

            // 5. ISA_READ_DATA (manteniendo IR fijo en ISA_READ_DATA)
            jtag_ir_shift(ISA_READ_DATA);
            for (y = 0; y < H; y++) begin
                for (w_idx = 0; w_idx < wpr; w_idx++) begin
                    jtag_dr_shift(128, 128'd0, rd_word);
                    for (lane = 0; lane < LANES; lane++) begin
                        x = w_idx * 4 + lane;
                        if (x < W) begin
                            px_actual   = rd_word[lane*32 +: 32];
                            px_expected = img_ref[y * W + x];
                            if (px_actual !== px_expected) begin
                                if (case_errors < 5) begin
                                    $display("  [DIFF] %s (%0d,%0d): esp=%h, obt=%h",
                                             label, x, y, px_expected, px_actual);
                                end
                                case_errors++;
                            end
                        end
                    end
                end
            end

            // 6. ISA_NOP y verificar que cycle_count no se altero
            isa_jtag_nop();
            isa_jtag_status(st, dummy[31:0]);
            if (dummy[31:0] !== cyc) begin
                $display("[FAIL] %s: cycle_count cambio tras ISA_NOP (%0d vs %0d)",
                         label, cyc, dummy[31:0]);
                case_errors++;
            end

            if (case_errors == 0) begin
                $display("[PASS] JTAG %-20s | %3dx%-2d | words=%3d | ciclos=%4d | polls=%0d | max_diff=0",
                         label, W, H, wpr * H, cyc, polls);
                tests_passed++;
            end else begin
                $display("[FAIL] JTAG %-20s | errores=%0d", label, case_errors);
                total_errors += case_errors;
            end
        end
    endtask

    // ------------------------------------------------------------------------
    // Secuencia principal de pruebas Fase 2
    // ------------------------------------------------------------------------
    initial begin
        logic [31:0] st, cyc;

        rst_n = 1'b0;
        tdi   = 1'b0;
        ir_in = ISA_NOP;
        v_cdr = 1'b0;
        v_sdr = 1'b0;
        v_udr = 1'b0;
        v_uir = 1'b0;

        repeat (5) @(posedge clk_sys);
        rst_n = 1'b1;
        repeat (3) @(posedge tck);

        $display("====================================================================================");
        $display(" Testbench Fase 2: Protocolo Virtual JTAG Asincrono + CDC 3-FF + Sobel 4-Lane");
        $display("====================================================================================");

        // Test A: ISA_RESET -> ST_IDLE
        isa_jtag_reset();
        isa_jtag_status(st, cyc);
        if (!st[STATUS_BIT_IDLE]) begin
            $display("[FAIL] ISA_RESET por JTAG no entro en ST_IDLE (st=%h)", st);
            total_errors++;
        end else begin
            $display("[PASS] JTAG ISA_RESET            -> ST_IDLE (st=%h)", st);
            tests_passed++;
        end

        // Test B: ISA_CONFIG invalido (0x0) -> ST_ERROR -> ISA_RESET -> ST_IDLE
        isa_jtag_config(16'd0, 16'd0);
        isa_jtag_status(st, cyc);
        if (!st[STATUS_BIT_ERROR]) begin
            $display("[FAIL] JTAG ISA_CONFIG 0x0 no marco ERROR (st=%h)", st);
            total_errors++;
        end else begin
            $display("[PASS] JTAG ISA_CONFIG 0x0       -> ST_ERROR (st=%h)", st);
            tests_passed++;
        end

        isa_jtag_reset();

        // Tests C..G: Tiles completos via JTAG (incluyendo re-ejecucion back-to-back)
        run_jtag_tile_test(1,  1,  42, "Minimo 1x1");
        run_jtag_tile_test(4,  4,  42, "Exacto 4x4");
        run_jtag_tile_test(7,  5,  99, "No mult 7x5");
        run_jtag_tile_test(13, 9,  77, "No mult 13x9");
        run_jtag_tile_test(32, 16, 42, "Tile 32x16");
        run_jtag_tile_test(16, 16, 123, "Segundo tile 16x16");

        $display("====================================================================================");
        if (total_errors == 0) begin
            $display(" [OK] TODAS LAS %0d PRUEBAS JTAG+CDC FASE 2 PASARON BIT A BIT (0 errores)", tests_passed);
        end else begin
            $display(" [ERROR] Fallaron %0d pruebas.", total_errors);
        end
        $display("====================================================================================");

        $finish;
    end

endmodule : tb_jtag_system
