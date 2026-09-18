module cholesky_k2_residual (
    input [15:0] a22,
    input [15:0] l20,
    input [15:0] l21,
    output [15:0] diff
);
  wire [31:0] l20_sq = (l20 * l20) >> 8;
  wire [31:0] l21_sq = (l21 * l21) >> 8;
  wire signed [32:0] diff_signed = {1'b0, 16'd0, a22} - {1'b0, l20_sq} - {1'b0, l21_sq};
  assign diff = (diff_signed <= 0) ? 16'd0 : diff_signed[15:0];
endmodule
