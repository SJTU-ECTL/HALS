module cholesky_k0_l10 (
    input [15:0] a10,
    input [15:0] l00,
    output [15:0] l10
);
  wire [31:0] a10_ext = {16'd0, a10};
  wire [31:0] l00_ext = {16'd0, l00};
  wire [31:0] a10_q8 = a10_ext << 8;
  wire [31:0] l10_div = (l00 != 0) ? (a10_q8 / l00_ext) : 32'd0;
  assign l10 = l10_div[15:0];
endmodule
