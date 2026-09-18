module interp_helper(
  input  signed [15:0] idata0,
  input  signed [15:0] idata1,
  input  signed [15:0] idata2,
  input  signed [15:0] idata3,
  input  signed [15:0] idata4,
  input  signed [15:0] idata5,
  input  signed [15:0] idata6,
  input  signed [15:0] idata7,
  output [34:0] odata0,
  output [34:0] odata1,
  output [34:0] odata2,
  output [34:0] odata3
);

wire signed [34:0] k1_p0;
wire signed [34:0] k1_p1;
wire signed [34:0] k2_p0;
wire signed [34:0] k2_p1;
wire signed [34:0] k3_p0;
wire signed [34:0] k3_p1;

interp_k1_p0 interp_k1_p0(
  idata0,
  idata1,
  idata2,
  idata3,
  k1_p0
);

interp_k1_p1 interp_k1_p1(
  idata4,
  idata5,
  idata6,
  idata7,
  k1_p1
);

interp_k2_p0 interp_k2_p0(
  idata0,
  idata1,
  idata2,
  idata3,
  k2_p0
);

interp_k2_p1 interp_k2_p1(
  idata4,
  idata5,
  idata6,
  idata7,
  k2_p1
);

interp_k3_p0 interp_k3_p0(
  idata0,
  idata1,
  idata2,
  idata3,
  k3_p0
);

interp_k3_p1 interp_k3_p1(
  idata4,
  idata5,
  idata6,
  idata7,
  k3_p1
);

assign odata0 = k1_p0 + k1_p1;
assign odata1 = k2_p0 + k2_p1;
assign odata2 = k3_p0 + k3_p1;

interp_k4 interp_k4(
  idata0,
  idata1,
  idata2,
  idata3,
  idata4,
  idata5,
  idata6,
  idata7,
  odata3
);

endmodule
