module decim_stage5_p1 (
    input signed [15:0] buf_4,
    input signed [15:0] buf_5,
    input signed [15:0] buf_6,
    input signed [15:0] buf_7,
    input signed [15:0] buf_16,
    input signed [15:0] buf_17,
    input signed [15:0] buf_18,
    input signed [15:0] buf_19,
    output signed [36:0] partial1
);
  wire signed [16:0] s4 = buf_4 + buf_19;
  wire signed [35:0] p4 = $signed(s4) * 78;
  wire signed [16:0] s5 = buf_5 + buf_18;
  wire signed [35:0] p5 = $signed(s5) * 248;
  wire signed [16:0] s6 = buf_6 + buf_17;
  wire signed [35:0] p6 = $signed(s6) * -33;
  wire signed [16:0] s7 = buf_7 + buf_16;
  wire signed [35:0] p7 = $signed(s7) * 71;
  assign partial1 = p4 + p5 + p6 + p7;
endmodule
