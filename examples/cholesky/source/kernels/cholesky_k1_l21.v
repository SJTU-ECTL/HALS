module cholesky_k1_l21 (
    input [31:0] diff2,
    input [15:0] l11,
    output [15:0] l21
);
  wire [31:0] l11_ext = {16'd0, l11};
  wire [31:0] diff2_q8 = diff2 << 8;
  wire [31:0] l21_div = (l11 != 0) ? (diff2_q8 / l11_ext) : 32'd0;
  assign l21 = l21_div[15:0];
endmodule
