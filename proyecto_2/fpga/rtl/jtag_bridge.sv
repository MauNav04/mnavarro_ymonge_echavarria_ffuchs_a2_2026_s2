// ============================================================================
// Proyecto Grupal 2 - Framework Experimental DLP (Sobel RGBA32)
// Archivo: jtag_bridge.sv
// Descripcion:
//   Puente entre Intel Virtual JTAG (`sld_virtual_jtag`, dominio `tck`) y el
//   acelerador Sobel 4-lane (`sobel_accelerator_top`, dominio `clk_sys`).
//
// Responsabilidades:
//   1. Dominio `tck` (IEEE 1149.1 Virtual TAP):
//        - Implementa el Data Register (DR) de longitud variable segun IR[3:0]:
//            * ISA_NOP        (4'h0) :   1 bit  (bypass)
//            * ISA_CONFIG     (4'h1) :  32 bits ({width[15:0], height[15:0]})
//            * ISA_START      (4'h2) :   1 bit  (disparo en v_udr)
//            * ISA_STATUS     (4'h3) :  64 bits ({status_reg[31:0], cycle_count[31:0]})
//            * ISA_WRITE_DATA (4'h4) : 128 bits (4 pixeles RGBA32 -> BRAM_IN)
//            * ISA_READ_DATA  (4'h5) : 128 bits (4 pixeles RGBA32 <- BRAM_OUT)
//            * ISA_RESET      (4'hF) :   1 bit  (disparo en v_udr)
//        - Todas las operaciones se sincronizan a `posedge tck` (sin usar
//          `v_udr` como reloj derivado, evitando warnings de timing).
//        - En `v_cdr` captura datos de lectura (STATUS o BRAM_OUT).
//        - En `v_sdr` desplaza LSB-first (`{tdi, dr[N-1:1]}`, `tdo = dr[0]`).
//        - En `v_udr` registra opcode/payload y conmuta `tck_cmd_toggle_q`.
//
//   2. Cruce de dominios de reloj (CDC 3-FF: `tck` -> `clk_sys`):
//        - 2 flip-flops de sincronizacion contra metaestabilidad + 1 flip-flop
//          detector de flanco (`sync[2] ^ sync[1]`).
//        - Genera pulsos limpios de exactamente 1 ciclo de `clk_sys` para
//          `cmd_valid_o` y `jtag_wr_en_o`.
//
//   3. Coherencia de lectura (`clk_sys` -> `tck`):
//        - Retrasa 1 ciclo de `clk_sys` la bandera `STATUS_BIT_DONE` expuesta
//          hacia `tck` para asegurar que `cycle_count_i` ya este congelado
//          antes de que el host vea `DONE=1`.
// ============================================================================

module jtag_bridge
    import sobel_isa_pkg::*;
