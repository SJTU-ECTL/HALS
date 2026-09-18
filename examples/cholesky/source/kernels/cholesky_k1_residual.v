module cholesky_k1_residual (
    input [15:0] a11,
    input [15:0] a21,
    input [15:0] l10,
    input [15:0] l20,
    output [15:0] diff1,
    output [31:0] diff2
);
  wire [31:0] l10_sq = (l10 * l10) >> 8;
  wire signed [32:0] diff1_signed = {1'b0, 16'd0, a11} - {1'b0, l10_sq};
  wire [31:0] prod = (l20 * l10) >> 8;
  wire signed [32:0] diff2_signed = {1'b0, 16'd0, a21} - {1'b0, prod};
  assign diff1 = (diff1_signed <= 0) ? 16'd0 : diff1_signed[15:0];
  assign diff2 = (diff2_signed <= 0) ? 32'd0 : diff2_signed[31:0];
endmodule
