module fft_s1_2_2_3 (
  // Input Ports: Expanded from sample[0:15][0:1]
  input   signed [15:0] sample_1_0,  input   signed [15:0] sample_1_1,
  input   signed [15:0] sample_5_0,  input   signed [15:0] sample_5_1,

  // Output Ports: Expanded from sample_out[0:15][0:1]
  output reg signed [15:0] sample_out_1_0,  output reg signed [15:0] sample_out_1_1,
  output reg signed [15:0] sample_out_5_0,  output reg signed [15:0] sample_out_5_1
);

  // Internal State Registers (Working Variables)
  // These act as the "sample_out" array during computation
  reg signed [15:0] s1_0, s1_1;
  reg signed [15:0] s5_0, s5_1;

  // Temporary variables for calculations
  reg signed [15:0] tmp_real, tmp_imag, tmp_real2, tmp_imag2;
  reg signed [31:0] tmp1, tmp2;
  reg signed [15:0] tmp_sha, tmp_shb;

  // Combinational Logic Block
always @(*) begin
// Initialize internal state with inputs
s1_0 = sample_1_0;  s1_1 = sample_1_1;
s5_0 = sample_5_0;  s5_1 = sample_5_1;

// Stage 2
// (1, 5)
tmp_real = s1_0 + s5_0; tmp_imag = s1_1 + s5_1;
tmp_real2 = s1_0 - s5_0; tmp_imag2 = s1_1 - s5_1;
tmp1 = (tmp_real2 * 16'sh00b5) - (tmp_imag2 * 16'shff4b);
tmp2 = (tmp_real2 * 16'shff4b) + (tmp_imag2 * 16'sh00b5);
tmp_sha = tmp1[23:8]; tmp_shb = tmp2[23:8];
s5_0 = tmp_sha; s5_1 = tmp_shb;
s1_0 = tmp_real; s1_1 = tmp_imag;

// Assign internal state to outputs
sample_out_1_0 = s1_0;  sample_out_1_1 = s1_1;
sample_out_5_0 = s5_0;  sample_out_5_1 = s5_1;
end

endmodule
