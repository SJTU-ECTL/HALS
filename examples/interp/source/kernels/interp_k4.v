module interp_k4 (
  input signed [15:0] idata0,
  input signed [15:0] idata1,
  input signed [15:0] idata2,
  input signed [15:0] idata3,
  input signed [15:0] idata4,
  input signed [15:0] idata5,
  input signed [15:0] idata6,
  input signed [15:0] idata7,
  output reg signed [34:0] odata3
);

// fixed coefficients (Q14.2, from interp.cpp) - k4 has 7 coeffs
localparam signed [15:0] C0 = 16'shFFFF;  // -1
localparam signed [15:0] C1 = 16'sh0000;  // 0
localparam signed [15:0] C2 = 16'shFFFF;  // -1
localparam signed [15:0] C3 = 16'sh8000;  // -32768
localparam signed [15:0] C4 = 16'shFFFF;  // -1
localparam signed [15:0] C5 = 16'sh0000;  // 0
localparam signed [15:0] C6 = 16'shFFFF;  // -1

reg signed [31:0] sop4_0;
reg signed [31:0] sop4_1;
reg signed [31:0] sop4_2;
reg signed [31:0] sop4_3;
reg signed [31:0] sop4_4;
reg signed [31:0] sop4_5;
reg signed [31:0] sop4_6;

reg signed [32:0] sop4_sum1;
reg signed [32:0] sop4_sum2;
reg signed [32:0] sop4_sum3;
reg signed [32:0] sop4_sum4;

reg signed [33:0] sop4_sum5;
reg signed [33:0] sop4_sum6;

always @(*) begin
sop4_0 = idata0 * C0;
sop4_1 = idata1 * C1;
sop4_2 = idata2 * C2;
sop4_3 = idata3 * C3;
sop4_4 = idata4 * C4;
sop4_5 = idata5 * C5;
sop4_6 = idata6 * C6;
// idata7 is unused for k4 (only 7 taps)

sop4_sum1 = sop4_0 + sop4_1;
sop4_sum2 = sop4_2 + sop4_3;
sop4_sum3 = sop4_4 + sop4_5;
sop4_sum4 = {sop4_6[31], sop4_6};

sop4_sum5 = sop4_sum1 + sop4_sum2;
sop4_sum6 = sop4_sum3 + sop4_sum4;

odata3 = sop4_sum5 + sop4_sum6;
end
endmodule
