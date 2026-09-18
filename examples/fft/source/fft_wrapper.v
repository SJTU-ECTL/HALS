`ifndef FFT_S1_2_1_EXTERNAL
`include "kernels/fft_s1_2_1.v"
`endif
`include "kernels/fft_s1_2_2_1.v"
`include "kernels/fft_s1_2_2_2.v"
`include "kernels/fft_s1_2_2_3.v"
`include "kernels/fft_s1_2_2_4.v"
`include "kernels/fft_s1_2_3_1.v"
`include "kernels/fft_s1_2_3_2.v"
`include "kernels/fft_s1_2_3_3.v"
`include "kernels/fft_s1_2_3_4.v"
`include "kernels/fft_s1_2_4_1.v"
`include "kernels/fft_s1_2_4_2.v"
`include "kernels/fft_s1_2_4_3.v"
`include "kernels/fft_s1_2_4_4.v"
`include "kernels/fft_s3_4_1.v"
`include "kernels/fft_s3_4_2.v"
`include "kernels/fft_s3_4_3.v"
`include "kernels/fft_s3_4_4.v"

module fft_wrapper(
    input signed [15:0] sample_0_0,
    input signed [15:0] sample_0_1,
    input signed [15:0] sample_1_0,
    input signed [15:0] sample_1_1,
    input signed [15:0] sample_2_0,
    input signed [15:0] sample_2_1,
    input signed [15:0] sample_3_0,
    input signed [15:0] sample_3_1,
    input signed [15:0] sample_4_0,
    input signed [15:0] sample_4_1,
    input signed [15:0] sample_5_0,
    input signed [15:0] sample_5_1,
    input signed [15:0] sample_6_0,
    input signed [15:0] sample_6_1,
    input signed [15:0] sample_7_0,
    input signed [15:0] sample_7_1,
    input signed [15:0] sample_8_0,
    input signed [15:0] sample_8_1,
    input signed [15:0] sample_9_0,
    input signed [15:0] sample_9_1,
    input signed [15:0] sample_10_0,
    input signed [15:0] sample_10_1,
    input signed [15:0] sample_11_0,
    input signed [15:0] sample_11_1,
    input signed [15:0] sample_12_0,
    input signed [15:0] sample_12_1,
    input signed [15:0] sample_13_0,
    input signed [15:0] sample_13_1,
    input signed [15:0] sample_14_0,
    input signed [15:0] sample_14_1,
    input signed [15:0] sample_15_0,
    input signed [15:0] sample_15_1,
    output signed [15:0] sample_out_0_0,
    output signed [15:0] sample_out_0_1,
    output signed [15:0] sample_out_1_0,
    output signed [15:0] sample_out_1_1,
    output signed [15:0] sample_out_2_0,
    output signed [15:0] sample_out_2_1,
    output signed [15:0] sample_out_3_0,
    output signed [15:0] sample_out_3_1,
    output signed [15:0] sample_out_4_0,
    output signed [15:0] sample_out_4_1,
    output signed [15:0] sample_out_5_0,
    output signed [15:0] sample_out_5_1,
    output signed [15:0] sample_out_6_0,
    output signed [15:0] sample_out_6_1,
    output signed [15:0] sample_out_7_0,
    output signed [15:0] sample_out_7_1,
    output signed [15:0] sample_out_8_0,
    output signed [15:0] sample_out_8_1,
    output signed [15:0] sample_out_9_0,
    output signed [15:0] sample_out_9_1,
    output signed [15:0] sample_out_10_0,
    output signed [15:0] sample_out_10_1,
    output signed [15:0] sample_out_11_0,
    output signed [15:0] sample_out_11_1,
    output signed [15:0] sample_out_12_0,
    output signed [15:0] sample_out_12_1,
    output signed [15:0] sample_out_13_0,
    output signed [15:0] sample_out_13_1,
    output signed [15:0] sample_out_14_0,
    output signed [15:0] sample_out_14_1,
    output signed [15:0] sample_out_15_0,
    output signed [15:0] sample_out_15_1
);

    wire signed [15:0] sample_0_0_;
    wire signed [15:0] sample_0_1_;
    wire signed [15:0] sample_1_0_;
    wire signed [15:0] sample_1_1_;
    wire signed [15:0] sample_2_0_;
    wire signed [15:0] sample_2_1_;
    wire signed [15:0] sample_3_0_;
    wire signed [15:0] sample_3_1_;
    wire signed [15:0] sample_4_0_;
    wire signed [15:0] sample_4_1_;
    wire signed [15:0] sample_5_0_;
    wire signed [15:0] sample_5_1_;
    wire signed [15:0] sample_6_0_;
    wire signed [15:0] sample_6_1_;
    wire signed [15:0] sample_7_0_;
    wire signed [15:0] sample_7_1_;
    wire signed [15:0] sample_8_0_;
    wire signed [15:0] sample_8_1_;
    wire signed [15:0] sample_9_0_;
    wire signed [15:0] sample_9_1_;
    wire signed [15:0] sample_10_0_;
    wire signed [15:0] sample_10_1_;
    wire signed [15:0] sample_11_0_;
    wire signed [15:0] sample_11_1_;
    wire signed [15:0] sample_12_0_;
    wire signed [15:0] sample_12_1_;
    wire signed [15:0] sample_13_0_;
    wire signed [15:0] sample_13_1_;
    wire signed [15:0] sample_14_0_;
    wire signed [15:0] sample_14_1_;
    wire signed [15:0] sample_15_0_;
    wire signed [15:0] sample_15_1_;
    wire signed [15:0] sample_out_0_0_;
    wire signed [15:0] sample_out_0_1_;
    wire signed [15:0] sample_out_1_0_;
    wire signed [15:0] sample_out_1_1_;
    wire signed [15:0] sample_out_2_0_;
    wire signed [15:0] sample_out_2_1_;
    wire signed [15:0] sample_out_3_0_;
    wire signed [15:0] sample_out_3_1_;
    wire signed [15:0] sample_out_4_0_;
    wire signed [15:0] sample_out_4_1_;
    wire signed [15:0] sample_out_5_0_;
    wire signed [15:0] sample_out_5_1_;
    wire signed [15:0] sample_out_6_0_;
    wire signed [15:0] sample_out_6_1_;
    wire signed [15:0] sample_out_7_0_;
    wire signed [15:0] sample_out_7_1_;
    wire signed [15:0] sample_out_8_0_;
    wire signed [15:0] sample_out_8_1_;
    wire signed [15:0] sample_out_9_0_;
    wire signed [15:0] sample_out_9_1_;
    wire signed [15:0] sample_out_10_0_;
    wire signed [15:0] sample_out_10_1_;
    wire signed [15:0] sample_out_11_0_;
    wire signed [15:0] sample_out_11_1_;
    wire signed [15:0] sample_out_12_0_;
    wire signed [15:0] sample_out_12_1_;
    wire signed [15:0] sample_out_13_0_;
    wire signed [15:0] sample_out_13_1_;
    wire signed [15:0] sample_out_14_0_;
    wire signed [15:0] sample_out_14_1_;
    wire signed [15:0] sample_out_15_0_;
    wire signed [15:0] sample_out_15_1_;
assign sample_0_0_ = sample_0_0;
assign sample_0_1_ = sample_0_1;
assign sample_1_0_ = sample_1_0;
assign sample_1_1_ = sample_1_1;
assign sample_2_0_ = sample_2_0;
assign sample_2_1_ = sample_2_1;
assign sample_3_0_ = sample_3_0;
assign sample_3_1_ = sample_3_1;
assign sample_4_0_ = sample_4_0;
assign sample_4_1_ = sample_4_1;
assign sample_5_0_ = sample_5_0;
assign sample_5_1_ = sample_5_1;
assign sample_6_0_ = sample_6_0;
assign sample_6_1_ = sample_6_1;
assign sample_7_0_ = sample_7_0;
assign sample_7_1_ = sample_7_1;
assign sample_8_0_ = sample_8_0;
assign sample_8_1_ = sample_8_1;
assign sample_9_0_ = sample_9_0;
assign sample_9_1_ = sample_9_1;
assign sample_10_0_ = sample_10_0;
assign sample_10_1_ = sample_10_1;
assign sample_11_0_ = sample_11_0;
assign sample_11_1_ = sample_11_1;
assign sample_12_0_ = sample_12_0;
assign sample_12_1_ = sample_12_1;
assign sample_13_0_ = sample_13_0;
assign sample_13_1_ = sample_13_1;
assign sample_14_0_ = sample_14_0;
assign sample_14_1_ = sample_14_1;
assign sample_15_0_ = sample_15_0;
assign sample_15_1_ = sample_15_1;
assign sample_out_0_0 = sample_out[0][0];
assign sample_out_0_1 = sample_out[0][1];
assign sample_out_1_0 = sample_out[1][0];
assign sample_out_1_1 = sample_out[1][1];
assign sample_out_2_0 = sample_out[2][0];
assign sample_out_2_1 = sample_out[2][1];
assign sample_out_3_0 = sample_out[3][0];
assign sample_out_3_1 = sample_out[3][1];
assign sample_out_4_0 = sample_out[4][0];
assign sample_out_4_1 = sample_out[4][1];
assign sample_out_5_0 = sample_out[5][0];
assign sample_out_5_1 = sample_out[5][1];
assign sample_out_6_0 = sample_out[6][0];
assign sample_out_6_1 = sample_out[6][1];
assign sample_out_7_0 = sample_out[7][0];
assign sample_out_7_1 = sample_out[7][1];
assign sample_out_8_0 = sample_out[8][0];
assign sample_out_8_1 = sample_out[8][1];
assign sample_out_9_0 = sample_out[9][0];
assign sample_out_9_1 = sample_out[9][1];
assign sample_out_10_0 = sample_out[10][0];
assign sample_out_10_1 = sample_out[10][1];
assign sample_out_11_0 = sample_out[11][0];
assign sample_out_11_1 = sample_out[11][1];
assign sample_out_12_0 = sample_out[12][0];
assign sample_out_12_1 = sample_out[12][1];
assign sample_out_13_0 = sample_out[13][0];
assign sample_out_13_1 = sample_out[13][1];
assign sample_out_14_0 = sample_out[14][0];
assign sample_out_14_1 = sample_out[14][1];
assign sample_out_15_0 = sample_out[15][0];
assign sample_out_15_1 = sample_out[15][1];

    wire signed [15:0] stage12_pre_0_0_;
    wire signed [15:0] stage12_pre_0_1_;
    wire signed [15:0] stage12_pre_1_0_;
    wire signed [15:0] stage12_pre_1_1_;
    wire signed [15:0] stage12_pre_2_0_;
    wire signed [15:0] stage12_pre_2_1_;
    wire signed [15:0] stage12_pre_3_0_;
    wire signed [15:0] stage12_pre_3_1_;
    wire signed [15:0] stage12_pre_4_0_;
    wire signed [15:0] stage12_pre_4_1_;
    wire signed [15:0] stage12_pre_5_0_;
    wire signed [15:0] stage12_pre_5_1_;
    wire signed [15:0] stage12_pre_6_0_;
    wire signed [15:0] stage12_pre_6_1_;
    wire signed [15:0] stage12_pre_7_0_;
    wire signed [15:0] stage12_pre_7_1_;
    wire signed [15:0] stage12_pre_8_0_;
    wire signed [15:0] stage12_pre_8_1_;
    wire signed [15:0] stage12_pre_9_0_;
    wire signed [15:0] stage12_pre_9_1_;
    wire signed [15:0] stage12_pre_10_0_;
    wire signed [15:0] stage12_pre_10_1_;
    wire signed [15:0] stage12_pre_11_0_;
    wire signed [15:0] stage12_pre_11_1_;
    wire signed [15:0] stage12_pre_12_0_;
    wire signed [15:0] stage12_pre_12_1_;
    wire signed [15:0] stage12_pre_13_0_;
    wire signed [15:0] stage12_pre_13_1_;
    wire signed [15:0] stage12_pre_14_0_;
    wire signed [15:0] stage12_pre_14_1_;
    wire signed [15:0] stage12_pre_15_0_;
    wire signed [15:0] stage12_pre_15_1_;
    wire signed [15:0] stage12_0_0_;
    wire signed [15:0] stage12_0_1_;
    wire signed [15:0] stage12_1_0_;
    wire signed [15:0] stage12_1_1_;
    wire signed [15:0] stage12_2_0_;
    wire signed [15:0] stage12_2_1_;
    wire signed [15:0] stage12_3_0_;
    wire signed [15:0] stage12_3_1_;
    wire signed [15:0] stage12_4_0_;
    wire signed [15:0] stage12_4_1_;
    wire signed [15:0] stage12_5_0_;
    wire signed [15:0] stage12_5_1_;
    wire signed [15:0] stage12_6_0_;
    wire signed [15:0] stage12_6_1_;
    wire signed [15:0] stage12_7_0_;
    wire signed [15:0] stage12_7_1_;
    wire signed [15:0] stage12_8_0_;
    wire signed [15:0] stage12_8_1_;
    wire signed [15:0] stage12_9_0_;
    wire signed [15:0] stage12_9_1_;
    wire signed [15:0] stage12_10_0_;
    wire signed [15:0] stage12_10_1_;
    wire signed [15:0] stage12_11_0_;
    wire signed [15:0] stage12_11_1_;
    wire signed [15:0] stage12_12_0_;
    wire signed [15:0] stage12_12_1_;
    wire signed [15:0] stage12_13_0_;
    wire signed [15:0] stage12_13_1_;
    wire signed [15:0] stage12_14_0_;
    wire signed [15:0] stage12_14_1_;
    wire signed [15:0] stage12_15_0_;
    wire signed [15:0] stage12_15_1_;

    wire signed [15:0] stage12_pre [0:15][0:1];
    wire signed [15:0] stage12 [0:15][0:1];
    wire signed [15:0] sample_out [0:15][0:1];

fft_s1_2_1 u_fft_s1_2_1 (
  .sample_0_0(sample_0_0),   .sample_0_1(sample_0_1),
  .sample_4_0(sample_4_0),   .sample_4_1(sample_4_1),
  .sample_8_0(sample_8_0),   .sample_8_1(sample_8_1),
  .sample_12_0(sample_12_0), .sample_12_1(sample_12_1),
  .sample_out_0_0(stage12[0][0]),   .sample_out_0_1(stage12[0][1]),
  .sample_out_4_0(stage12[4][0]),   .sample_out_4_1(stage12[4][1]),
  .sample_out_8_0(stage12[8][0]),   .sample_out_8_1(stage12[8][1]),
  .sample_out_12_0(stage12[12][0]), .sample_out_12_1(stage12[12][1])
);

fft_s1_2_2_1 u_fft_s1_2_2_1 (
  .sample_1_0(sample_1_0), .sample_1_1(sample_1_1),
  .sample_9_0(sample_9_0), .sample_9_1(sample_9_1),
  .sample_out_1_0(stage12_pre[1][0]), .sample_out_1_1(stage12_pre[1][1]),
  .sample_out_9_0(stage12_pre[9][0]), .sample_out_9_1(stage12_pre[9][1])
);

fft_s1_2_2_2 u_fft_s1_2_2_2 (
  .sample_5_0(sample_5_0),   .sample_5_1(sample_5_1),
  .sample_13_0(sample_13_0), .sample_13_1(sample_13_1),
  .sample_out_5_0(stage12_pre[5][0]),   .sample_out_5_1(stage12_pre[5][1]),
  .sample_out_13_0(stage12_pre[13][0]), .sample_out_13_1(stage12_pre[13][1])
);

fft_s1_2_2_3 u_fft_s1_2_2_3 (
  .sample_1_0(stage12_pre[1][0]), .sample_1_1(stage12_pre[1][1]),
  .sample_5_0(stage12_pre[5][0]), .sample_5_1(stage12_pre[5][1]),
  .sample_out_1_0(stage12[1][0]), .sample_out_1_1(stage12[1][1]),
  .sample_out_5_0(stage12[5][0]), .sample_out_5_1(stage12[5][1])
);

fft_s1_2_2_4 u_fft_s1_2_2_4 (
  .sample_9_0(stage12_pre[9][0]),   .sample_9_1(stage12_pre[9][1]),
  .sample_13_0(stage12_pre[13][0]), .sample_13_1(stage12_pre[13][1]),
  .sample_out_9_0(stage12[9][0]),   .sample_out_9_1(stage12[9][1]),
  .sample_out_13_0(stage12[13][0]), .sample_out_13_1(stage12[13][1])
);

fft_s1_2_3_1 u_fft_s1_2_3_1 (
  .sample_2_0(sample_2_0),   .sample_2_1(sample_2_1),
  .sample_10_0(sample_10_0), .sample_10_1(sample_10_1),
  .sample_out_2_0(stage12_pre[2][0]),   .sample_out_2_1(stage12_pre[2][1]),
  .sample_out_10_0(stage12_pre[10][0]), .sample_out_10_1(stage12_pre[10][1])
);

fft_s1_2_3_2 u_fft_s1_2_3_2 (
  .sample_6_0(sample_6_0),   .sample_6_1(sample_6_1),
  .sample_14_0(sample_14_0), .sample_14_1(sample_14_1),
  .sample_out_6_0(stage12_pre[6][0]),   .sample_out_6_1(stage12_pre[6][1]),
  .sample_out_14_0(stage12_pre[14][0]), .sample_out_14_1(stage12_pre[14][1])
);

fft_s1_2_3_3 u_fft_s1_2_3_3 (
  .sample_2_0(stage12_pre[2][0]), .sample_2_1(stage12_pre[2][1]),
  .sample_6_0(stage12_pre[6][0]), .sample_6_1(stage12_pre[6][1]),
  .sample_out_2_0(stage12[2][0]), .sample_out_2_1(stage12[2][1]),
  .sample_out_6_0(stage12[6][0]), .sample_out_6_1(stage12[6][1])
);

fft_s1_2_3_4 u_fft_s1_2_3_4 (
  .sample_10_0(stage12_pre[10][0]), .sample_10_1(stage12_pre[10][1]),
  .sample_14_0(stage12_pre[14][0]), .sample_14_1(stage12_pre[14][1]),
  .sample_out_10_0(stage12[10][0]), .sample_out_10_1(stage12[10][1]),
  .sample_out_14_0(stage12[14][0]), .sample_out_14_1(stage12[14][1])
);

fft_s1_2_4_1 u_fft_s1_2_4_1 (
  .sample_3_0(sample_3_0),   .sample_3_1(sample_3_1),
  .sample_11_0(sample_11_0), .sample_11_1(sample_11_1),
  .sample_out_3_0(stage12_pre[3][0]),   .sample_out_3_1(stage12_pre[3][1]),
  .sample_out_11_0(stage12_pre[11][0]), .sample_out_11_1(stage12_pre[11][1])
);

fft_s1_2_4_2 u_fft_s1_2_4_2 (
  .sample_7_0(sample_7_0),   .sample_7_1(sample_7_1),
  .sample_15_0(sample_15_0), .sample_15_1(sample_15_1),
  .sample_out_7_0(stage12_pre[7][0]),   .sample_out_7_1(stage12_pre[7][1]),
  .sample_out_15_0(stage12_pre[15][0]), .sample_out_15_1(stage12_pre[15][1])
);

fft_s1_2_4_3 u_fft_s1_2_4_3 (
  .sample_3_0(stage12_pre[3][0]), .sample_3_1(stage12_pre[3][1]),
  .sample_7_0(stage12_pre[7][0]), .sample_7_1(stage12_pre[7][1]),
  .sample_out_3_0(stage12[3][0]), .sample_out_3_1(stage12[3][1]),
  .sample_out_7_0(stage12[7][0]), .sample_out_7_1(stage12[7][1])
);

fft_s1_2_4_4 u_fft_s1_2_4_4 (
  .sample_11_0(stage12_pre[11][0]), .sample_11_1(stage12_pre[11][1]),
  .sample_15_0(stage12_pre[15][0]), .sample_15_1(stage12_pre[15][1]),
  .sample_out_11_0(stage12[11][0]), .sample_out_11_1(stage12[11][1]),
  .sample_out_15_0(stage12[15][0]), .sample_out_15_1(stage12[15][1])
);

fft_s3_4_1 u_fft_s3_4_1 (
  .sample_0_0(stage12[0][0]), .sample_0_1(stage12[0][1]),
  .sample_1_0(stage12[1][0]), .sample_1_1(stage12[1][1]),
  .sample_2_0(stage12[2][0]), .sample_2_1(stage12[2][1]),
  .sample_3_0(stage12[3][0]), .sample_3_1(stage12[3][1]),
  .sample_out_0_0(sample_out_0_0), .sample_out_0_1(sample_out_0_1),
  .sample_out_1_0(sample_out_1_0), .sample_out_1_1(sample_out_1_1),
  .sample_out_2_0(sample_out_2_0), .sample_out_2_1(sample_out_2_1),
  .sample_out_3_0(sample_out_3_0), .sample_out_3_1(sample_out_3_1)
);

fft_s3_4_2 u_fft_s3_4_2 (
  .sample_4_0(stage12[4][0]), .sample_4_1(stage12[4][1]),
  .sample_5_0(stage12[5][0]), .sample_5_1(stage12[5][1]),
  .sample_6_0(stage12[6][0]), .sample_6_1(stage12[6][1]),
  .sample_7_0(stage12[7][0]), .sample_7_1(stage12[7][1]),
  .sample_out_4_0(sample_out_4_0), .sample_out_4_1(sample_out_4_1),
  .sample_out_5_0(sample_out_5_0), .sample_out_5_1(sample_out_5_1),
  .sample_out_6_0(sample_out_6_0), .sample_out_6_1(sample_out_6_1),
  .sample_out_7_0(sample_out_7_0), .sample_out_7_1(sample_out_7_1)
);

fft_s3_4_3 u_fft_s3_4_3 (
  .sample_8_0(stage12[8][0]),   .sample_8_1(stage12[8][1]),
  .sample_9_0(stage12[9][0]),   .sample_9_1(stage12[9][1]),
  .sample_10_0(stage12[10][0]), .sample_10_1(stage12[10][1]),
  .sample_11_0(stage12[11][0]), .sample_11_1(stage12[11][1]),
  .sample_out_8_0(sample_out_8_0),   .sample_out_8_1(sample_out_8_1),
  .sample_out_9_0(sample_out_9_0),   .sample_out_9_1(sample_out_9_1),
  .sample_out_10_0(sample_out_10_0), .sample_out_10_1(sample_out_10_1),
  .sample_out_11_0(sample_out_11_0), .sample_out_11_1(sample_out_11_1)
);

fft_s3_4_4 u_fft_s3_4_4 (
  .sample_12_0(stage12[12][0]), .sample_12_1(stage12[12][1]),
  .sample_13_0(stage12[13][0]), .sample_13_1(stage12[13][1]),
  .sample_14_0(stage12[14][0]), .sample_14_1(stage12[14][1]),
  .sample_15_0(stage12[15][0]), .sample_15_1(stage12[15][1]),
  .sample_out_12_0(sample_out_12_0), .sample_out_12_1(sample_out_12_1),
  .sample_out_13_0(sample_out_13_0), .sample_out_13_1(sample_out_13_1),
  .sample_out_14_0(sample_out_14_0), .sample_out_14_1(sample_out_14_1),
  .sample_out_15_0(sample_out_15_0), .sample_out_15_1(sample_out_15_1)
);

endmodule
