module fft (
  // Input Ports: Expanded from sample[0:15][0:1]
  input  signed [15:0] sample_0_0,  input  signed [15:0] sample_0_1,
  input  signed [15:0] sample_1_0,  input  signed [15:0] sample_1_1,
  input  signed [15:0] sample_2_0,  input  signed [15:0] sample_2_1,
  input  signed [15:0] sample_3_0,  input  signed [15:0] sample_3_1,
  input  signed [15:0] sample_4_0,  input  signed [15:0] sample_4_1,
  input  signed [15:0] sample_5_0,  input  signed [15:0] sample_5_1,
  input  signed [15:0] sample_6_0,  input  signed [15:0] sample_6_1,
  input  signed [15:0] sample_7_0,  input  signed [15:0] sample_7_1,
  input  signed [15:0] sample_8_0,  input  signed [15:0] sample_8_1,
  input  signed [15:0] sample_9_0,  input  signed [15:0] sample_9_1,
  input  signed [15:0] sample_10_0, input  signed [15:0] sample_10_1,
  input  signed [15:0] sample_11_0, input  signed [15:0] sample_11_1,
  input  signed [15:0] sample_12_0, input  signed [15:0] sample_12_1,
  input  signed [15:0] sample_13_0, input  signed [15:0] sample_13_1,
  input  signed [15:0] sample_14_0, input  signed [15:0] sample_14_1,
  input  signed [15:0] sample_15_0, input  signed [15:0] sample_15_1,

  // Output Ports: Expanded from sample_out[0:15][0:1]
  output reg signed [15:0] sample_out_0_0,  output reg signed [15:0] sample_out_0_1,
  output reg signed [15:0] sample_out_1_0,  output reg signed [15:0] sample_out_1_1,
  output reg signed [15:0] sample_out_2_0,  output reg signed [15:0] sample_out_2_1,
  output reg signed [15:0] sample_out_3_0,  output reg signed [15:0] sample_out_3_1,
  output reg signed [15:0] sample_out_4_0,  output reg signed [15:0] sample_out_4_1,
  output reg signed [15:0] sample_out_5_0,  output reg signed [15:0] sample_out_5_1,
  output reg signed [15:0] sample_out_6_0,  output reg signed [15:0] sample_out_6_1,
  output reg signed [15:0] sample_out_7_0,  output reg signed [15:0] sample_out_7_1,
  output reg signed [15:0] sample_out_8_0,  output reg signed [15:0] sample_out_8_1,
  output reg signed [15:0] sample_out_9_0,  output reg signed [15:0] sample_out_9_1,
  output reg signed [15:0] sample_out_10_0, output reg signed [15:0] sample_out_10_1,
  output reg signed [15:0] sample_out_11_0, output reg signed [15:0] sample_out_11_1,
  output reg signed [15:0] sample_out_12_0, output reg signed [15:0] sample_out_12_1,
  output reg signed [15:0] sample_out_13_0, output reg signed [15:0] sample_out_13_1,
  output reg signed [15:0] sample_out_14_0, output reg signed [15:0] sample_out_14_1,
  output reg signed [15:0] sample_out_15_0, output reg signed [15:0] sample_out_15_1
);

  // Internal State Registers (Working Variables)
  // These act as the "sample_out" array during computation
  reg signed [15:0] s0_0, s0_1;
  reg signed [15:0] s1_0, s1_1;
  reg signed [15:0] s2_0, s2_1;
  reg signed [15:0] s3_0, s3_1;
  reg signed [15:0] s4_0, s4_1;
  reg signed [15:0] s5_0, s5_1;
  reg signed [15:0] s6_0, s6_1;
  reg signed [15:0] s7_0, s7_1;
  reg signed [15:0] s8_0, s8_1;
  reg signed [15:0] s9_0, s9_1;
  reg signed [15:0] s10_0, s10_1;
  reg signed [15:0] s11_0, s11_1;
  reg signed [15:0] s12_0, s12_1;
  reg signed [15:0] s13_0, s13_1;
  reg signed [15:0] s14_0, s14_1;
  reg signed [15:0] s15_0, s15_1;

  // Temporary variables for calculations
  reg signed [15:0] tmp_real, tmp_imag, tmp_real2, tmp_imag2;
  reg signed [31:0] tmp1, tmp2;
  reg signed [15:0] tmp_sha, tmp_shb;

  // Combinational Logic Block
