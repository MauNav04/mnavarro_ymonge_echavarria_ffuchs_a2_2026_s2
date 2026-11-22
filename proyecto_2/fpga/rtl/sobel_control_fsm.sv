// ============================================================================
// Proyecto Grupal 2 - Framework Experimental DLP (Sobel RGBA32)
// Archivo: sobel_control_fsm.sv
// Descripcion:
//   Maquina de estados finitos (FSM) y decodificador del ISA minimo del
//   acelerador Sobel. Gestiona los comandos CONFIG, START, STATUS y RESET,
//   almacena las dimensiones del frame y controla el contador de ciclos.
// ============================================================================

module sobel_control_fsm
    import sobel_isa_pkg::*;
(
    input  logic                     clk,
    input  logic                     rst_n,

    // Interfaz de comandos del ISA minimo (desde JTAG / puente de control)
    input  logic                     cmd_valid_i,
    input  isa_opcode_t              cmd_opcode_i,
    input  logic [DIM_WIDTH-1:0]     cfg_width_i,
    input  logic [DIM_WIDTH-1:0]     cfg_height_i,
    input  logic [ADDR_WIDTH-1:0]    cfg_src_addr_i,
    input  logic [ADDR_WIDTH-1:0]    cfg_dst_addr_i,

    // Retroalimentacion del datapath (pulso por cada pixel de salida emitido)
    input  logic                     pixel_out_valid_i,

    // Registros de configuracion hacia el datapath y memoria
    output logic [DIM_WIDTH-1:0]     img_width_o,
    output logic [DIM_WIDTH-1:0]     img_height_o,
    output logic [ADDR_WIDTH-1:0]    src_base_addr_o,
    output logic [ADDR_WIDTH-1:0]    dst_base_addr_o,

    // Senales de control hacia datapath y contador de ciclos
    output logic                     datapath_en_o,
    output logic                     datapath_clear_o,
    output logic                     counter_en_o,
    output logic                     counter_clear_o,

    // Estado observable (para lectura ISA_STATUS y LEDs de depuracion)
    output accel_state_t             state_o,
    output logic [31:0]              status_reg_o,
    output logic [31:0]              processed_pixels_o
);

    accel_state_t state_q;
    logic [31:0]  total_pixels_q;
    logic [31:0]  processed_q;

    // ------------------------------------------------------------------------
    // Logica secuencial de la FSM y registros de configuracion
    // ------------------------------------------------------------------------
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state_q         <= ST_IDLE;
            img_width_o     <= '0;
            img_height_o    <= '0;
            src_base_addr_o <= '0;
            dst_base_addr_o <= '0;
            total_pixels_q  <= '0;
            processed_q     <= '0;
        end else begin
            // Reinicio por software tiene prioridad sobre cualquier estado
            if (cmd_valid_i && (cmd_opcode_i == ISA_RESET)) begin
                state_q         <= ST_IDLE;
                img_width_o     <= '0;
                img_height_o    <= '0;
                src_base_addr_o <= '0;
                dst_base_addr_o <= '0;
                total_pixels_q  <= '0;
                processed_q     <= '0;
            end else begin
                case (state_q)
                    ST_IDLE,
                    ST_CONFIGURED,
                    ST_DONE,
                    ST_ERROR: begin
                        if (cmd_valid_i) begin
                            case (cmd_opcode_i)
                                ISA_CONFIG: begin
                                    img_width_o     <= cfg_width_i;
                                    img_height_o    <= cfg_height_i;
                                    src_base_addr_o <= cfg_src_addr_i;
                                    dst_base_addr_o <= cfg_dst_addr_i;
                                    total_pixels_q  <= cfg_width_i * cfg_height_i;
                                    processed_q     <= '0;
                                    if (cfg_width_i == '0 || cfg_height_i == '0) begin
                                        state_q <= ST_ERROR;
                                    end else begin
                                        state_q <= ST_CONFIGURED;
                                    end
                                end

                                ISA_START: begin
                                    if ((state_q == ST_CONFIGURED || state_q == ST_DONE) &&
                                        (total_pixels_q != 32'd0)) begin
                                        processed_q <= '0;
                                        state_q     <= ST_RUN;
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
                        if (pixel_out_valid_i) begin
                            if (processed_q + 32'd1 >= total_pixels_q) begin
                                processed_q <= total_pixels_q;
                                state_q     <= ST_DONE;
                            end else begin
                                processed_q <= processed_q + 32'd1;
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
    // Salidas combinacionales de control y registro STATUS
    // ------------------------------------------------------------------------
    always_comb begin
        state_o            = state_q;
        processed_pixels_o = processed_q;

        datapath_en_o      = (state_q == ST_RUN);
        counter_en_o       = (state_q == ST_RUN);

        // Limpia datapath y contador al recibir ISA_RESET o un ISA_START valido
        datapath_clear_o   = cmd_valid_i && (
                                 (cmd_opcode_i == ISA_RESET) ||
                                 (cmd_opcode_i == ISA_START &&
                                  (state_q == ST_CONFIGURED || state_q == ST_DONE) &&
                                  (total_pixels_q != 32'd0))
                             );
        counter_clear_o    = datapath_clear_o;

        status_reg_o                        = 32'd0;
        status_reg_o[STATUS_BIT_IDLE]       = (state_q == ST_IDLE);
        status_reg_o[STATUS_BIT_CONFIGURED] = (state_q == ST_CONFIGURED);
        status_reg_o[STATUS_BIT_BUSY]       = (state_q == ST_RUN);
        status_reg_o[STATUS_BIT_DONE]       = (state_q == ST_DONE);
        status_reg_o[STATUS_BIT_ERROR]      = (state_q == ST_ERROR);
        status_reg_o[7:5]                   = state_q;
    end

endmodule : sobel_control_fsm
