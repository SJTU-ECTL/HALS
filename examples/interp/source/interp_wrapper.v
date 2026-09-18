module interp_wrapper(
  input  signed [15:0] idata_0,
  input  signed [15:0] idata_1,
  input  signed [15:0] idata_2,
  input  signed [15:0] idata_3,
  input  signed [15:0] idata_4,
  input  signed [15:0] idata_5,
  input  signed [15:0] idata_6,
  input  signed [15:0] idata_7,
  output reg signed [63:0] odata_0,
  output reg signed [63:0] odata_1,
  output reg signed [63:0] odata_2,
  output reg signed [63:0] odata_3
);

wire signed [34:0] odata_k0;
wire signed [34:0] odata_k1;
wire signed [34:0] odata_k2;
wire signed [34:0] odata_k3;

interp_helper interp_helper(
  .idata0(idata_0),
  .idata1(idata_1),
  .idata2(idata_2),
  .idata3(idata_3),
  .idata4(idata_4),
  .idata5(idata_5),
  .idata6(idata_6),
  .idata7(idata_7),
  .odata0(odata_k0),
  .odata1(odata_k1),
  .odata2(odata_k2),
  .odata3(odata_k3)
);

always @(*) begin
  odata_0 = {{29{odata_k0[34]}}, odata_k0};
  odata_1 = {{29{odata_k1[34]}}, odata_k1};
  odata_2 = {{29{odata_k2[34]}}, odata_k2};
  odata_3 = {{29{odata_k3[34]}}, odata_k3};
end

endmodule
