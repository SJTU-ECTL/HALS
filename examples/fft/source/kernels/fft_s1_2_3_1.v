module fft_s1_2_3_1 (
  // Input Ports: Expanded from sample[0:15][0:1]
  input   signed [15:0] sample_2_0,  input   signed [15:0] sample_2_1,
  input   signed [15:0] sample_10_0,  input   signed [15:0] sample_10_1,

  // Output Ports: Expanded from sample_out[0:15][0:1]
  output reg signed [15:0] sample_out_2_0,  output reg signed [15:0] sample_out_2_1,
  output reg signed [15:0] sample_out_10_0,  output reg signed [15:0] sample_out_10_1
);

  // Internal State Registers (Working Variables)
  // These act as the "sample_out" array during computation
  reg signed [15:0] s2_0, s2_1;
  reg signed [15:0] s10_0, s10_1;

  // Temporary variables for calculations
  reg signed [15:0] tmp_real, tmp_imag, tmp_real2, tmp_imag2;
  reg signed [31:0] tmp1, tmp2;
  reg signed [15:0] tmp_sha, tmp_shb;

  // Combinational Logic Block
always @(*) begin
// Initialize internal state with inputs
s2_0 = sample_2_0;  s2_1 = sample_2_1;
s10_0 = sample_10_0;  s10_1 = sample_10_1;

// Stage 1
// (2, 10)
tmp_real = s2_0 + s10_0; tmp_imag = s2_1 + s10_1;
tmp_real2 = s2_0 - s10_0; tmp_imag2 = s2_1 - s10_1;
tmp1 = (tmp_real2 * 16'sh00b5) - (tmp_imag2 * 16'shff4b);
tmp2 = (tmp_real2 * 16'shff4b) + (tmp_imag2 * 16'sh00b5);
tmp_sha = tmp1[23:8]; tmp_shb = tmp2[23:8];
s10_0 = tmp_sha; s10_1 = tmp_shb;
s2_0 = tmp_real; s2_1 = tmp_imag;

// Assign internal state to outputs
sample_out_2_0 = s2_0;  sample_out_2_1 = s2_1;
sample_out_10_0 = s10_0;  sample_out_10_1 = s10_1;
end

endmodule
