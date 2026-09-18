module decim_stage5 (
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
    input signed [15:0] buf_11,
    input signed [15:0] buf_12,
    input signed [15:0] buf_13,
    input signed [15:0] buf_14,
    input signed [15:0] buf_15,
    input signed [15:0] buf_16,
    input signed [15:0] buf_17,
    input signed [15:0] buf_18,
    input signed [15:0] buf_19,
    input signed [15:0] buf_20,
    input signed [15:0] buf_21,
    input signed [15:0] buf_22,
    input signed [15:0] buf_23,
    output signed [15:0] out
);
  wire signed [36:0] partial0;
  wire signed [36:0] partial1;
  wire signed [36:0] partial2;
  wire signed [37:0] sum;

  decim_stage5_p0 s5_p0 (
    .buf_0(buf_0), .buf_1(buf_1), .buf_2(buf_2), .buf_3(buf_3),
    .buf_20(buf_20), .buf_21(buf_21), .buf_22(buf_22), .buf_23(buf_23),
    .partial0(partial0)
  );

  decim_stage5_p1 s5_p1 (
    .buf_4(buf_4), .buf_5(buf_5), .buf_6(buf_6), .buf_7(buf_7),
    .buf_16(buf_16), .buf_17(buf_17), .buf_18(buf_18), .buf_19(buf_19),
    .partial1(partial1)
  );

  decim_stage5_p2 s5_p2 (
    .buf_8(buf_8), .buf_9(buf_9), .buf_10(buf_10), .buf_11(buf_11),
    .buf_13(buf_13), .buf_14(buf_14), .buf_15(buf_15),
    .partial2(partial2)
  );

  assign sum = partial0 + partial1 + partial2;
  assign out = sum[23:8];
endmodule
