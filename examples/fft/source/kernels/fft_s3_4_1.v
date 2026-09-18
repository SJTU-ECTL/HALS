module fft_s3_4_1 (
  // Input Ports: Expanded from sample[0:15][0:1]
  input   signed [15:0] sample_0_0,  input   signed [15:0] sample_0_1,
  input   signed [15:0] sample_1_0,  input   signed [15:0] sample_1_1,
  input   signed [15:0] sample_2_0,  input   signed [15:0] sample_2_1,
  input   signed [15:0] sample_3_0,  input   signed [15:0] sample_3_1,

  // Output Ports: Expanded from sample_out[0:15][0:1]
  output reg signed [15:0] sample_out_0_0,  output reg signed [15:0] sample_out_0_1,
  output reg signed [15:0] sample_out_1_0,  output reg signed [15:0] sample_out_1_1,
  output reg signed [15:0] sample_out_2_0,  output reg signed [15:0] sample_out_2_1,
  output reg signed [15:0] sample_out_3_0,  output reg signed [15:0] sample_out_3_1
);

  // Internal State Registers (Working Variables)
  // These act as the "sample_out" array during computation
  reg signed [15:0] s0_0, s0_1;
  reg signed [15:0] s1_0, s1_1;
  reg signed [15:0] s2_0, s2_1;
  reg signed [15:0] s3_0, s3_1;

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

// Stage 3
// (0, 2)
tmp_real = s0_0 + s2_0;   tmp_imag = s0_1 + s2_1;
s2_0 = s0_0 - s2_0;       s2_1 = s0_1 - s2_1;
s0_0 = tmp_real;          s0_1 = tmp_imag;

// (1, 3)
tmp_real = s1_0 + s3_0; tmp_imag = s1_1 + s3_1;
tmp_real2 = s1_0 - s3_0; tmp_imag2 = s1_1 - s3_1;
tmp1 = (tmp_real2 * 16'sh0000) - (tmp_imag2 * 16'shff00);
tmp2 = (tmp_real2 * 16'shff00) + (tmp_imag2 * 16'sh0000);
tmp_sha = tmp1[23:8]; tmp_shb = tmp2[23:8];
s3_0 = tmp_sha; s3_1 = tmp_shb;
s1_0 = tmp_real; s1_1 = tmp_imag;


// Stage 4
// (0, 1)
tmp_real = s0_0 + s1_0;   tmp_imag = s0_1 + s1_1;
s1_0 = s0_0 - s1_0;       s1_1 = s0_1 - s1_1;
s0_0 = tmp_real;          s0_1 = tmp_imag;

// (2, 3)
tmp_real = s2_0 + s3_0;   tmp_imag = s2_1 + s3_1;
s3_0 = s2_0 - s3_0;       s3_1 = s2_1 - s3_1;
s2_0 = tmp_real;          s2_1 = tmp_imag;

// Assign internal state to outputs
sample_out_0_0 = s0_0;  sample_out_0_1 = s0_1;
sample_out_1_0 = s1_0;  sample_out_1_1 = s1_1;
sample_out_2_0 = s2_0;  sample_out_2_1 = s2_1;
sample_out_3_0 = s3_0;  sample_out_3_1 = s3_1;
end

endmodule
