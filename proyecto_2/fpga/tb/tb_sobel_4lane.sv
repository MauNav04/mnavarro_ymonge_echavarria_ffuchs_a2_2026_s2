// ============================================================================
// Proyecto Grupal 2 - Framework Experimental DLP (Sobel RGBA32)
// Archivo: tb_sobel_4lane.sv
// Descripcion:
//   Testbench de verificacion exhaustiva para Fase 1 (ModelSim-Altera).
//   Valida el acelerador `sobel_accelerator_top` (4 lanes + BRAM_IN/BRAM_OUT
//   M10K + line_buffer_4lane + sobel_control_fsm + cycle_counter) bit a bit
//   contra un modelo dorado identico a `sobel_sequential.cpp` (max_diff == 0).
//
// Casos probados:
//   - Manejo de errores ISA (0x0, width > MAX_WIDTH, tile > BRAM_DEPTH) + RESET
//   - Dimensiones limite y no multiplos de 4: 1x1, 2x2, 3x3, 4x4, 5x1, 1x5,
//     7x5, 13x9
//   - Patrones unitarios: constante, borde vertical, borde horizontal, impulso,
//     canal aislado y Xorshift32 determinista (identico a image_rgba.cpp)
//   - Escala intermedia y capacidad maxima de tile en BRAM: 64x64 y 512x64
// ============================================================================

