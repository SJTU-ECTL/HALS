module fft_s1_2_4_2 (
  // Input Ports: Expanded from sample[0:15][0:1]
  input   signed [15:0] sample_7_0,  input   signed [15:0] sample_7_1,
  input   signed [15:0] sample_15_0,  input   signed [15:0] sample_15_1,

  // Output Ports: Expanded from sample_out[0:15][0:1]
  output reg signed [15:0] sample_out_7_0,  output reg signed [15:0] sample_out_7_1,
  output reg signed [15:0] sample_out_15_0,  output reg signed [15:0] sample_out_15_1
);

  // Internal State Registers (Working Variables)
  // These act as the "sample_out" array during computation
  reg signed [15:0] s7_0, s7_1;
  reg signed [15:0] s15_0, s15_1;

  // Temporary variables for calculations
  reg signed [15:0] tmp_real, tmp_imag, tmp_real2, tmp_imag2;
  reg signed [31:0] tmp1, tmp2;
  reg signed [15:0] tmp_sha, tmp_shb;

  // Combinational Logic Block
always @(*) begin
// Initialize internal state with inputs
s7_0 = sample_7_0;  s7_1 = sample_7_1;
s15_0 = sample_15_0;  s15_1 = sample_15_1;

// Stage 1
// (7, 15)
tmp_real = s7_0 + s15_0; tmp_imag = s7_1 + s15_1;
tmp_real2 = s7_0 - s15_0; tmp_imag2 = s7_1 - s15_1;
tmp1 = (tmp_real2 * 16'shff13) - (tmp_imag2 * 16'shff9e);
tmp2 = (tmp_real2 * 16'shff9e) + (tmp_imag2 * 16'shff13);
tmp_sha = tmp1[23:8]; tmp_shb = tmp2[23:8];
s15_0 = tmp_sha; s15_1 = tmp_shb;
s7_0 = tmp_real; s7_1 = tmp_imag;

// Assign internal state to outputs
sample_out_7_0 = s7_0;  sample_out_7_1 = s7_1;
sample_out_15_0 = s15_0;  sample_out_15_1 = s15_1;
end

endmodule
