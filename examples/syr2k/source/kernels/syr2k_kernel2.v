module syr2k_kernel2 (
    input [15:0] beta,
    input [15:0] C_in,
    input [15:0] alpha,
    input [15:0] term2,
    output [15:0] C_next
);
  wire [31:0] prod_c = beta * C_in;
  wire [31:0] prod_t = alpha * term2;
  wire [31:0] sum = (prod_c >> 8) + (prod_t >> 8);
  assign C_next = sum[15:0];
endmodule
