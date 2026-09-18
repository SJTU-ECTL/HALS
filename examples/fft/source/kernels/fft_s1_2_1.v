module fft_s1_2_1 (
  // Input Ports: Expanded from sample[0:15][0:1]
  input   signed [15:0] sample_0_0,  input   signed [15:0] sample_0_1,
  input   signed [15:0] sample_4_0,  input   signed [15:0] sample_4_1,
  input   signed [15:0] sample_8_0,  input   signed [15:0] sample_8_1,
  input   signed [15:0] sample_12_0,  input   signed [15:0] sample_12_1,

  // Output Ports: Expanded from sample_out[0:15][0:1]
  output reg signed [15:0] sample_out_0_0,  output reg signed [15:0] sample_out_0_1,
  output reg signed [15:0] sample_out_4_0,  output reg signed [15:0] sample_out_4_1,
  output reg signed [15:0] sample_out_8_0,  output reg signed [15:0] sample_out_8_1,
  output reg signed [15:0] sample_out_12_0,  output reg signed [15:0] sample_out_12_1
);

  // Internal State Registers (Working Variables)
  // These act as the "sample_out" array during computation
  reg signed [15:0] s0_0, s0_1;
  reg signed [15:0] s4_0, s4_1;
  reg signed [15:0] s8_0, s8_1;
  reg signed [15:0] s12_0, s12_1;

  // Temporary variables for calculations
  reg signed [15:0] tmp_real, tmp_imag, tmp_real2, tmp_imag2;
  reg signed [31:0] tmp1, tmp2;
  reg signed [15:0] tmp_sha, tmp_shb;

  // Combinational Logic Block
always @(*) begin
// Initialize internal state with inputs
s0_0 = sample_0_0;  s0_1 = sample_0_1;
s4_0 = sample_4_0;  s4_1 = sample_4_1;
s8_0 = sample_8_0;  s8_1 = sample_8_1;
s12_0 = sample_12_0;  s12_1 = sample_12_1;

// Stage 1
// (0, 8)
tmp_real = s0_0 + s8_0;   tmp_imag = s0_1 + s8_1;
s8_0 = s0_0 - s8_0;       s8_1 = s0_1 - s8_1;
s0_0 = tmp_real;          s0_1 = tmp_imag;

// (4, 12)
tmp_real = s4_0 + s12_0; tmp_imag = s4_1 + s12_1;
tmp_real2 = s4_0 - s12_0; tmp_imag2 = s4_1 - s12_1;
tmp1 = (tmp_real2 * 16'sh0000) - (tmp_imag2 * 16'shff00);
tmp2 = (tmp_real2 * 16'shff00) + (tmp_imag2 * 16'sh0000);
tmp_sha = tmp1[23:8]; tmp_shb = tmp2[23:8];
s12_0 = tmp_sha; s12_1 = tmp_shb;
s4_0 = tmp_real; s4_1 = tmp_imag;


// Stage 2
// (0, 4)
tmp_real = s0_0 + s4_0;   tmp_imag = s0_1 + s4_1;
s4_0 = s0_0 - s4_0;       s4_1 = s0_1 - s4_1;
s0_0 = tmp_real;          s0_1 = tmp_imag;

// (8, 12)
tmp_real = s8_0 + s12_0;  tmp_imag = s8_1 + s12_1;
s12_0 = s8_0 - s12_0;     s12_1 = s8_1 - s12_1;
s8_0 = tmp_real;          s8_1 = tmp_imag;

// Assign internal state to outputs
sample_out_0_0 = s0_0;  sample_out_0_1 = s0_1;
sample_out_4_0 = s4_0;  sample_out_4_1 = s4_1;
sample_out_8_0 = s8_0;  sample_out_8_1 = s8_1;
sample_out_12_0 = s12_0;  sample_out_12_1 = s12_1;
end

endmodule
