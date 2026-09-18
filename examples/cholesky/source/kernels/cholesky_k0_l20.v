module cholesky_k0_l20 (
    input [15:0] a20,
    input [15:0] l00,
    output [15:0] l20
);
  wire [31:0] a20_ext = {16'd0, a20};
  wire [31:0] l00_ext = {16'd0, l00};
  wire [31:0] a20_q8 = a20_ext << 8;
  wire [31:0] l20_div = (l00 != 0) ? (a20_q8 / l00_ext) : 32'd0;
  assign l20 = l20_div[15:0];
endmodule
