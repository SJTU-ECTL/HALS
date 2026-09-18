module cholesky_sqrt_shared (
    input clk,
    input rst,
    input en,
    input [15:0] x,
    output reg [15:0] y,
    output reg valid
);
  `include "cholesky_sqrt_func.vh"

  always @(posedge clk or posedge rst) begin
    if (rst) begin
      y <= 16'd0;
      valid <= 1'b0;
    end else begin
      valid <= en;
      if (en) begin
        y <= c_sqrt(x);
      end
    end
  end
endmodule
