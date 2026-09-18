module gesummv_acc (
    input [15:0] tmp_acc,
    input [15:0] y_acc,
    input [15:0] Aij,
    input [15:0] Bij,
    input [15:0] xj,
    output [15:0] tmp_next,
    output [15:0] y_next
);
  wire [31:0] prod_a = Aij * xj;
  wire [31:0] prod_b = Bij * xj;
  wire [31:0] tmp_sum = {16'd0, tmp_acc} + (prod_a >> 8);
  wire [31:0] y_sum = {16'd0, y_acc} + (prod_b >> 8);
  assign tmp_next = tmp_sum[15:0];
  assign y_next = y_sum[15:0];
endmodule
