module csr (
    input  wire        clk_i,
    input  wire        rst_ni,
    input  wire [11:0] addr_i,
    input  wire [31:0] wdata_i,
    input  wire        irq_i,
    input  wire [31:0] pc_i,
    input  wire        write_i,
    input  wire        set_i,
    input  wire        clear_i,
    input  wire        interrupt_i,
    input  wire        mret_i,
    output reg  [31:0] rdata_o,
    output wire [31:0] mtvec_o,
    output wire [31:0] mepc_o,
    output wire        ipending_o
);

    reg [31:0] mstatus_r, mie_r, mtvec_r, mepc_r, mcause_r, mip_r;

    localparam MSTATUS = 12'h300;
    localparam MIE     = 12'h304;
    localparam MTVEC   = 12'h305;
    localparam MEPC    = 12'h341;
    localparam MCAUSE  = 12'h342;
    localparam MIP     = 12'h344;

    assign mtvec_o = mtvec_r;
    assign mepc_o  = mepc_r;
    assign ipending_o = (mip_r != 32'b0) && (mie_r != 32'b0) && (mstatus_r[3] != 0);

    always @(*) begin
        case (addr_i)
            MSTATUS: rdata_o = mstatus_r;
            MIE    : rdata_o = mie_r;
            MTVEC  : rdata_o = mtvec_r;
            MEPC   : rdata_o = mepc_r;
            MCAUSE : rdata_o = mcause_r;
            MIP    : rdata_o = mip_r;
            default: rdata_o = 32'b0;
        endcase
    end

    always @(posedge clk_i) begin
        if (!rst_ni) begin
            mstatus_r <= 32'h00001800;
            mie_r     <= 32'b0;
            mtvec_r   <= 32'b0;
            mepc_r    <= 32'b0;
            mcause_r  <= 32'b0;
            mip_r     <= 32'b0;
        end else begin

            mip_r[11] <= irq_i;

            if (write_i) begin
                case (addr_i)
                    MSTATUS: mstatus_r <= wdata_i & 32'h1888;
                    MIE    : mie_r     <= wdata_i & 32'h0800;
                    MTVEC  : mtvec_r   <= wdata_i;
                    MEPC   : mepc_r    <= wdata_i;
                    MCAUSE : mcause_r  <= wdata_i & 32'h80000800;
                    MIP    : mip_r     <= wdata_i & 32'h0800;
                    default: ;
                endcase
            end else if (set_i) begin
                case (addr_i)
                    MSTATUS: mstatus_r <= mstatus_r | (wdata_i & 32'h1888);
                    MIE    : mie_r     <= mie_r     | (wdata_i & 32'h0800);
                    MTVEC  : mtvec_r   <= mtvec_r   | wdata_i;
                    MEPC   : mepc_r    <= mepc_r    | wdata_i;
                    MCAUSE : mcause_r  <= mcause_r  | (wdata_i & 32'h80000800);
                    MIP    : mip_r     <= mip_r     | (wdata_i & 32'h0800);
                    default: ;
                endcase
            end else if (clear_i) begin
                case (addr_i)
                    MSTATUS: mstatus_r <= mstatus_r & ~(wdata_i & 32'h1888);
                    MIE    : mie_r     <= mie_r     & ~(wdata_i & 32'h0800);
                    MTVEC  : mtvec_r   <= mtvec_r   & ~wdata_i;
                    MEPC   : mepc_r    <= mepc_r    & ~wdata_i;
                    MCAUSE : mcause_r  <= mcause_r  & ~(wdata_i & 32'h80000800);
                    MIP    : mip_r     <= mip_r     & ~(wdata_i & 32'h0800);
                    default: ;
                endcase
            end

            if (interrupt_i) begin
                mepc_r <= pc_i;
                mcause_r <= 32'h80000800;
                mstatus_r[7] <= mstatus_r[3];
                mstatus_r[3] <= 1'b0;
                mip_r[11] <= 1'b0;
            end

            if (mret_i) begin
                mstatus_r[3] <= mstatus_r[7];
                mstatus_r[7] <= 1'b1;
            end

        end
    end
endmodule