always @(*) begin
// Initialize internal state with inputs
s0_0 = sample_0_0;  s0_1 = sample_0_1;
s1_0 = sample_1_0;  s1_1 = sample_1_1;
s2_0 = sample_2_0;  s2_1 = sample_2_1;
s3_0 = sample_3_0;  s3_1 = sample_3_1;
s4_0 = sample_4_0;  s4_1 = sample_4_1;
s5_0 = sample_5_0;  s5_1 = sample_5_1;
s6_0 = sample_6_0;  s6_1 = sample_6_1;
s7_0 = sample_7_0;  s7_1 = sample_7_1;
s8_0 = sample_8_0;  s8_1 = sample_8_1;
s9_0 = sample_9_0;  s9_1 = sample_9_1;
s10_0 = sample_10_0; s10_1 = sample_10_1;
s11_0 = sample_11_0; s11_1 = sample_11_1;
s12_0 = sample_12_0; s12_1 = sample_12_1;
s13_0 = sample_13_0; s13_1 = sample_13_1;
s14_0 = sample_14_0; s14_1 = sample_14_1;
s15_0 = sample_15_0; s15_1 = sample_15_1;

// Stage 1
// (0, 8)
tmp_real = s0_0 + s8_0;   tmp_imag = s0_1 + s8_1;
s8_0 = s0_0 - s8_0;       s8_1 = s0_1 - s8_1;
s0_0 = tmp_real;          s0_1 = tmp_imag;

// (1, 9)
tmp_real = s1_0 + s9_0; tmp_imag = s1_1 + s9_1;
tmp_real2 = s1_0 - s9_0; tmp_imag2 = s1_1 - s9_1;
tmp1 = (tmp_real2 * 16'sh00ed) - (tmp_imag2 * 16'shff9e);
tmp2 = (tmp_real2 * 16'shff9e) + (tmp_imag2 * 16'sh00ed);
tmp_sha = tmp1[23:8]; tmp_shb = tmp2[23:8];
s9_0 = tmp_sha; s9_1 = tmp_shb;
s1_0 = tmp_real; s1_1 = tmp_imag;

// (2, 10)
tmp_real = s2_0 + s10_0; tmp_imag = s2_1 + s10_1;
tmp_real2 = s2_0 - s10_0; tmp_imag2 = s2_1 - s10_1;
tmp1 = (tmp_real2 * 16'sh00b5) - (tmp_imag2 * 16'shff4b);
tmp2 = (tmp_real2 * 16'shff4b) + (tmp_imag2 * 16'sh00b5);
tmp_sha = tmp1[23:8]; tmp_shb = tmp2[23:8];
s10_0 = tmp_sha; s10_1 = tmp_shb;
s2_0 = tmp_real; s2_1 = tmp_imag;

// (3, 11)
tmp_real = s3_0 + s11_0; tmp_imag = s3_1 + s11_1;
tmp_real2 = s3_0 - s11_0; tmp_imag2 = s3_1 - s11_1;
tmp1 = (tmp_real2 * 16'sh0062) - (tmp_imag2 * 16'shff13);
tmp2 = (tmp_real2 * 16'shff13) + (tmp_imag2 * 16'sh0062);
tmp_sha = tmp1[23:8]; tmp_shb = tmp2[23:8];
s11_0 = tmp_sha; s11_1 = tmp_shb;
s3_0 = tmp_real; s3_1 = tmp_imag;

