module bicg_s_acc (
    input  wire [15:0] s_acc,
    input  wire [15:0] Aij,
    input  wire [15:0] ri,
    output wire [15:0] s_next
);
  wire [31:0] product = Aij * ri;
  wire [16:0] sum = {1'b0, s_acc} + {1'b0, product[23:8]};
  assign s_next = sum[15:0];
endmodule
