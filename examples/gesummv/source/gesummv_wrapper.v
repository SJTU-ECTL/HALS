module gesummv_wrapper (
    input clk,
    input rst,
    input [15:0] Aij,
    input [15:0] Bij,
    input [15:0] xj,
    input [15:0] alpha,
    input [15:0] beta,
    output reg valid_out,
    output reg [15:0] out
);
  localparam DEPTH = 15;
  reg [3:0] index;
  reg [15:0] tmp_reg [0:DEPTH-1];
  reg [15:0] y_reg [0:DEPTH-1];
  wire [15:0] tmp_acc = (index == 0) ? 16'd0 : tmp_reg[index - 1];
  wire [15:0] y_acc = (index == 0) ? 16'd0 : y_reg[index - 1];
  wire [15:0] tmp_next;
  wire [15:0] y_next;
  wire [15:0] combined;
  integer i;

  gesummv_acc acc_inst(.tmp_acc(tmp_acc), .y_acc(y_acc), .Aij(Aij), .Bij(Bij), .xj(xj),
                       .tmp_next(tmp_next), .y_next(y_next));
  gesummv_combine combine_inst(.alpha(alpha), .beta(beta), .tmp_final(tmp_next), .y_final(y_next), .out(combined));

  initial begin
    index = 0;
    valid_out = 0;
    out = 0;
    for (i = 0; i < DEPTH; i = i + 1) begin
      tmp_reg[i] = 0;
      y_reg[i] = 0;
    end
  end

  always @(posedge clk) begin
    if (rst) begin
      index <= 0;
      valid_out <= 0;
      out <= 0;
      for (i = 0; i < DEPTH; i = i + 1) begin
        tmp_reg[i] <= 0;
        y_reg[i] <= 0;
      end
    end else begin
      tmp_reg[index] <= tmp_next;
      y_reg[index] <= y_next;
      valid_out <= (index == DEPTH - 1);
      if (index == DEPTH - 1) begin
        out <= combined;
        index <= 0;
      end else begin
        index <= index + 1;
      end
    end
  end
endmodule
