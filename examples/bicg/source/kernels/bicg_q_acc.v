module bicg_q_acc (
    input  wire [15:0] q_acc,
    input  wire [15:0] Aij,
    input  wire [15:0] pj,
    output wire [15:0] q_next
);
  wire [31:0] product = Aij * pj;
  wire [16:0] sum = {1'b0, q_acc} + {1'b0, product[23:8]};
  assign q_next = sum[15:0];
endmodule
