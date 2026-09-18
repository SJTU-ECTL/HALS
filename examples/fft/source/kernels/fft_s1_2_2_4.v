module fft_s1_2_2_4 (
  // Input Ports: Expanded from sample[0:15][0:1]
  input   signed [15:0] sample_9_0,  input   signed [15:0] sample_9_1,
  input   signed [15:0] sample_13_0,  input   signed [15:0] sample_13_1,

  // Output Ports: Expanded from sample_out[0:15][0:1]
  output reg signed [15:0] sample_out_9_0,  output reg signed [15:0] sample_out_9_1,
  output reg signed [15:0] sample_out_13_0,  output reg signed [15:0] sample_out_13_1
);

  // Internal State Registers (Working Variables)
  // These act as the "sample_out" array during computation
  reg signed [15:0] s9_0, s9_1;
  reg signed [15:0] s13_0, s13_1;

  // Temporary variables for calculations
  reg signed [15:0] tmp_real, tmp_imag, tmp_real2, tmp_imag2;
  reg signed [31:0] tmp1, tmp2;
  reg signed [15:0] tmp_sha, tmp_shb;

  // Combinational Logic Block
always @(*) begin
// Initialize internal state with inputs
s9_0 = sample_9_0;  s9_1 = sample_9_1;
s13_0 = sample_13_0;  s13_1 = sample_13_1;

// Stage 2
// (9, 13)
tmp_real = s9_0 + s13_0; tmp_imag = s9_1 + s13_1;
tmp_real2 = s9_0 - s13_0; tmp_imag2 = s9_1 - s13_1;
tmp1 = (tmp_real2 * 16'sh00b5) - (tmp_imag2 * 16'shff4b);
tmp2 = (tmp_real2 * 16'shff4b) + (tmp_imag2 * 16'sh00b5);
tmp_sha = tmp1[23:8]; tmp_shb = tmp2[23:8];
s13_0 = tmp_sha; s13_1 = tmp_shb;
s9_0 = tmp_real; s9_1 = tmp_imag;

// Assign internal state to outputs
sample_out_9_0 = s9_0;  sample_out_9_1 = s9_1;
sample_out_13_0 = s13_0;  sample_out_13_1 = s13_1;
end

endmodule
