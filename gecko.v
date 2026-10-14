module tb (
    input  wire         clk_i,
    input  wire         rst_ni,
    input  wire         button_top_i,
    input  wire         button_bottom_i,
    input  wire         button_left_i,
    input  wire         button_right_i,
    input  wire         button_center_i,
    input  wire         joystick_up_i,
    input  wire         joystick_down_i,
    input  wire         joystick_left_i,
    input  wire         joystick_right_i,
    input  wire         joystick_pressed_i,
    input  wire [  7:0] dip_switches_i,
    output wire [119:0] led_r_o,
    output wire [119:0] led_g_o,
    output wire [119:0] led_b_o,
    output reg  [  7:0] sevensegment_1_o,
    output reg  [  7:0] sevensegment_2_o,
    output reg  [  7:0] sevensegment_3_o,
    output reg  [  7:0] sevensegment_4_o
);
  // Internal signals
  wire [31:0] waddr_w;
  wire [31:0] wdata_w;
  wire        we_w;
  wire [31:0] rdata_w;
  wire en_ram_w, en_leds_w, en_7_seg_lcd_w, en_buttons_w;
  wire [31:0] mem_rdata_w;
  wire [119:0] leds_r_w, leds_g_w, leds_b_w;
  wire [31:0] seg_disp_w;
  wire [31:0] buttons_rdata_w;
  wire [31:0] mux_out_w;
  wire        irq_w;

  // Reconstruct push[9:0] from individual button inputs
  wire [ 9:0] push_w;
  assign push_w = {
    button_top_i,
    button_bottom_i,
    button_left_i,
    button_right_i,
    button_center_i,
    joystick_up_i,
    joystick_down_i,
    joystick_left_i,
    joystick_right_i,
    joystick_pressed_i
  };

  ///////////////////
  // CPU Instance
  ///////////////////
  cpu u_cpu (
      .clk_i  (clk_i),
      .rst_ni (rst_ni),
      .rdata_i(rdata_w),
      .addr_o (waddr_w),
      .wdata_o(wdata_w),
      .we_o   (we_w),
      .irq_i  (irq_w)
  );

  //////////////////////
  // Address Decoder
  //////////////////////
  decoder decoder_inst (
      .addr_i        (waddr_w),
      .en_ram_o      (en_ram_w),
      .en_leds_o     (en_leds_w),
      .en_7_seg_lcd_o(en_7_seg_lcd_w),
      .en_buttons_o  (en_buttons_w)
  );

  ///////////////////
  // Memory Instance
  ///////////////////
  mem #(
      .B(32'hA0000000)
  ) u_mem (
      .clk_i  (clk_i),
      .en_i   (en_ram_w),
      .we_i   (we_w),
      .addr_i (waddr_w),
      .wdata_i(wdata_w),
      .rdata_o(mem_rdata_w)
  );

  /////////////////////////
  // LED Controller Instance
  /////////////////////////
  leds leds_inst (
      .clk_i  (clk_i),
      .rst_ni (rst_ni),
      .en_i   (en_leds_w),
      .we_i   (we_w),
      .waddr_i(waddr_w),
      .wdata_i(wdata_w),
      .led_r_o(leds_r_w),
      .led_g_o(leds_g_w),
      .led_b_o(leds_b_w)
  );

  /////////////////////////////////
  // 7-Segment LCD Controller Instance
  /////////////////////////////////
  seven_seg_lcd seven_seg_lcd_inst (
      .clk_i  (clk_i),
      .rst_ni (rst_ni),
      .en_i   (en_7_seg_lcd_w),
      .we_i   (we_w),
      .waddr_i(waddr_w),
      .wdata_i(wdata_w),
      .disp_o (seg_disp_w)
  );

  //////////////////////
  // Buttons Instance
  //////////////////////
  buttons buttons_inst (
      .clk_i   (clk_i),
      .rst_ni  (rst_ni),
      .en_i    (en_buttons_w),
      .we_i    (we_w),
      .addr_i  (waddr_w),
      .wdata_i (wdata_w),
      .push_i  (push_w),
      .switch_i(dip_switches_i),
      .rdata_o (buttons_rdata_w),
      .irq_o   (irq_w)
  );

  /////////////////
  // Mux Instance
  /////////////////
  mux2x32 mux_inst (
      .a_i  (mem_rdata_w),
      .b_i  (buttons_rdata_w),
      .sel_i(en_buttons_w),
      .o_o  (mux_out_w)
  );

  /////////////////////////
  // Output Assignments
  /////////////////////////
  assign rdata_w = mux_out_w;
  assign led_r_o = leds_r_w;
  assign led_g_o = leds_g_w;
  assign led_b_o = leds_b_w;

  // Split the 32-bit seg_disp_w into four 8-bit seven-segment displays
  always @(*) begin
    sevensegment_1_o = seg_disp_w[7:0];
    sevensegment_2_o = seg_disp_w[15:8];
    sevensegment_3_o = seg_disp_w[23:16];
    sevensegment_4_o = seg_disp_w[31:24];
  end

endmodule
