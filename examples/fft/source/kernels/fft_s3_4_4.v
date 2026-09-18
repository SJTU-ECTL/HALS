module fft_s3_4_4 (
  // Input Ports: Expanded from sample[0:15][0:1]
  input   signed [15:0] sample_12_0,  input   signed [15:0] sample_12_1,
  input   signed [15:0] sample_13_0,  input   signed [15:0] sample_13_1,
  input   signed [15:0] sample_14_0,  input   signed [15:0] sample_14_1,
  input   signed [15:0] sample_15_0,  input   signed [15:0] sample_15_1,

  // Output Ports: Expanded from sample_out[0:15][0:1]
  output reg signed [15:0] sample_out_12_0,  output reg signed [15:0] sample_out_12_1,
  output reg signed [15:0] sample_out_13_0,  output reg signed [15:0] sample_out_13_1,
  output reg signed [15:0] sample_out_14_0,  output reg signed [15:0] sample_out_14_1,
  output reg signed [15:0] sample_out_15_0,  output reg signed [15:0] sample_out_15_1
);

  // Internal State Registers (Working Variables)
  // These act as the "sample_out" array during computation
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
s12_0 = sample_12_0;  s12_1 = sample_12_1;
s13_0 = sample_13_0;  s13_1 = sample_13_1;
s14_0 = sample_14_0;  s14_1 = sample_14_1;
s15_0 = sample_15_0;  s15_1 = sample_15_1;

// Stage 3
// (12, 14)
tmp_real = s12_0 + s14_0; tmp_imag = s12_1 + s14_1;
s14_0 = s12_0 - s14_0;    s14_1 = s12_1 - s14_1;
s12_0 = tmp_real;         s12_1 = tmp_imag;

// (13, 15)
tmp_real = s13_0 + s15_0; tmp_imag = s13_1 + s15_1;
tmp_real2 = s13_0 - s15_0; tmp_imag2 = s13_1 - s15_1;
tmp1 = (tmp_real2 * 16'sh0000) - (tmp_imag2 * 16'shff00);
tmp2 = (tmp_real2 * 16'shff00) + (tmp_imag2 * 16'sh0000);
tmp_sha = tmp1[23:8]; tmp_shb = tmp2[23:8];
s15_0 = tmp_sha; s15_1 = tmp_shb;
s13_0 = tmp_real; s13_1 = tmp_imag;


// Stage 4
// (12, 13)
tmp_real = s12_0 + s13_0; tmp_imag = s12_1 + s13_1;
s13_0 = s12_0 - s13_0;    s13_1 = s12_1 - s13_1;
s12_0 = tmp_real;         s12_1 = tmp_imag;

// (14, 15)
tmp_real = s14_0 + s15_0; tmp_imag = s14_1 + s15_1;
s15_0 = s14_0 - s15_0;    s15_1 = s14_1 - s15_1;
s14_0 = tmp_real;         s14_1 = tmp_imag;

// Assign internal state to outputs
sample_out_12_0 = s12_0;  sample_out_12_1 = s12_1;
sample_out_13_0 = s13_0;  sample_out_13_1 = s13_1;
sample_out_14_0 = s14_0;  sample_out_14_1 = s14_1;
sample_out_15_0 = s15_0;  sample_out_15_1 = s15_1;
end

endmodule
