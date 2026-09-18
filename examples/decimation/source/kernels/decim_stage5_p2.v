module decim_stage5_p2 (
    input signed [15:0] buf_8,
    input signed [15:0] buf_9,
    input signed [15:0] buf_10,
    input signed [15:0] buf_11,
    input signed [15:0] buf_13,
    input signed [15:0] buf_14,
    input signed [15:0] buf_15,
    output signed [36:0] partial2
);
  wire signed [16:0] s8 = buf_8 + buf_15;
  wire signed [35:0] p8 = $signed(s8) * 79;
  wire signed [16:0] s9 = buf_9 + buf_14;
  wire signed [35:0] p9 = $signed(s9) * 28;
  wire signed [16:0] s10 = buf_10 + buf_13;
  wire signed [35:0] p10 = $signed(s10) * -144;
  wire signed [35:0] p11 = $signed(buf_11) * 37;
  assign partial2 = p8 + p9 + p10 + p11;
endmodule
