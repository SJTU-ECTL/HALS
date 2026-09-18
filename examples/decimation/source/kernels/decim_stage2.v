module decim_stage2 (
    input signed [15:0] buf_0,
    input signed [15:0] buf_1,
    input signed [15:0] buf_2,
    input signed [15:0] buf_3,
    input signed [15:0] buf_4,
    input signed [15:0] buf_5,
    input signed [15:0] buf_6,
    output signed [15:0] out
);
  wire signed [16:0] s0 = buf_0 + buf_6;
  wire signed [35:0] p0 = $signed(s0) * 130;
  wire signed [16:0] s1 = buf_1 + buf_5;
  wire signed [35:0] p1 = $signed(s1) * 191;
  wire signed [16:0] s2 = buf_2 + buf_4;
  wire signed [35:0] p2 = $signed(s2) * 30;
  wire signed [35:0] p3 = $signed(buf_3) * -139;  // last tap: +0
  wire signed [36:0] sum_1 = p0 + p1;
  wire signed [36:0] sum_2 = sum_1 + p2;
  wire signed [36:0] sum_3 = sum_2 + p3;
  assign out = sum_3[23:8];
endmodule
