// ============================================================================
// Proyecto Grupal 2 - Framework Experimental DLP (Sobel RGBA32)
// Archivo: sobel_control_fsm.sv
// Descripcion:
//   Unidad de control (FSM) y decodificador del ISA minimo del acelerador
//   Sobel 4-lane. Gestiona:
//     - Decodificacion de comandos ISA (NOP, CONFIG, START, STATUS, WRITE,
//       READ, RESET).
//     - Validacion de dimensiones y limite de capacidad de BRAM (8192 palabras).
//     - Punteros auto-incrementales:
//         * wr_ptr_o     : escritura Host/JTAG -> BRAM_IN (ISA_WRITE_DATA)
//         * rd_ptr_o     : lectura BRAM_OUT -> Host/JTAG  (ISA_READ_DATA)
//         * out_wr_ptr_o : escritura 4x Sobel -> BRAM_OUT (sincronizado con
//                          word_out_valid_i tras las 2 etapas de pipeline)
//     - Control del datapath y del contador de ciclos (solo en ST_RUN).
// ============================================================================

module sobel_control_fsm
    import sobel_isa_pkg::*;
#(
    parameter int P_MAX_WIDTH  = MAX_WIDTH,
    parameter int P_BRAM_DEPTH = BRAM_DEPTH,
    parameter int P_BRAM_ADDRW = $clog2(P_BRAM_DEPTH)
) (
    input  logic                    clk,
    input  logic                    rst_n,

    // Interfaz de comandos del ISA minimo (desde jtag_bridge en dominio clk_sys)
    input  logic                    cmd_valid_i,
    input  isa_opcode_t             cmd_opcode_i,
    input  logic [DIM_WIDTH-1:0]    cfg_width_i,
    input  logic [DIM_WIDTH-1:0]    cfg_height_i,

    // Retroalimentacion del datapath (pulso por cada palabra de 128b emitida)
    input  logic                    word_out_valid_i,

    // Registros de configuracion hacia line_buffer_4lane
    output logic [DIM_WIDTH-1:0]    img_width_o,
    output logic [DIM_WIDTH-1:0]    img_height_o,
    output logic [WPR_WIDTH-1:0]    words_per_row_o,

    // Punteros y habilitaciones de memoria BRAM_IN / BRAM_OUT
    output logic                    bram_in_we_o,
    output logic [P_BRAM_ADDRW-1:0] wr_ptr_o,
    output logic [P_BRAM_ADDRW-1:0] rd_ptr_o,
    output logic                    bram_out_we_o,
    output logic [P_BRAM_ADDRW-1:0] out_wr_ptr_o,

    // Senales de control hacia datapath y contador de ciclos
    output logic                    datapath_en_o,
    output logic                    datapath_clear_o,
    output logic                    counter_en_o,
    output logic                    counter_clear_o,

    // Estado observable (para lectura ISA_STATUS y LEDs de depuracion)
    output accel_state_t            state_o,
    output logic [31:0]             status_reg_o,
    output logic [31:0]             processed_words_o
);

    accel_state_t            state_q;
    logic [31:0]             total_words_q;
    logic [31:0]             processed_words_q;
    logic [P_BRAM_ADDRW-1:0] wr_ptr_q;
    logic [P_BRAM_ADDRW-1:0] rd_ptr_q;
    logic [P_BRAM_ADDRW-1:0] out_wr_ptr_q;

    // Calculo combinacional de palabras/fila y total de palabras para ISA_CONFIG
    logic [16:0]             cfg_w_plus3_comb;
    logic [WPR_WIDTH-1:0]    cfg_wpr_comb;
    logic [31:0]             cfg_total_words_comb;
    logic                    cfg_valid_comb;

    always_comb begin
        // ceil(width / 4) = (width + 3) >> 2 (sin truncamiento ni overflow)
        cfg_w_plus3_comb     = {1'b0, cfg_width_i} + 17'd3;
        cfg_wpr_comb         = cfg_w_plus3_comb[15:2];
        cfg_total_words_comb = {18'd0, cfg_wpr_comb} * {16'd0, cfg_height_i};
        cfg_valid_comb       = (cfg_width_i != 16'd0) &&
                               (cfg_height_i != 16'd0) &&
                               (cfg_width_i <= P_MAX_WIDTH[DIM_WIDTH-1:0]) &&
                               (cfg_total_words_comb != 32'd0) &&
                               (cfg_total_words_comb <= P_BRAM_DEPTH[31:0]);
    end

    // ------------------------------------------------------------------------
    // Logica secuencial de la FSM, CSRs y punteros de BRAM
    // ------------------------------------------------------------------------
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state_q           <= ST_IDLE;
            img_width_o       <= '0;
            img_height_o      <= '0;
            words_per_row_o   <= '0;
            total_words_q     <= '0;
            processed_words_q <= '0;
            wr_ptr_q          <= '0;
            rd_ptr_q          <= '0;
            out_wr_ptr_q      <= '0;
        end else begin
            // Reinicio por software tiene prioridad sobre cualquier estado
            if (cmd_valid_i && (cmd_opcode_i == ISA_RESET)) begin
                state_q           <= ST_IDLE;
                img_width_o       <= '0;
                img_height_o      <= '0;
                words_per_row_o   <= '0;
                total_words_q     <= '0;
                processed_words_q <= '0;
                wr_ptr_q          <= '0;
                rd_ptr_q          <= '0;
                out_wr_ptr_q      <= '0;
            end else begin
                case (state_q)
                    ST_IDLE,
                    ST_CONFIGURED,
                    ST_DONE,
                    ST_ERROR: begin
                        if (cmd_valid_i) begin
                            case (cmd_opcode_i)
                                ISA_CONFIG: begin
                                    wr_ptr_q          <= '0;
                                    rd_ptr_q          <= '0;
                                    out_wr_ptr_q      <= '0;
                                    processed_words_q <= '0;
                                    if (cfg_valid_comb) begin
                                        img_width_o     <= cfg_width_i;
                                        img_height_o    <= cfg_height_i;
                                        words_per_row_o <= cfg_wpr_comb;
                                        total_words_q   <= cfg_total_words_comb;
                                        state_q         <= ST_CONFIGURED;
                                    end else begin
                                        img_width_o     <= '0;
                                        img_height_o    <= '0;
                                        words_per_row_o <= '0;
                                        total_words_q   <= '0;
                                        state_q         <= ST_ERROR;
                                    end
                                end

                                ISA_WRITE_DATA: begin
                                    // Avanza wr_ptr despues de escribir en BRAM_IN[wr_ptr_q]
                                    if (state_q == ST_CONFIGURED || state_q == ST_DONE) begin
                                        wr_ptr_q <= wr_ptr_q + {{(P_BRAM_ADDRW-1){1'b0}}, 1'b1};
                                    end
                                end

                                ISA_READ_DATA: begin
                                    // Avanza rd_ptr despues de leer BRAM_OUT[rd_ptr_q]
                                    if (state_q == ST_DONE || state_q == ST_CONFIGURED) begin
                                        rd_ptr_q <= rd_ptr_q + {{(P_BRAM_ADDRW-1){1'b0}}, 1'b1};
                                    end
                                end

                                ISA_START: begin
                                    if ((state_q == ST_CONFIGURED || state_q == ST_DONE) &&
                                        (total_words_q != 32'd0)) begin
                                        processed_words_q <= '0;
                                        out_wr_ptr_q      <= '0;
                                        rd_ptr_q          <= '0;
                                        state_q           <= ST_RUN;
                                    end else begin
                                        state_q <= ST_ERROR;
                                    end
                                end

                                default: begin
                                    // ISA_NOP e ISA_STATUS mantienen el estado actual
                                end
                            endcase
                        end
                    end

                    ST_RUN: begin
                        if (word_out_valid_i) begin
                            if (processed_words_q + 32'd1 >= total_words_q) begin
                                processed_words_q <= total_words_q;
                                out_wr_ptr_q      <= out_wr_ptr_q + {{(P_BRAM_ADDRW-1){1'b0}}, 1'b1};
                                state_q           <= ST_DONE;
                            end else begin
                                processed_words_q <= processed_words_q + 32'd1;
                                out_wr_ptr_q      <= out_wr_ptr_q + {{(P_BRAM_ADDRW-1){1'b0}}, 1'b1};
                            end
                        end
                    end

                    default: begin
                        state_q <= ST_IDLE;
                    end
                endcase
            end
        end
    end

    // ------------------------------------------------------------------------
    // Salidas combinacionales de control, punteros y registro STATUS
    // ------------------------------------------------------------------------
    always_comb begin
        state_o           = state_q;
        processed_words_o = processed_words_q;
        wr_ptr_o          = wr_ptr_q;
        rd_ptr_o          = rd_ptr_q;
        out_wr_ptr_o      = out_wr_ptr_q;

        // Escritura en BRAM_IN habilitada solo cuando el acelerador no corre
        bram_in_we_o      = cmd_valid_i &&
                            (cmd_opcode_i == ISA_WRITE_DATA) &&
                            (state_q == ST_CONFIGURED || state_q == ST_DONE);

        // Escritura en BRAM_OUT sincronizada con cada palabra valida del pipeline
        bram_out_we_o     = (state_q == ST_RUN) && word_out_valid_i;

        datapath_en_o     = (state_q == ST_RUN);
        counter_en_o      = (state_q == ST_RUN);

        // Limpia datapath y contador al recibir ISA_RESET o un ISA_START valido
        datapath_clear_o  = cmd_valid_i && (
                                (cmd_opcode_i == ISA_RESET) ||
                                (cmd_opcode_i == ISA_START &&
                                 (state_q == ST_CONFIGURED || state_q == ST_DONE) &&
                                 (total_words_q != 32'd0))
                            );
        counter_clear_o   = datapath_clear_o;

        status_reg_o                        = 32'd0;
        status_reg_o[STATUS_BIT_IDLE]       = (state_q == ST_IDLE);
        status_reg_o[STATUS_BIT_CONFIGURED] = (state_q == ST_CONFIGURED);
        status_reg_o[STATUS_BIT_BUSY]       = (state_q == ST_RUN);
        status_reg_o[STATUS_BIT_DONE]       = (state_q == ST_DONE);
        status_reg_o[STATUS_BIT_ERROR]      = (state_q == ST_ERROR);
        status_reg_o[7:5]                   = state_q;
    end

endmodule : sobel_control_fsm