// (4, 12)
tmp_real = s4_0 + s12_0; tmp_imag = s4_1 + s12_1;
tmp_real2 = s4_0 - s12_0; tmp_imag2 = s4_1 - s12_1;
tmp1 = (tmp_real2 * 16'sh0000) - (tmp_imag2 * 16'shff00);
tmp2 = (tmp_real2 * 16'shff00) + (tmp_imag2 * 16'sh0000);
tmp_sha = tmp1[23:8]; tmp_shb = tmp2[23:8];
s12_0 = tmp_sha; s12_1 = tmp_shb;
s4_0 = tmp_real; s4_1 = tmp_imag;

// (5, 13)
tmp_real = s5_0 + s13_0; tmp_imag = s5_1 + s13_1;
tmp_real2 = s5_0 - s13_0; tmp_imag2 = s5_1 - s13_1;
tmp1 = (tmp_real2 * 16'shff9e) - (tmp_imag2 * 16'shff13);
tmp2 = (tmp_real2 * 16'shff13) + (tmp_imag2 * 16'shff9e);
tmp_sha = tmp1[23:8]; tmp_shb = tmp2[23:8];
s13_0 = tmp_sha; s13_1 = tmp_shb;
s5_0 = tmp_real; s5_1 = tmp_imag;

// (6, 14)
tmp_real = s6_0 + s14_0; tmp_imag = s6_1 + s14_1;
tmp_real2 = s6_0 - s14_0; tmp_imag2 = s6_1 - s14_1;
tmp1 = (tmp_real2 * 16'shff4b) - (tmp_imag2 * 16'shff4b);
tmp2 = (tmp_real2 * 16'shff4b) + (tmp_imag2 * 16'shff4b);
tmp_sha = tmp1[23:8]; tmp_shb = tmp2[23:8];
s14_0 = tmp_sha; s14_1 = tmp_shb;
s6_0 = tmp_real; s6_1 = tmp_imag;

// (7, 15)
tmp_real = s7_0 + s15_0; tmp_imag = s7_1 + s15_1;
tmp_real2 = s7_0 - s15_0; tmp_imag2 = s7_1 - s15_1;
tmp1 = (tmp_real2 * 16'shff13) - (tmp_imag2 * 16'shff9e);
tmp2 = (tmp_real2 * 16'shff9e) + (tmp_imag2 * 16'shff13);
tmp_sha = tmp1[23:8]; tmp_shb = tmp2[23:8];
s15_0 = tmp_sha; s15_1 = tmp_shb;
s7_0 = tmp_real; s7_1 = tmp_imag;

// Stage 2
// (0, 4)
tmp_real = s0_0 + s4_0;   tmp_imag = s0_1 + s4_1;
s4_0 = s0_0 - s4_0;       s4_1 = s0_1 - s4_1;
s0_0 = tmp_real;          s0_1 = tmp_imag;

// (8, 12)
tmp_real = s8_0 + s12_0;  tmp_imag = s8_1 + s12_1;
s12_0 = s8_0 - s12_0;     s12_1 = s8_1 - s12_1;
s8_0 = tmp_real;          s8_1 = tmp_imag;

// (1, 5)
tmp_real = s1_0 + s5_0; tmp_imag = s1_1 + s5_1;
tmp_real2 = s1_0 - s5_0; tmp_imag2 = s1_1 - s5_1;
tmp1 = (tmp_real2 * 16'sh00b5) - (tmp_imag2 * 16'shff4b);
tmp2 = (tmp_real2 * 16'shff4b) + (tmp_imag2 * 16'sh00b5);
tmp_sha = tmp1[23:8]; tmp_shb = tmp2[23:8];
s5_0 = tmp_sha; s5_1 = tmp_shb;
s1_0 = tmp_real; s1_1 = tmp_imag;

// (9, 13)
tmp_real = s9_0 + s13_0; tmp_imag = s9_1 + s13_1;
tmp_real2 = s9_0 - s13_0; tmp_imag2 = s9_1 - s13_1;
tmp1 = (tmp_real2 * 16'sh00b5) - (tmp_imag2 * 16'shff4b);
tmp2 = (tmp_real2 * 16'shff4b) + (tmp_imag2 * 16'sh00b5);
tmp_sha = tmp1[23:8]; tmp_shb = tmp2[23:8];
s13_0 = tmp_sha; s13_1 = tmp_shb;
s9_0 = tmp_real; s9_1 = tmp_imag;

