module interp_k2_p0 (
  input signed [15:0] idata0,
  input signed [15:0] idata1,
  input signed [15:0] idata2,
  input signed [15:0] idata3,
  output reg signed [34:0] partial0
);
parameter signed [15:0] C0 = 16'hFF98;
parameter signed [15:0] C1 = 16'h037A;
parameter signed [15:0] C2 = 16'hEFF8;
parameter signed [15:0] C3 = 16'h4CF3;

reg signed [34:0] p0;
reg signed [34:0] p1;
reg signed [34:0] p2;
reg signed [34:0] p3;
reg signed [34:0] s0;
reg signed [34:0] s1;

always @(*) begin
  p0 = idata0 * C0;
  p1 = idata1 * C1;
  p2 = idata2 * C2;
  p3 = idata3 * C3;
  s0 = p0 + p1;
  s1 = p2 + p3;
  partial0 = s0 + s1;
end
endmodule