(
    // Reloj y reset del acelerador (dominio `clk_sys`)
    input  logic                     clk_sys,
    input  logic                     rst_n,

    // Interfaz Virtual JTAG (dominio `tck`, desde `vjtag_wrapper`)
    input  logic                     tck,
    input  logic                     tdi,
    input  logic [3:0]               ir_in,
    input  logic                     v_cdr,
    input  logic                     v_sdr,
    input  logic                     v_udr,
    input  logic                     v_uir,
    output logic                     tdo,

    // Entradas de lectura desde el acelerador (dominio `clk_sys`)
    input  logic [31:0]              status_reg_i,
    input  logic [COUNTER_WIDTH-1:0] cycle_count_i,
    input  logic [WORD_WIDTH-1:0]    rd_data_i,

    // Salidas de control y datos hacia el acelerador (dominio `clk_sys`)
    output logic                     cmd_valid_o,
    output isa_opcode_t              cmd_opcode_o,
    output logic [DIM_WIDTH-1:0]     cfg_width_o,
    output logic [DIM_WIDTH-1:0]     cfg_height_o,
    output logic                     jtag_wr_en_o,
    output logic [WORD_WIDTH-1:0]    wr_data_o,

    // Indicador de actividad JTAG (para diagnostico en LEDR)
    output logic                     jtag_activity_o
);

    // Evita warning por puerto v_uir no utilizado (todos los disparos ocurren en v_udr)
    logic unused_v_uir;
    assign unused_v_uir = v_uir;

    // ========================================================================
    // 1. Coherencia de lectura en `clk_sys` (congela cycle_count antes de DONE)
    // ========================================================================
    logic [31:0]              status_jtag_q;
    logic [COUNTER_WIDTH-1:0] cycle_jtag_q;
    logic                     done_prev_q;

    always_ff @(posedge clk_sys or negedge rst_n) begin
        if (!rst_n) begin
            status_jtag_q <= 32'd0;
            cycle_jtag_q  <= '0;
            done_prev_q   <= 1'b0;
        end else begin
            done_prev_q  <= status_reg_i[STATUS_BIT_DONE];
            cycle_jtag_q <= cycle_count_i;

            // Expone DONE hacia JTAG 1 ciclo despues de congelarse cycle_count
            status_jtag_q                  <= status_reg_i;
            status_jtag_q[STATUS_BIT_DONE] <= status_reg_i[STATUS_BIT_DONE] && done_prev_q;
        end
    end

    // ========================================================================
    // 2. Logica de desplazamiento y captura en dominio `tck` (Virtual JTAG)
    // ========================================================================
    isa_opcode_t           cur_ir;
    assign cur_ir = isa_opcode_t'(ir_in);

    logic                  dr_bypass_q;      // DR 1 bit (NOP/START/RESET)
    logic [WORD_WIDTH-1:0] dr_shift_q;       // DR compartido (32b/64b/128b)

    // Registros de transferencia estables hacia dominio `clk_sys`
    logic                  tck_cmd_toggle_q; // Conmuta en cada comando valido
    isa_opcode_t           tck_opcode_q;
    logic [WORD_WIDTH-1:0] tck_payload_q;

    // Multiplexor combinacional de salida serial TDO (siempre LSB primero)
    always_comb begin
        case (cur_ir)
            ISA_CONFIG,
            ISA_STATUS,
            ISA_WRITE_DATA,
            ISA_READ_DATA: tdo = dr_shift_q[0];
            default:       tdo = dr_bypass_q;
        endcase
    end

    always_ff @(posedge tck or negedge rst_n) begin
        if (!rst_n) begin
            dr_bypass_q      <= 1'b0;
            dr_shift_q       <= '0;
            tck_cmd_toggle_q <= 1'b0;
            tck_opcode_q     <= ISA_NOP;
            tck_payload_q    <= '0;
        end else begin
            // ---- Fase Capture-DR (`v_cdr`): carga datos de lectura hacia PC ----
            if (v_cdr) begin
                case (cur_ir)
                    ISA_STATUS: begin
                        // DR[63:32] = status_reg, DR[31:0] = cycle_count
                        dr_shift_q[63:0] <= {status_jtag_q, cycle_jtag_q};
                    end
                    ISA_READ_DATA: begin
                        // DR[127:0] = 4 pixeles RGBA32 desde BRAM_OUT[rd_ptr]
                        dr_shift_q <= rd_data_i;
                    end
                    default: begin
                        dr_bypass_q <= 1'b0;
                    end
                endcase
            end
            // ---- Fase Shift-DR (`v_sdr`): desplazamiento serial LSB-first ----
            else if (v_sdr) begin
                case (cur_ir)
                    ISA_CONFIG: begin
                        // Cadena de exactamente 32 bits
                        dr_shift_q[31:0] <= {tdi, dr_shift_q[31:1]};
                    end
                    ISA_STATUS: begin
                        // Cadena de exactamente 64 bits
                        dr_shift_q[63:0] <= {tdi, dr_shift_q[63:1]};
                    end
                    ISA_WRITE_DATA,
                    ISA_READ_DATA: begin
                        // Cadena de exactamente 128 bits
                        dr_shift_q <= {tdi, dr_shift_q[WORD_WIDTH-1:1]};
                    end
                    default: begin
                        // ISA_NOP, ISA_START, ISA_RESET: cadena de 1 bit
                        dr_bypass_q <= tdi;
                    end
                endcase
            end

            // ---- Fase Update-DR (`v_udr`): lanza comando hacia `clk_sys` ----
            if (v_udr) begin
                case (cur_ir)
                    ISA_CONFIG,
                    ISA_START,
                    ISA_WRITE_DATA,
                    ISA_READ_DATA,
                    ISA_RESET: begin
                        tck_opcode_q     <= cur_ir;
                        tck_payload_q    <= dr_shift_q;
                        tck_cmd_toggle_q <= ~tck_cmd_toggle_q;
                    end
                    default: begin
                        // ISA_NOP e ISA_STATUS no disparan cambios en la FSM
                    end
                endcase
            end
        end
    end

    // ========================================================================
    // 3. Sincronizador CDC 3-FF (`tck` -> `clk_sys`)
    //    sync_toggle_q[1:0] : 2 FF anti-metaestabilidad
    //    sync_toggle_q[2]   : 3er FF para deteccion de flanco (XOR)
    // ========================================================================
    (* altera_attribute = "-name SYNCHRONIZER_IDENTIFICATION FORCED_IF_ASYNCHRONOUS" *)
    logic [2:0] sync_toggle_q;
    logic       cdc_pulse_sys;

    always_ff @(posedge clk_sys or negedge rst_n) begin
        if (!rst_n) begin
            sync_toggle_q <= 3'b000;
        end else begin
            sync_toggle_q <= {sync_toggle_q[1:0], tck_cmd_toggle_q};
        end
    end

    assign cdc_pulse_sys = sync_toggle_q[2] ^ sync_toggle_q[1];

    // Registro de salida limpio en dominio `clk_sys` (pulso exacto de 1 ciclo)
    always_ff @(posedge clk_sys or negedge rst_n) begin
        if (!rst_n) begin
            cmd_valid_o     <= 1'b0;
            cmd_opcode_o    <= ISA_NOP;
            cfg_width_o     <= '0;
            cfg_height_o    <= '0;
            jtag_wr_en_o    <= 1'b0;
            wr_data_o       <= '0;
            jtag_activity_o <= 1'b0;
        end else if (cdc_pulse_sys) begin
            cmd_valid_o     <= 1'b1;
            cmd_opcode_o    <= tck_opcode_q;
            cfg_width_o     <= tck_payload_q[31:16];
            cfg_height_o    <= tck_payload_q[15:0];
            jtag_wr_en_o    <= (tck_opcode_q == ISA_WRITE_DATA);
            wr_data_o       <= tck_payload_q;
            jtag_activity_o <= ~jtag_activity_o;
        end else begin
            cmd_valid_o     <= 1'b0;
            jtag_wr_en_o    <= 1'b0;
        end
    end

endmodule : jtag_bridge
