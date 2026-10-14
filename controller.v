module controller (
    input wire clk_i,
    input wire rst_ni,
    input wire ipending_i,

    // Current instruction
    input wire [31:0] instruction_i,

    // Branch operation
    output reg branch_op_o,

    // Immediate value correctly extended
    output reg [31:0] imm_o,

    // Instruction Register control signals
    output reg ir_en_o,

    // PC control signals
    output reg pc_add_imm_o,
    output reg pc_en_o,
    output reg pc_sel_alu_o,
    output reg pc_sel_pc_base_o,

    // Register file control signals
    output reg rf_we_o,

    // Multiplexer control signals
    output reg sel_addr_o,
    output reg sel_b_o,
    output reg sel_mem_o,
    output reg sel_pc_o,
    output reg sel_imm_o,
    output reg sel_csr_o,
    output reg csr_write_o,
    output reg csr_set_o,
    output reg csr_clear_o,
    output reg csr_interrupt_o,
    output reg csr_mret_o,
    output reg pc_sel_mtvec_o,
    output reg pc_sel_mepc_o,

    // Memory control signals
    output reg we_o,

    // ALU control signals
    output reg [5:0] alu_op_o
);
    // States
    localparam [3:0]
        FETCH1 = 4'b0000,
        FETCH2 = 4'b0001,
        DECODE = 4'b0010,
        U_TYPE = 4'b0011,
        R_TYPE = 4'b0100,
        S_TYPE = 4'b0101,
        I_TYPE = 4'b0110,
        BREAK = 4'b0111,
        B_TYPE = 4'b1000,
        JAL_TYPE = 4'b1001,
        JALR_TYPE = 4'b1010,
        LOAD1 = 4'b1011,
        LOAD2 = 4'b1100,
        CSR = 4'b1101,
        INTERRUPT_RETURN = 4'b1110;

    // Op codes
    localparam [6:0]
        R_OPCODE = 7'h33,
        S_OPCODE = 7'h23,
        U_OPCODE = 7'h37,
        I_OPCODE = 7'h13,
        B_OPCODE = 7'h63,
        JAL_OPCODE = 7'h6F,
        JALR_OPCODE = 7'h67,
        LOAD_OPCODE = 7'h03,
        STORE_OPCODE = 7'h23,
        BREAK_OPCODE = 7'h73;

    reg [3:0] current_state;
    reg [3:0] next_state;

    wire [6:0] opcode = instruction_i[6:0];
    wire [2:0] funct3 = instruction_i[14:12];
    wire [6:0] funct7 = instruction_i[31:25];
    wire [11:0] immediate = instruction_i[31:20];
    wire [19:0] s_extension = {20{instruction_i[31]}};
    wire [31:0] sext_immediate = {s_extension, immediate};
    wire [31:0] branch_immediate = {{19{instruction_i[31]}},
                                     instruction_i[31],instruction_i[7],
                                     instruction_i[30:25],instruction_i[11:8],
                                     1'b0};
    wire [31:0] jal_immediate = {{12{instruction_i[31]}},
                                instruction_i[19:12], instruction_i[20],
                                instruction_i[30:21], 1'b0};
    wire [31:0] jalr_immediate = {{21{instruction_i[31]}},instruction_i[30:20]};


    // Next state logic
    always @(*) begin
        case (current_state)
            FETCH1: next_state = FETCH2;
            FETCH2: begin
                next_state = ipending_i ? FETCH1 : DECODE;
            end
            DECODE:
                case (opcode)
                    R_OPCODE: next_state = R_TYPE;
                    I_OPCODE: next_state = I_TYPE;
                    U_OPCODE: next_state = U_TYPE;
                    B_OPCODE: next_state = B_TYPE;
                    JAL_OPCODE: next_state = JAL_TYPE;
                    JALR_OPCODE: next_state = JALR_TYPE;
                    LOAD_OPCODE: next_state = LOAD1;
                    STORE_OPCODE: next_state = S_TYPE;
                    BREAK_OPCODE:
                        case ({immediate, funct3})
                            14'b000000000001000: next_state = BREAK;
                            14'b001100000010000: next_state = INTERRUPT_RETURN;
                            default: next_state = CSR;
                        endcase
                    default: next_state = FETCH1;
                endcase
            U_TYPE: next_state = FETCH2;
            R_TYPE: next_state = FETCH2;
            S_TYPE: next_state = FETCH1;
            I_TYPE: next_state = FETCH2;
            BREAK: next_state = BREAK;
            CSR: next_state = FETCH2;
            INTERRUPT_RETURN: next_state = FETCH1;
            B_TYPE: next_state = FETCH1;
            JAL_TYPE: next_state = FETCH1;
            JALR_TYPE: next_state = FETCH1;
            LOAD1: next_state = LOAD2;
            LOAD2: next_state = FETCH1;
            default: next_state = FETCH1;
        endcase
    end

    // State memory
    always @(posedge clk_i) begin
        if (!rst_ni) begin
            current_state <= FETCH1;
        end
        else begin
            current_state <= next_state;
        end
    end

    // Output control signals
    always @(*) begin
        // initialise
        branch_op_o = 0;
        imm_o = 32'b0;
        ir_en_o = 0;
        pc_add_imm_o = 0;
        pc_en_o = 0;
        pc_sel_alu_o = 0;
        pc_sel_pc_base_o = 0;
        rf_we_o = 0;
        sel_addr_o = 0;
        sel_mem_o = 0;
        sel_pc_o = 0;
        sel_imm_o = 0;
        sel_b_o = 0;
        we_o = 0;
        sel_csr_o = 0;
        csr_write_o = 0;
        csr_set_o = 0;
        csr_clear_o = 0;
        csr_interrupt_o = 0;
        csr_mret_o = 0;
        pc_sel_mepc_o = 0;
        pc_sel_mtvec_o = 0;
        alu_op_o = 6'b0;

        case (current_state)
            FETCH1: begin
                // no output logic
            end
            FETCH2: begin
                pc_en_o = 1;
                if (ipending_i) begin
                    csr_interrupt_o = 1;
                    pc_sel_mtvec_o = 1;
                end else begin
                    ir_en_o = 1;
                end
            end
            DECODE: begin
                // no output logic
            end
            U_TYPE: begin
                sel_imm_o = 1;
                rf_we_o = 1;
                imm_o = {instruction_i[31:12], 12'd0};
            end
            S_TYPE: begin
                sel_addr_o = 1;
                we_o = 1;
                imm_o = {s_extension, instruction_i[31:25], instruction_i[11:7]};
            end
            R_TYPE: begin
                rf_we_o = 1;
                sel_b_o = 1;
            end
            I_TYPE: begin
                rf_we_o = 1;
                imm_o = sext_immediate;
            end
            B_TYPE: begin
                branch_op_o = 1;
                sel_b_o = 1;
                pc_sel_pc_base_o = 1;
                pc_add_imm_o = 1;
                imm_o = branch_immediate;
            end
            JAL_TYPE: begin
                rf_we_o = 1;
                pc_en_o = 1;
                pc_add_imm_o = 1;
                sel_pc_o = 1;
                pc_sel_pc_base_o = 1;
                imm_o = jal_immediate;
            end
            JALR_TYPE: begin
                rf_we_o = 1;
                pc_en_o = 1;
                pc_sel_alu_o = 1;
                sel_pc_o = 1;
                imm_o = jalr_immediate;
            end
            LOAD1: begin
                sel_addr_o = 1;
                we_o = 0;
                imm_o = sext_immediate;
            end
            LOAD2: begin
                rf_we_o = 1;
                sel_addr_o = 1;
                sel_mem_o = 1;
                imm_o = sext_immediate;
            end
            BREAK: begin
                // no output logic
            end
            CSR: begin
                sel_csr_o = 1;
                rf_we_o = 1;
                case (funct3[2])
                    1'b0: begin
                        case (funct3)
                            3'b001: csr_write_o = 1;
                            3'b010: csr_set_o = 1;
                            3'b011: csr_clear_o = 1;
                            default: begin end
                        endcase
                    end
                    1'b1: begin
                        sel_imm_o = 1;
                        imm_o = {27'b0, instruction_i[19:15]};
                        case (funct3)
                            3'b101: csr_write_o = 1;
                            3'b110: csr_set_o = 1;
                            3'b111: csr_clear_o = 1;
                            default: begin end
                        endcase
                    end
                    default: begin end
                endcase
            end

            INTERRUPT_RETURN: begin
                csr_mret_o = 1;
                pc_sel_mepc_o = 1;
                pc_en_o = 1;
            end
            default: begin
            end
        endcase
    end

    // alu_op generater
    always @(*) begin
        case (opcode)
            R_OPCODE:
                case ({funct7,funct3})
                    10'b0000000000: alu_op_o = 6'b000000;
                    10'b0100000000: alu_op_o = 6'b001000;
                    10'b0000000100: alu_op_o = 6'b100100;
                    10'b0000000110: alu_op_o = 6'b100110;
                    10'b0000000111: alu_op_o = 6'b100111;
                    10'b0000000001: alu_op_o = 6'b110001;
                    10'b0000000101: alu_op_o = 6'b110101;
                    10'b0100000101: alu_op_o = 6'b111101;
                    10'b0000000010: alu_op_o = 6'b011100;
                    10'b0000000011: alu_op_o = 6'b011110;
                    default: alu_op_o = 6'b000000;
                endcase
            S_OPCODE: alu_op_o = 6'b000000;
            I_OPCODE:
                case (funct3)
                    3'b000: alu_op_o = 6'b000000;
                    3'b010: alu_op_o = 6'b011100;
                    3'b011: alu_op_o = 6'b011110;
                    3'b100: alu_op_o = 6'b100100;
                    3'b110: alu_op_o = 6'b100110;
                    3'b111: alu_op_o = 6'b100111;
                    3'b001: alu_op_o = 6'b110001;
                    3'b101:
                        case (funct7)
                            7'b0000000: alu_op_o = 6'b110101;
                            7'b0100000: alu_op_o = 6'b111101;
                            default: alu_op_o = 6'b000000;
                        endcase
                    default: alu_op_o = 6'b000000;
                endcase
            B_OPCODE:
                case (funct3)
                    3'b000: alu_op_o = 6'b011000; // BEQ  : A == B
                    3'b001: alu_op_o = 6'b011001; // BNE  : A != B
                    3'b100: alu_op_o = 6'b011100; // BLT  : A < B (signed)
                    3'b101: alu_op_o = 6'b011101; // BGE  : A >= B (signed)
                    3'b110: alu_op_o = 6'b011110; // BLTU : A < B (unsigned)
                    3'b111: alu_op_o = 6'b011111; // BGEU : A >= B (unsigned)
                    default: alu_op_o = 6'b000000;
                endcase
            LOAD_OPCODE: alu_op_o = 6'b000000;
            JAL_OPCODE: alu_op_o = 6'b000000;
            JALR_OPCODE: alu_op_o = 6'b000000;
            default: alu_op_o = 6'b000000;
        endcase
    end
endmodule
