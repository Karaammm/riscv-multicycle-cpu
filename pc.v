module pc (
    input  wire        clk_i,
    input  wire        rst_ni,
    input  wire        en_i,
    input  wire        sel_alu_i,
    input  wire        sel_pc_base_i,
    input  wire        add_imm_i,
    input  wire [31:0] imm_i,
    input  wire [31:0] alu_i,
    input wire sel_mtvec_i,
    input wire [31:0] mtvec_i,
    input wire sel_mepc_i,
    input wire [31:0] mepc_i,
    output wire [31:0] addr_o
);

  ////////////////////
  // Internal Registers
  ////////////////////
  reg  [31:0] pc_reg;
  reg  [31:0] next_pc_r;

  ////////////////////
  // Internal Wires
  ////////////////////
  wire [31:0] add_op_w;
  wire [31:0] pc_plus_offset_w;
  wire [31:0] next_pc_plus_offset_w;

  ////////////////////
  // Combinational Logic
  ////////////////////

  // Calculate the offset to add to the pc
  assign add_op_w = add_imm_i ? imm_i : 32'h4;

  // Calculate offsets for both pc and next_pc
  assign pc_plus_offset_w = pc_reg + add_op_w;
  assign next_pc_plus_offset_w = next_pc_r + 32'h4;

  ////////////////////
  // Sequential Logic
  ////////////////////
  always @(posedge clk_i) begin
    if (!rst_ni) begin
      next_pc_r <= 32'h80000000;
      pc_reg <= 32'h80000000;
    end else if (en_i) begin
      pc_reg <= next_pc_r;
      if (sel_mtvec_i) begin
        next_pc_r <= {mtvec_i[31:2], 2'b00};
      end else if (sel_mepc_i) begin
        next_pc_r <= mepc_i;
      end else if (sel_alu_i) begin
        // Use when jalr where the result of the ALU is the next_pc
        next_pc_r <= {alu_i[31:2], 2'b00};
      end else if (sel_pc_base_i) begin
        // Use pc as base when branching/jumping
        next_pc_r <= {pc_plus_offset_w[31:2], 2'b00};
      end else begin
        // Normal increment of next_pc
        next_pc_r <= {next_pc_plus_offset_w[31:2], 2'b00};
      end
    end
  end

  // Output
  assign addr_o = next_pc_r;

endmodule
