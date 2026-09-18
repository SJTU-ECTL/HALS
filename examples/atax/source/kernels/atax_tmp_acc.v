module atax_tmp_acc (
    input  wire [15:0] tmp_acc,
    input  wire [15:0] Aij,
    input  wire [15:0] xj,
    output wire [15:0] tmp_next
);
  wire [31:0] product = Aij * xj;
  wire [16:0] sum = {1'b0, tmp_acc} + {1'b0, product[23:8]};
  assign tmp_next = sum[15:0];
endmodule
