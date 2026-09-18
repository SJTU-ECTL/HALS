module fft_s3_4_2 (
  // Input Ports: Expanded from sample[0:15][0:1]
  input   signed [15:0] sample_4_0,  input   signed [15:0] sample_4_1,
  input   signed [15:0] sample_5_0,  input   signed [15:0] sample_5_1,
  input   signed [15:0] sample_6_0,  input   signed [15:0] sample_6_1,
  input   signed [15:0] sample_7_0,  input   signed [15:0] sample_7_1,

  // Output Ports: Expanded from sample_out[0:15][0:1]
  output reg signed [15:0] sample_out_4_0,  output reg signed [15:0] sample_out_4_1,
  output reg signed [15:0] sample_out_5_0,  output reg signed [15:0] sample_out_5_1,
  output reg signed [15:0] sample_out_6_0,  output reg signed [15:0] sample_out_6_1,
  output reg signed [15:0] sample_out_7_0,  output reg signed [15:0] sample_out_7_1
);

  // Internal State Registers (Working Variables)
  // These act as the "sample_out" array during computation
  reg signed [15:0] s4_0, s4_1;
  reg signed [15:0] s5_0, s5_1;
  reg signed [15:0] s6_0, s6_1;
  reg signed [15:0] s7_0, s7_1;

  // Temporary variables for calculations
  reg signed [15:0] tmp_real, tmp_imag, tmp_real2, tmp_imag2;
  reg signed [31:0] tmp1, tmp2;
  reg signed [15:0] tmp_sha, tmp_shb;

  // Combinational Logic Block
always @(*) begin
// Initialize internal state with inputs
s4_0 = sample_4_0;  s4_1 = sample_4_1;
s5_0 = sample_5_0;  s5_1 = sample_5_1;
s6_0 = sample_6_0;  s6_1 = sample_6_1;
s7_0 = sample_7_0;  s7_1 = sample_7_1;

// Stage 3
// (4, 6)
tmp_real = s4_0 + s6_0;   tmp_imag = s4_1 + s6_1;
s6_0 = s4_0 - s6_0;       s6_1 = s4_1 - s6_1;
s4_0 = tmp_real;          s4_1 = tmp_imag;

// (5, 7)
tmp_real = s5_0 + s7_0; tmp_imag = s5_1 + s7_1;
tmp_real2 = s5_0 - s7_0; tmp_imag2 = s5_1 - s7_1;
tmp1 = (tmp_real2 * 16'sh0000) - (tmp_imag2 * 16'shff00);
tmp2 = (tmp_real2 * 16'shff00) + (tmp_imag2 * 16'sh0000);
tmp_sha = tmp1[23:8]; tmp_shb = tmp2[23:8];
s7_0 = tmp_sha; s7_1 = tmp_shb;
s5_0 = tmp_real; s5_1 = tmp_imag;


// Stage 4
// (4, 5)
tmp_real = s4_0 + s5_0;   tmp_imag = s4_1 + s5_1;
s5_0 = s4_0 - s5_0;       s5_1 = s4_1 - s5_1;
s4_0 = tmp_real;          s4_1 = tmp_imag;

// (6, 7)
tmp_real = s6_0 + s7_0;   tmp_imag = s6_1 + s7_1;
s7_0 = s6_0 - s7_0;       s7_1 = s6_1 - s7_1;
s6_0 = tmp_real;          s6_1 = tmp_imag;

// Assign internal state to outputs
sample_out_4_0 = s4_0;  sample_out_4_1 = s4_1;
sample_out_5_0 = s5_0;  sample_out_5_1 = s5_1;
sample_out_6_0 = s6_0;  sample_out_6_1 = s6_1;
sample_out_7_0 = s7_0;  sample_out_7_1 = s7_1;
end

endmodule
