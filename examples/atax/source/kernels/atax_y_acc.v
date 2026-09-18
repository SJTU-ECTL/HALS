module atax_y_acc (
    input  wire [15:0] y_acc,
    input  wire [15:0] Aij,
    input  wire [15:0] tmp_final,
    output wire [15:0] y_next
);
  wire [31:0] product = Aij * tmp_final;
  wire [16:0] sum = {1'b0, y_acc} + {1'b0, product[23:8]};
  assign y_next = sum[15:0];
endmodule
