module fft_s1_2_4_1 (
  // Input Ports: Expanded from sample[0:15][0:1]
  input   signed [15:0] sample_3_0,  input   signed [15:0] sample_3_1,
  input   signed [15:0] sample_11_0,  input   signed [15:0] sample_11_1,

  // Output Ports: Expanded from sample_out[0:15][0:1]
  output reg signed [15:0] sample_out_3_0,  output reg signed [15:0] sample_out_3_1,
  output reg signed [15:0] sample_out_11_0,  output reg signed [15:0] sample_out_11_1
);

  // Internal State Registers (Working Variables)
  // These act as the "sample_out" array during computation
  reg signed [15:0] s3_0, s3_1;
  reg signed [15:0] s11_0, s11_1;

  // Temporary variables for calculations
  reg signed [15:0] tmp_real, tmp_imag, tmp_real2, tmp_imag2;
  reg signed [31:0] tmp1, tmp2;
  reg signed [15:0] tmp_sha, tmp_shb;

  // Combinational Logic Block
always @(*) begin
// Initialize internal state with inputs
s3_0 = sample_3_0;  s3_1 = sample_3_1;
s11_0 = sample_11_0;  s11_1 = sample_11_1;

// Stage 1
// (3, 11)
tmp_real = s3_0 + s11_0; tmp_imag = s3_1 + s11_1;
tmp_real2 = s3_0 - s11_0; tmp_imag2 = s3_1 - s11_1;
tmp1 = (tmp_real2 * 16'sh0062) - (tmp_imag2 * 16'shff13);
tmp2 = (tmp_real2 * 16'shff13) + (tmp_imag2 * 16'sh0062);
tmp_sha = tmp1[23:8]; tmp_shb = tmp2[23:8];
s11_0 = tmp_sha; s11_1 = tmp_shb;
s3_0 = tmp_real; s3_1 = tmp_imag;

// Assign internal state to outputs
sample_out_3_0 = s3_0;  sample_out_3_1 = s3_1;
sample_out_11_0 = s11_0;  sample_out_11_1 = s11_1;
end

endmodule
