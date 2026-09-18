module syr2k_wrapper (
    input [15:0] C_in,
    input [15:0] A_jk,
    input [15:0] B_ik,
    input [15:0] B_jk,
    input [15:0] A_ik,
    input [15:0] alpha,
    input [15:0] beta,
    output [15:0] C_next
);
  wire [15:0] term2;
  syr2k_kernel1 k1(.A_jk(A_jk), .B_ik(B_ik), .B_jk(B_jk), .A_ik(A_ik), .term2(term2));
  syr2k_kernel2 k2(.beta(beta), .C_in(C_in), .alpha(alpha), .term2(term2), .C_next(C_next));
endmodule
