module buttons (
    input  wire        clk_i,
    input  wire        rst_ni,
    input  wire        en_i,
    input  wire        we_i,
    input  wire [31:0] addr_i,
    input  wire [31:0] wdata_i,
    input  wire [ 9:0] push_i,
    input  wire [ 7:0] switch_i,
    output wire        irq_o,
    output wire [31:0] rdata_o
);

    localparam VAL_ADDR = 32'h70000000;
    localparam SRC_ADDR = 32'h70000004;
    localparam PTM_ADDR = 32'h70000008;
    localparam STM_ADDR = 32'h7000000C;

    localparam [1:0] ACTIVE_LOW   = 2'b00;
    localparam [1:0] RISING_EDGE  = 2'b01;
    localparam [1:0] FALLING_EDGE = 2'b10;
    localparam [1:0] ACTIVE_HIGH  = 2'b11;

    reg [31:0] prev_val_r;
    reg [31:0] src_r;
    reg [19:0] ptm_r;
    reg [15:0] stm_r;
    reg [31:0] rdata_q;

    reg [31:0] clear_block;

    wire [31:0] curr_val;
    assign curr_val[9:0]   = push_i;
    assign curr_val[15:10] = 6'b0;
    assign curr_val[23:16] = switch_i;
    assign curr_val[31:24] = 8'b0;

    reg [1:0] mode;
    reg low_act, high_act, rise_act, fall_act;
    reg curr, prev;

    integer i;
    always @(*) begin
        clear_block = 32'h0;
        if (en_i && we_i && addr_i == SRC_ADDR && wdata_i == 32'h0) begin
            clear_block = 32'hFFFF_FFFF;
        end
    end

    always @(posedge clk_i) begin
        if (!rst_ni) begin
            prev_val_r <= 32'b0;
            src_r <= 32'b0;
        end else begin
            prev_val_r <= curr_val;
            if (clear_block == 32'hFFFFFFFF) begin
                src_r <= 32'b0;
            end else begin
                for (i = 0; i < 10; i = i + 1) begin
                    mode     = ptm_r[2*i +: 2];
                    low_act  = (mode == ACTIVE_LOW);
                    high_act = (mode == ACTIVE_HIGH);
                    rise_act = (mode == RISING_EDGE);
                    fall_act = (mode == FALLING_EDGE);

                    curr = curr_val[i];
                    prev = prev_val_r[i];

                    if (low_act && !curr)
                        src_r[i] <= 1'b1;
                    else if (high_act && curr)
                        src_r[i] <= 1'b1;
                    else if (rise_act && !prev && curr)
                        src_r[i] <= 1'b1;
                    else if (fall_act && prev && !curr)
                        src_r[i] <= 1'b1;
                end

                for (i = 0; i < 8; i = i + 1) begin
                    mode     = stm_r[2*i +: 2];
                    low_act  = (mode == ACTIVE_LOW);
                    high_act = (mode == ACTIVE_HIGH);
                    rise_act = (mode == RISING_EDGE);
                    fall_act = (mode == FALLING_EDGE);

                    curr = curr_val[i+16];
                    prev = prev_val_r[i+16];

                    if (low_act && !curr)
                        src_r[i+16] <= 1'b1;
                    else if (high_act && curr)
                        src_r[i+16] <= 1'b1;
                    else if (rise_act && !prev && curr)
                        src_r[i+16] <= 1'b1;
                    else if (fall_act && prev && !curr)
                        src_r[i+16] <= 1'b1;
                end
            end
        end
    end

    always @(posedge clk_i) begin
        if (!rst_ni) begin
            ptm_r      <= 20'hFFFFF;
            stm_r      <= 16'hFFFF;
            rdata_q    <= 32'b0;
        end else begin
            if (en_i && we_i) begin
                if (addr_i == PTM_ADDR) ptm_r <= wdata_i[19:0];
                if (addr_i == STM_ADDR) stm_r <= wdata_i[15:0];
            end

            if (en_i && !we_i) begin
                case (addr_i)
                    VAL_ADDR: rdata_q <= curr_val;
                    SRC_ADDR: rdata_q <= src_r;
                    PTM_ADDR: rdata_q <= {12'b0, ptm_r};
                    STM_ADDR: rdata_q <= {16'b0, stm_r};
                    default:  rdata_q <= 32'b0;
                endcase
            end
        end
    end

    assign irq_o   = (src_r != 0);
    assign rdata_o = rdata_q;

endmodule
