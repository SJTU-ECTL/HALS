module decim_stage4 (
    input signed [15:0] buf_0,
    input signed [15:0] buf_1,
    input signed [15:0] buf_2,
    input signed [15:0] buf_3,
    input signed [15:0] buf_4,
    input signed [15:0] buf_5,
    input signed [15:0] buf_6,
    input signed [15:0] buf_7,
    input signed [15:0] buf_8,
    input signed [15:0] buf_9,
    input signed [15:0] buf_10,
    output signed [15:0] out
);
  wire signed [16:0] s0 = buf_0 + buf_10;
  wire signed [35:0] p0 = $signed(s0) * -65;
  wire signed [16:0] s1 = buf_1 + buf_9;
  wire signed [35:0] p1 = $signed(s1) * 42;
  wire signed [16:0] s2 = buf_2 + buf_8;
  wire signed [35:0] p2 = $signed(s2) * 26;
  wire signed [16:0] s3 = buf_3 + buf_7;
  wire signed [35:0] p3 = $signed(s3) * 95;
  wire signed [16:0] s4 = buf_4 + buf_6;
  wire signed [35:0] p4 = $signed(s4) * -38;
  wire signed [35:0] p5 = $signed(buf_5) * -245;  // last tap: +0
  wire signed [36:0] sum_1 = p0 + p1;
  wire signed [36:0] sum_2 = sum_1 + p2;
  wire signed [36:0] sum_3 = sum_2 + p3;
  wire signed [36:0] sum_4 = sum_3 + p4;
  wire signed [36:0] sum_5 = sum_4 + p5;
  assign out = sum_5[23:8];
endmodule
