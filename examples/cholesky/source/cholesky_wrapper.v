module cholesky_wrapper (
    input clk,
    input rst,
    input start,
    input  [15:0] a00,
    input  [15:0] a10,
    input  [15:0] a20,
    input  [15:0] a11,
    input  [15:0] a21,
    input  [15:0] a22,
    output reg valid,
    output [15:0] l00,
    output [15:0] l10,
    output [15:0] l20,
    output [15:0] l03,
    output [15:0] l11,
    output [15:0] l21,
    output [15:0] l06,
    output [15:0] l07,
    output [15:0] l22
);
  localparam S_IDLE = 2'd0;
  localparam S_K0 = 2'd1;
  localparam S_K1 = 2'd2;
  localparam S_K2 = 2'd3;

  reg [1:0] state;
  reg [15:0] a00_r, a10_r, a20_r, a11_r, a21_r, a22_r;
  reg [15:0] l00_r, l10_r, l20_r, l11_r, l21_r, l22_r;
  reg [31:0] diff2_r;
  reg sqrt_en;
  reg [15:0] sqrt_x;
  wire [15:0] sqrt_y;
  wire sqrt_valid;

  wire [15:0] k0_l10;
  wire [15:0] k0_l20;
  wire [15:0] k1_diff1;
  wire [31:0] k1_diff2;
  wire [15:0] k1_l21;
  wire [15:0] k2_diff;

  cholesky_sqrt_shared sqrt_unit (
    .clk(clk), .rst(rst), .en(sqrt_en), .x(sqrt_x), .y(sqrt_y), .valid(sqrt_valid)
  );

  cholesky_k0_l10 k0_l10_inst (.a10(a10_r), .l00(sqrt_y), .l10(k0_l10));
  cholesky_k0_l20 k0_l20_inst (.a20(a20_r), .l00(sqrt_y), .l20(k0_l20));
  cholesky_k1_residual k1_residual_inst (
    .a11(a11_r), .a21(a21_r), .l10(k0_l10), .l20(k0_l20),
    .diff1(k1_diff1), .diff2(k1_diff2)
  );
  cholesky_k1_l21 k1_l21_inst (.diff2(diff2_r), .l11(sqrt_y), .l21(k1_l21));
  cholesky_k2_residual k2_residual_inst (
    .a22(a22_r), .l20(l20_r), .l21(k1_l21), .diff(k2_diff)
  );

  always @(posedge clk or posedge rst) begin
    if (rst) begin
      state <= S_IDLE;
      valid <= 1'b0;
      sqrt_en <= 1'b0;
      sqrt_x <= 16'd0;
      a00_r <= 16'd0; a10_r <= 16'd0; a20_r <= 16'd0;
      a11_r <= 16'd0; a21_r <= 16'd0; a22_r <= 16'd0;
      l00_r <= 16'd0; l10_r <= 16'd0; l20_r <= 16'd0;
      l11_r <= 16'd0; l21_r <= 16'd0; l22_r <= 16'd0;
      diff2_r <= 32'd0;
    end else begin
      valid <= 1'b0;
      sqrt_en <= 1'b0;
      case (state)
        S_IDLE: begin
          if (start) begin
            a00_r <= a00; a10_r <= a10; a20_r <= a20;
            a11_r <= a11; a21_r <= a21; a22_r <= a22;
            sqrt_x <= a00;
            sqrt_en <= 1'b1;
            state <= S_K0;
          end
        end
        S_K0: begin
          if (sqrt_valid) begin
            l00_r <= sqrt_y;
            l10_r <= k0_l10;
            l20_r <= k0_l20;
            diff2_r <= k1_diff2;
            sqrt_x <= k1_diff1;
            sqrt_en <= 1'b1;
            state <= S_K1;
          end
        end
        S_K1: begin
          if (sqrt_valid) begin
            l11_r <= sqrt_y;
            l21_r <= k1_l21;
            sqrt_x <= k2_diff;
            sqrt_en <= 1'b1;
            state <= S_K2;
          end
        end
        S_K2: begin
          if (sqrt_valid) begin
            l22_r <= sqrt_y;
            valid <= 1'b1;
            state <= S_IDLE;
          end
        end
      endcase
    end
  end

  assign l00 = l00_r;
  assign l10 = l10_r;
  assign l20 = l20_r;
  assign l03 = 16'd0;
  assign l11 = l11_r;
  assign l21 = l21_r;
  assign l06 = 16'd0;
  assign l07 = 16'd0;
  assign l22 = l22_r;
endmodule
