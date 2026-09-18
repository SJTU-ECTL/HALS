module fft_s1_2_3_2 (
  // Input Ports: Expanded from sample[0:15][0:1]
  input   signed [15:0] sample_6_0,  input   signed [15:0] sample_6_1,
  input   signed [15:0] sample_14_0,  input   signed [15:0] sample_14_1,

  // Output Ports: Expanded from sample_out[0:15][0:1]
  output reg signed [15:0] sample_out_6_0,  output reg signed [15:0] sample_out_6_1,
  output reg signed [15:0] sample_out_14_0,  output reg signed [15:0] sample_out_14_1
);

  // Internal State Registers (Working Variables)
  // These act as the "sample_out" array during computation
  reg signed [15:0] s6_0, s6_1;
  reg signed [15:0] s14_0, s14_1;

  // Temporary variables for calculations
  reg signed [15:0] tmp_real, tmp_imag, tmp_real2, tmp_imag2;
  reg signed [31:0] tmp1, tmp2;
  reg signed [15:0] tmp_sha, tmp_shb;

  // Combinational Logic Block
always @(*) begin
// Initialize internal state with inputs
s6_0 = sample_6_0;  s6_1 = sample_6_1;
s14_0 = sample_14_0;  s14_1 = sample_14_1;

// Stage 1
// (6, 14)
tmp_real = s6_0 + s14_0; tmp_imag = s6_1 + s14_1;
tmp_real2 = s6_0 - s14_0; tmp_imag2 = s6_1 - s14_1;
tmp1 = (tmp_real2 * 16'shff4b) - (tmp_imag2 * 16'shff4b);
tmp2 = (tmp_real2 * 16'shff4b) + (tmp_imag2 * 16'shff4b);
tmp_sha = tmp1[23:8]; tmp_shb = tmp2[23:8];
s14_0 = tmp_sha; s14_1 = tmp_shb;
s6_0 = tmp_real; s6_1 = tmp_imag;

// Assign internal state to outputs
sample_out_6_0 = s6_0;  sample_out_6_1 = s6_1;
sample_out_14_0 = s14_0;  sample_out_14_1 = s14_1;
end

endmodule
