// 5-stage decimation wrapper with proper shift-register history buffers
// Each stage maintains a buffer of past N samples via clocked shift registers.

module decim_wrapper (
    input clk,
    input rst,
    input signed [15:0] data_in,
    output reg signed [15:0] data_out
);

  // =========================================================================
  // Stage 1: 7-sample buffer → 7-tap symmetric FIR (4 coeffs)
  // =========================================================================
  reg signed [15:0] buf1 [0:6];
  wire signed [15:0] stage1_out;

  always @(posedge clk or posedge rst) begin
    if (rst) begin
      buf1[0] <= 0; buf1[1] <= 0; buf1[2] <= 0; buf1[3] <= 0;
      buf1[4] <= 0; buf1[5] <= 0; buf1[6] <= 0;
    end else begin
      buf1[0] <= data_in;
      buf1[1] <= buf1[0];
      buf1[2] <= buf1[1];
      buf1[3] <= buf1[2];
      buf1[4] <= buf1[3];
      buf1[5] <= buf1[4];
      buf1[6] <= buf1[5];
    end
  end

  decim_stage1 s1 (
    .buf_0(buf1[0]), .buf_1(buf1[1]), .buf_2(buf1[2]), .buf_3(buf1[3]),
    .buf_4(buf1[4]), .buf_5(buf1[5]), .buf_6(buf1[6]),
    .out(stage1_out)
  );

  // =========================================================================
  // Stage 2: 7-sample buffer → 7-tap symmetric FIR (4 coeffs)
  // =========================================================================
  reg signed [15:0] buf2 [0:6];
  wire signed [15:0] stage2_out;

  always @(posedge clk or posedge rst) begin
    if (rst) begin
      buf2[0] <= 0; buf2[1] <= 0; buf2[2] <= 0; buf2[3] <= 0;
      buf2[4] <= 0; buf2[5] <= 0; buf2[6] <= 0;
    end else begin
      buf2[0] <= stage1_out;
      buf2[1] <= buf2[0];
      buf2[2] <= buf2[1];
      buf2[3] <= buf2[2];
      buf2[4] <= buf2[3];
      buf2[5] <= buf2[4];
      buf2[6] <= buf2[5];
    end
  end

  decim_stage2 s2 (
    .buf_0(buf2[0]), .buf_1(buf2[1]), .buf_2(buf2[2]), .buf_3(buf2[3]),
    .buf_4(buf2[4]), .buf_5(buf2[5]), .buf_6(buf2[6]),
    .out(stage2_out)
  );

  // =========================================================================
  // Stage 3: 7-sample buffer → 7-tap symmetric FIR (4 coeffs)
  // =========================================================================
  reg signed [15:0] buf3 [0:6];
  wire signed [15:0] stage3_out;

  always @(posedge clk or posedge rst) begin
    if (rst) begin
      buf3[0] <= 0; buf3[1] <= 0; buf3[2] <= 0; buf3[3] <= 0;
      buf3[4] <= 0; buf3[5] <= 0; buf3[6] <= 0;
    end else begin
      buf3[0] <= stage2_out;
      buf3[1] <= buf3[0];
      buf3[2] <= buf3[1];
      buf3[3] <= buf3[2];
      buf3[4] <= buf3[3];
      buf3[5] <= buf3[4];
      buf3[6] <= buf3[5];
    end
  end

  decim_stage3 s3 (
    .buf_0(buf3[0]), .buf_1(buf3[1]), .buf_2(buf3[2]), .buf_3(buf3[3]),
    .buf_4(buf3[4]), .buf_5(buf3[5]), .buf_6(buf3[6]),
    .out(stage3_out)
  );

  // =========================================================================
  // Stage 4: 11-sample buffer → 11-tap symmetric FIR (6 coeffs)
  // =========================================================================
  reg signed [15:0] buf4 [0:10];
  wire signed [15:0] stage4_out;

  always @(posedge clk or posedge rst) begin
    if (rst) begin
      buf4[0] <= 0; buf4[1] <= 0; buf4[2] <= 0; buf4[3] <= 0; buf4[4] <= 0;
      buf4[5] <= 0; buf4[6] <= 0; buf4[7] <= 0; buf4[8] <= 0; buf4[9] <= 0;
      buf4[10] <= 0;
    end else begin
      buf4[0] <= stage3_out;
      buf4[1] <= buf4[0]; buf4[2] <= buf4[1]; buf4[3] <= buf4[2]; buf4[4] <= buf4[3];
      buf4[5] <= buf4[4]; buf4[6] <= buf4[5]; buf4[7] <= buf4[6]; buf4[8] <= buf4[7];
      buf4[9] <= buf4[8]; buf4[10] <= buf4[9];
    end
  end

  decim_stage4 s4 (
    .buf_0(buf4[0]), .buf_1(buf4[1]), .buf_2(buf4[2]), .buf_3(buf4[3]),
    .buf_4(buf4[4]), .buf_5(buf4[5]), .buf_6(buf4[6]), .buf_7(buf4[7]),
    .buf_8(buf4[8]), .buf_9(buf4[9]), .buf_10(buf4[10]),
    .out(stage4_out)
  );

  // =========================================================================
  // Stage 5: 24-sample buffer → 23-tap symmetric polyphase FIR (12 coeffs)
  // =========================================================================
  reg signed [15:0] buf5 [0:23];
  wire signed [15:0] stage5_out;

  always @(posedge clk or posedge rst) begin
    if (rst) begin
      buf5[0] <= 0; buf5[1] <= 0; buf5[2] <= 0; buf5[3] <= 0; buf5[4] <= 0;
      buf5[5] <= 0; buf5[6] <= 0; buf5[7] <= 0; buf5[8] <= 0; buf5[9] <= 0;
      buf5[10] <= 0; buf5[11] <= 0; buf5[12] <= 0; buf5[13] <= 0; buf5[14] <= 0;
      buf5[15] <= 0; buf5[16] <= 0; buf5[17] <= 0; buf5[18] <= 0; buf5[19] <= 0;
      buf5[20] <= 0; buf5[21] <= 0; buf5[22] <= 0; buf5[23] <= 0;
    end else begin
      buf5[0] <= stage4_out;
      buf5[1] <= buf5[0]; buf5[2] <= buf5[1]; buf5[3] <= buf5[2]; buf5[4] <= buf5[3];
      buf5[5] <= buf5[4]; buf5[6] <= buf5[5]; buf5[7] <= buf5[6]; buf5[8] <= buf5[7];
      buf5[9] <= buf5[8]; buf5[10] <= buf5[9]; buf5[11] <= buf5[10]; buf5[12] <= buf5[11];
      buf5[13] <= buf5[12]; buf5[14] <= buf5[13]; buf5[15] <= buf5[14]; buf5[16] <= buf5[15];
      buf5[17] <= buf5[16]; buf5[18] <= buf5[17]; buf5[19] <= buf5[18]; buf5[20] <= buf5[19];
      buf5[21] <= buf5[20]; buf5[22] <= buf5[21]; buf5[23] <= buf5[22];
    end
  end

  decim_stage5 s5 (
    .buf_0(buf5[0]), .buf_1(buf5[1]), .buf_2(buf5[2]), .buf_3(buf5[3]),
    .buf_4(buf5[4]), .buf_5(buf5[5]), .buf_6(buf5[6]), .buf_7(buf5[7]),
    .buf_8(buf5[8]), .buf_9(buf5[9]), .buf_10(buf5[10]), .buf_11(buf5[11]),
    .buf_12(buf5[12]), .buf_13(buf5[13]), .buf_14(buf5[14]), .buf_15(buf5[15]),
    .buf_16(buf5[16]), .buf_17(buf5[17]), .buf_18(buf5[18]), .buf_19(buf5[19]),
    .buf_20(buf5[20]), .buf_21(buf5[21]), .buf_22(buf5[22]), .buf_23(buf5[23]),
    .out(stage5_out)
  );

  always @(posedge clk or posedge rst) begin
    if (rst) data_out <= 0;
    else data_out <= stage5_out;
  end

endmodule