// (2, 6)
tmp_real = s2_0 + s6_0; tmp_imag = s2_1 + s6_1;
tmp_real2 = s2_0 - s6_0; tmp_imag2 = s2_1 - s6_1;
tmp1 = (tmp_real2 * 16'sh0000) - (tmp_imag2 * 16'shff00);
tmp2 = (tmp_real2 * 16'shff00) + (tmp_imag2 * 16'sh0000);
tmp_sha = tmp1[23:8]; tmp_shb = tmp2[23:8];
s6_0 = tmp_sha; s6_1 = tmp_shb;
s2_0 = tmp_real; s2_1 = tmp_imag;

// (10, 14)
tmp_real = s10_0 + s14_0; tmp_imag = s10_1 + s14_1;
tmp_real2 = s10_0 - s14_0; tmp_imag2 = s10_1 - s14_1;
tmp1 = (tmp_real2 * 16'sh0000) - (tmp_imag2 * 16'shff00);
tmp2 = (tmp_real2 * 16'shff00) + (tmp_imag2 * 16'sh0000);
tmp_sha = tmp1[23:8]; tmp_shb = tmp2[23:8];
s14_0 = tmp_sha; s14_1 = tmp_shb;
s10_0 = tmp_real; s10_1 = tmp_imag;

// (3, 7)
tmp_real = s3_0 + s7_0; tmp_imag = s3_1 + s7_1;
tmp_real2 = s3_0 - s7_0; tmp_imag2 = s3_1 - s7_1;
tmp1 = (tmp_real2 * 16'shff4b) - (tmp_imag2 * 16'shff4b);
tmp2 = (tmp_real2 * 16'shff4b) + (tmp_imag2 * 16'shff4b);
tmp_sha = tmp1[23:8]; tmp_shb = tmp2[23:8];
s7_0 = tmp_sha; s7_1 = tmp_shb;
s3_0 = tmp_real; s3_1 = tmp_imag;

// (11, 15)
tmp_real = s11_0 + s15_0; tmp_imag = s11_1 + s15_1;
tmp_real2 = s11_0 - s15_0; tmp_imag2 = s11_1 - s15_1;
tmp1 = (tmp_real2 * 16'shff4b) - (tmp_imag2 * 16'shff4b);
tmp2 = (tmp_real2 * 16'shff4b) + (tmp_imag2 * 16'shff4b);
tmp_sha = tmp1[23:8]; tmp_shb = tmp2[23:8];
s15_0 = tmp_sha; s15_1 = tmp_shb;
s11_0 = tmp_real; s11_1 = tmp_imag;

// Assign internal state to outputs
sample_out_0_0 = s0_0;  sample_out_0_1 = s0_1;
sample_out_1_0 = s1_0;  sample_out_1_1 = s1_1;
sample_out_2_0 = s2_0;  sample_out_2_1 = s2_1;
sample_out_3_0 = s3_0;  sample_out_3_1 = s3_1;
sample_out_4_0 = s4_0;  sample_out_4_1 = s4_1;
sample_out_5_0 = s5_0;  sample_out_5_1 = s5_1;
sample_out_6_0 = s6_0;  sample_out_6_1 = s6_1;
sample_out_7_0 = s7_0;  sample_out_7_1 = s7_1;
sample_out_8_0 = s8_0;  sample_out_8_1 = s8_1;
sample_out_9_0 = s9_0;  sample_out_9_1 = s9_1;
sample_out_10_0 = s10_0; sample_out_10_1 = s10_1;
sample_out_11_0 = s11_0; sample_out_11_1 = s11_1;
sample_out_12_0 = s12_0; sample_out_12_1 = s12_1;
sample_out_13_0 = s13_0; sample_out_13_1 = s13_1;
sample_out_14_0 = s14_0; sample_out_14_1 = s14_1;
sample_out_15_0 = s15_0; sample_out_15_1 = s15_1;
end

endmodule
