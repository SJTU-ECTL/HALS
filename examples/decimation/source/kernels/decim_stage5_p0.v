module decim_stage5_p0 (
    input signed [15:0] buf_0,
    input signed [15:0] buf_1,
    input signed [15:0] buf_2,
    input signed [15:0] buf_3,
    input signed [15:0] buf_20,
    input signed [15:0] buf_21,
    input signed [15:0] buf_22,
    input signed [15:0] buf_23,
    output signed [36:0] partial0
);
  wire signed [16:0] s0 = buf_0 + buf_23;
  wire signed [35:0] p0 = $signed(s0) * 79;
  wire signed [16:0] s1 = buf_1 + buf_22;
  wire signed [35:0] p1 = $signed(s1) * 28;
  wire signed [16:0] s2 = buf_2 + buf_21;
  wire signed [35:0] p2 = $signed(s2) * -144;
  wire signed [16:0] s3 = buf_3 + buf_20;
  wire signed [35:0] p3 = $signed(s3) * 37;
  assign partial0 = p0 + p1 + p2 + p3;
endmodule
