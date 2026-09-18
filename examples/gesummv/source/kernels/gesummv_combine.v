module gesummv_combine (
    input [15:0] alpha,
    input [15:0] beta,
    input [15:0] tmp_final,
    input [15:0] y_final,
    output [15:0] out
);
  wire [31:0] prod_tmp = alpha * tmp_final;
  wire [31:0] prod_y = beta * y_final;
  wire [31:0] sum = (prod_tmp >> 8) + (prod_y >> 8);
  assign out = sum[15:0];
endmodule