`timescale 1ns/1ps

module tb_sobel_4lane;
    import sobel_isa_pkg::*;

    // Reloj objetivo de 100 MHz (periodo = 10 ns)
    logic clk;
    logic rst_n;

    initial clk = 1'b0;
    always #5 clk = ~clk;

    // Senales de interfaz del DUT (`sobel_accelerator_top`)
    logic                     cmd_valid;
    isa_opcode_t              cmd_opcode;
    logic [DIM_WIDTH-1:0]     cfg_width;
    logic [DIM_WIDTH-1:0]     cfg_height;
    logic [WORD_WIDTH-1:0]    jtag_wr_data;
    logic [WORD_WIDTH-1:0]    jtag_rd_data;
    accel_state_t             state;
    logic [31:0]              status_reg;
    logic [COUNTER_WIDTH-1:0] cycle_count;
    logic [31:0]              processed_words;

    sobel_accelerator_top #(
        .P_MAX_WIDTH  (MAX_WIDTH),
        .P_BRAM_DEPTH (BRAM_DEPTH)
    ) u_dut (
        .clk               (clk),
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

    // Buffers de simulacion para imagen de entrada y referencia dorada
    // Maximo probado: 512 x 64 = 32,768 pixeles
    localparam int MAX_TEST_PIXELS = 32768;
    logic [31:0] img_in  [0:MAX_TEST_PIXELS-1];
    logic [31:0] img_ref [0:MAX_TEST_PIXELS-1];

    int total_errors = 0;
    int tests_passed = 0;

    // ------------------------------------------------------------------------
    // Modelo dorado bit a bit (identico a `sobel_sequential.cpp`)
    // ------------------------------------------------------------------------
    function automatic int abs_int(input int v);
        return (v < 0) ? -v : v;
    endfunction

    function automatic logic [7:0] sat8(input int v);
        return (v > 255) ? 8'd255 : v[7:0];
    endfunction

    task automatic compute_golden_sobel(input int W, input int H);
        int x, y, i, j, sx, sy, c;
        int wx, wy, sample;
        int gx [0:2];
        int gy [0:2];
        logic [31:0] px;
        logic [7:0]  out_r, out_g, out_b;
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

                            sample = int'(px[7:0]);
                            gx[0] += sample * wx; gy[0] += sample * wy;

                            sample = int'(px[15:8]);
                            gx[1] += sample * wx; gy[1] += sample * wy;

                            sample = int'(px[23:16]);
                            gx[2] += sample * wx; gy[2] += sample * wy;
                        end
                    end

                    out_r = sat8(abs_int(gx[0]) + abs_int(gy[0]));
                    out_g = sat8(abs_int(gx[1]) + abs_int(gy[1]));
                    out_b = sat8(abs_int(gx[2]) + abs_int(gy[2]));
                    img_ref[y * W + x] = pack_rgba(out_r, out_g, out_b, 8'hFF);
                end
            end
        end
    endtask

    // ------------------------------------------------------------------------
    // Generadores de imagenes de prueba (identicos a `image_rgba.cpp`)
    // ------------------------------------------------------------------------
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

    task automatic fill_constant(input int W, input int H,
                                 input logic [7:0] r, input logic [7:0] g, input logic [7:0] b);
        int idx;
        begin
            for (idx = 0; idx < W * H; idx++) begin
                img_in[idx] = pack_rgba(r, g, b, 8'hFF);
            end
        end
    endtask

    task automatic fill_vertical_edge(input int W, input int H);
        int x, y;
        logic [7:0] v;
        begin
            for (y = 0; y < H; y++) begin
                for (x = 0; x < W; x++) begin
                    v = (x < (W / 2)) ? 8'd0 : 8'd255;
                    img_in[y * W + x] = pack_rgba(v, v, v, 8'hFF);
                end
            end
        end
    endtask

    task automatic fill_horizontal_edge(input int W, input int H);
        int x, y;
        logic [7:0] v;
        begin
            for (y = 0; y < H; y++) begin
                v = (y < (H / 2)) ? 8'd0 : 8'd255;
                for (x = 0; x < W; x++) begin
                    img_in[y * W + x] = pack_rgba(v, v, v, 8'hFF);
                end
            end
        end
    endtask

    task automatic fill_impulse(input int W, input int H, input int ix, input int iy);
        begin
            fill_constant(W, H, 8'd0, 8'd0, 8'd0);
            img_in[iy * W + ix] = pack_rgba(8'd10, 8'd20, 8'd30, 8'hFF);
        end
    endtask

    task automatic fill_isolated_channel(input int W, input int H, input int ch, input logic [7:0] val);
        int x, y;
        logic [7:0] r, g, b;
        begin
            for (y = 0; y < H; y++) begin
                for (x = 0; x < W; x++) begin
                    r = (ch == 0 && x >= W/2) ? val : 8'd0;
                    g = (ch == 1 && x >= W/2) ? val : 8'd0;
                    b = (ch == 2 && x >= W/2) ? val : 8'd0;
                    img_in[y * W + x] = pack_rgba(r, g, b, 8'hFF);
                end
            end
        end
    endtask

    // ------------------------------------------------------------------------
    // Utilidades de envio de comandos ISA hacia el DUT (1 ciclo por comando)
    // ------------------------------------------------------------------------
    task automatic send_cmd(
        input isa_opcode_t         op,
        input logic [DIM_WIDTH-1:0] w = 16'd0,
        input logic [DIM_WIDTH-1:0] h = 16'd0,
        input logic [WORD_WIDTH-1:0] wr_d = '0
    );
        begin
            @(negedge clk);
            cmd_valid    = 1'b1;
            cmd_opcode   = op;
            cfg_width    = w;
            cfg_height   = h;
            jtag_wr_data = wr_d;
            @(posedge clk);
            @(negedge clk);
            cmd_valid    = 1'b0;
            cmd_opcode   = ISA_NOP;
        end
    endtask

    // ------------------------------------------------------------------------
    // Ejecucion completa de un caso de prueba ISA + comparacion bit a bit
    // ------------------------------------------------------------------------
    task automatic run_and_verify_case(
        input int    W,
        input int    H,
        input string label
    );
        int wpr, y, w_idx, lane, x;
        int case_errors;
        int timeout;
        logic [WORD_WIDTH-1:0] wr_word;
        logic [WORD_WIDTH-1:0] rd_word;
        logic [31:0] px_actual, px_expected;
        real px_per_cycle;
        begin
            case_errors = 0;
            wpr = (W + 3) / 4;

            // 1. Calcular referencia dorada exacta
            compute_golden_sobel(W, H);

            // 2. ISA_CONFIG (carga W, H y reinicia punteros wr_ptr/rd_ptr)
            send_cmd(ISA_CONFIG, W[15:0], H[15:0], '0);
            @(posedge clk);
            #1;
            if (state !== ST_CONFIGURED) begin
                $display("[FAIL] %s (%0dx%0d): esperado ST_CONFIGURED, obtenido state=%0d",
                         label, W, H, state);
                total_errors++;
                return;
            end

            // 3. ISA_WRITE_DATA: cargar palabras de 128b en BRAM_IN
            //    Nota: en los pixeles de relleno (x >= W) ponemos basura 0xDEADBEEF
            //    a proposito para validar que `line_buffer_4lane` los enmascara a 0!
            for (y = 0; y < H; y++) begin
                for (w_idx = 0; w_idx < wpr; w_idx++) begin
                    wr_word = '0;
                    for (lane = 0; lane < LANES; lane++) begin
                        x = w_idx * 4 + lane;
                        if (x < W) begin
                            wr_word[lane*32 +: 32] = img_in[y * W + x];
                        end else begin
                            wr_word[lane*32 +: 32] = 32'hDEAD_BEEF;
                        end
                    end
                    send_cmd(ISA_WRITE_DATA, 16'd0, 16'd0, wr_word);
                end
            end

            // 4. ISA_START: iniciar computo 4-lane y esperar ST_DONE
            send_cmd(ISA_START);
            timeout = 0;
            while (state !== ST_DONE && timeout < (wpr * H * 4 + 200)) begin
                @(posedge clk);
                #1;
                timeout++;
            end

            if (state !== ST_DONE) begin
                $display("[FAIL] %s (%0dx%0d): timeout esperando ST_DONE (state=%0d, proc=%0d)",
                         label, W, H, state, processed_words);
                total_errors++;
                return;
            end

            // 5. ISA_READ_DATA: leer BRAM_OUT palabra a palabra y comparar bit a bit
            //    Esperar 1 ciclo tras ST_DONE para que BRAM_OUT exponga addr 0
            @(posedge clk);
            for (y = 0; y < H; y++) begin
                for (w_idx = 0; w_idx < wpr; w_idx++) begin
                    #1;
                    rd_word = jtag_rd_data;
                    // Enviar ISA_READ_DATA para avanzar rd_ptr++ (como hace jtag_bridge tras v_cdr)
                    send_cmd(ISA_READ_DATA);
                    // Esperar 1 ciclo de latencia sincrona de BRAM_OUT para la siguiente direccion
                    @(posedge clk);

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

            px_per_cycle = real'(W * H) / real'(cycle_count);
            if (case_errors == 0) begin
                $display("[PASS] %-24s | %4dx%-3d | words=%4d | ciclos=%5d | %.2f px/ciclo | max_diff=0",
                         label, W, H, wpr * H, cycle_count, px_per_cycle);
                tests_passed++;
            end else begin
                $display("[FAIL] %-24s | %4dx%-3d | errores=%0d", label, W, H, case_errors);
                total_errors += case_errors;
            end
        end
    endtask

    // ------------------------------------------------------------------------
    // Secuencia principal de pruebas
    // ------------------------------------------------------------------------
    initial begin
        rst_n        = 1'b0;
        cmd_valid    = 1'b0;
        cmd_opcode   = ISA_NOP;
        cfg_width    = '0;
        cfg_height   = '0;
        jtag_wr_data = '0;

        repeat (4) @(posedge clk);
        rst_n = 1'b1;
        repeat (2) @(posedge clk);

        $display("====================================================================================");
        $display(" Testbench Fase 1: Acelerador Sobel FPGA 4-Lane (Validacion Bit a Bit vs Secuencial)");
        $display("====================================================================================");

        // --- Test de validacion de errores ISA_CONFIG + ISA_RESET ---
        send_cmd(ISA_CONFIG, 16'd0, 16'd0);
        @(posedge clk); #1;
        if (state !== ST_ERROR) begin
            $display("[FAIL] ISA_CONFIG 0x0 no entro en ST_ERROR (state=%0d)", state);
            total_errors++;
        end else begin
            $display("[PASS] ISA_CONFIG 0x0           -> ST_ERROR detectado correctamente");
            tests_passed++;
        end

        send_cmd(ISA_CONFIG, 16'd4000, 16'd4); // > MAX_WIDTH (3840)
        @(posedge clk); #1;
        if (state !== ST_ERROR) begin
            $display("[FAIL] ISA_CONFIG 4000x4 no entro en ST_ERROR (state=%0d)", state);
            total_errors++;
        end else begin
            $display("[PASS] ISA_CONFIG 4000x4        -> ST_ERROR (> MAX_WIDTH) detectado");
            tests_passed++;
        end

        send_cmd(ISA_CONFIG, 16'd512, 16'd65); // 128*65 = 8320 > BRAM_DEPTH (8192)
        @(posedge clk); #1;
        if (state !== ST_ERROR) begin
            $display("[FAIL] ISA_CONFIG 512x65 no entro en ST_ERROR (state=%0d)", state);
            total_errors++;
        end else begin
            $display("[PASS] ISA_CONFIG 512x65        -> ST_ERROR (> BRAM_DEPTH) detectado");
            tests_passed++;
        end

        send_cmd(ISA_RESET);
        @(posedge clk); #1;
        if (state !== ST_IDLE) begin
            $display("[FAIL] ISA_RESET no regreso a ST_IDLE (state=%0d)", state);
            total_errors++;
        end else begin
            $display("[PASS] ISA_RESET                -> ST_IDLE restaurado");
            tests_passed++;
        end

        $display("------------------------------------------------------------------------------------");

        // --- Casos funcionales y de bordes (exactitud bit a bit) ---
        fill_deterministic(1, 1, 42);
        run_and_verify_case(1, 1, "Minimo 1x1");

        fill_deterministic(2, 2, 42);
        run_and_verify_case(2, 2, "Pequeno 2x2 (w<4)");

        fill_impulse(3, 3, 1, 1);
        run_and_verify_case(3, 3, "Impulso 3x3 (w<4)");

        fill_vertical_edge(4, 4);
        run_and_verify_case(4, 4, "Borde vertical 4x4");

        fill_deterministic(5, 1, 42);
        run_and_verify_case(5, 1, "Fila unica 5x1 (rem=1)");

        fill_deterministic(1, 5, 42);
        run_and_verify_case(1, 5, "Columna unica 1x5");

        fill_deterministic(7, 5, 42);
        run_and_verify_case(7, 5, "No multiplo 7x5 (rem=3)");

        fill_deterministic(13, 9, 42);
        run_and_verify_case(13, 9, "No multiplo 13x9 (rem=1)");

        fill_constant(16, 16, 8'd100, 8'd150, 8'd200);
        run_and_verify_case(16, 16, "Constante 16x16");

        fill_horizontal_edge(16, 16);
        run_and_verify_case(16, 16, "Borde horizontal 16x16");

        fill_isolated_channel(16, 16, 1, 8'd180);
        run_and_verify_case(16, 16, "Canal G aislado 16x16");

        fill_deterministic(64, 64, 42);
        run_and_verify_case(64, 64, "Determinista 64x64");

        fill_deterministic(512, 64, 42);
        run_and_verify_case(512, 64, "Tile max BRAM 512x64");

        $display("====================================================================================");
        if (total_errors == 0) begin
            $display(" [OK] TODAS LAS %0d PRUEBAS FASE 1 PASARON BIT A BIT (0 errores)", tests_passed);
        end else begin
            $display(" [ERROR] Fallaron %0d verificaciones.", total_errors);
        end
        $display("====================================================================================");

        $finish;
    end

endmodule : tb_sobel_4lane
