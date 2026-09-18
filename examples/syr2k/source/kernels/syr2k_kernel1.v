module syr2k_kernel1 (
    input [15:0] A_jk,
    input [15:0] B_ik,
    input [15:0] B_jk,
    input [15:0] A_ik,
    output [15:0] term2
);
  wire [31:0] prod0 = A_jk * B_ik;
  wire [31:0] prod1 = B_jk * A_ik;
  wire [31:0] sum = (prod0 >> 8) + (prod1 >> 8);
  assign term2 = sum[15:0];
endmodule
