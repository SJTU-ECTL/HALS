module interp_k1_p1 (
  input signed [15:0] idata4,
  input signed [15:0] idata5,
  input signed [15:0] idata6,
  input signed [15:0] idata7,
  output reg signed [34:0] partial1
);
parameter signed [15:0] C4 = 16'h6E60;
parameter signed [15:0] C5 = 16'hF236;
parameter signed [15:0] C6 = 16'h02C5;
parameter signed [15:0] C7 = 16'hFFB0;

reg signed [34:0] p4;
reg signed [34:0] p5;
reg signed [34:0] p6;
reg signed [34:0] p7;
reg signed [34:0] s0;
reg signed [34:0] s1;

always @(*) begin
  p4 = idata4 * C4;
  p5 = idata5 * C5;
  p6 = idata6 * C6;
  p7 = idata7 * C7;
  s0 = p4 + p5;
  s1 = p6 + p7;
  partial1 = s0 + s1;
end
endmodule
