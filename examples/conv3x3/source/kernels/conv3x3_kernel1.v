module conv3x3_kernel1 (
    input  wire [7:0] a0,
    input  wire [7:0] a1,
    input  wire [7:0] a2,
    input  wire [7:0] a3,
    input  wire [7:0] b0,
    input  wire [7:0] b1,
    input  wire [7:0] b2,
    input  wire [7:0] b3,
    output wire [17:0] add5
);

  
    wire [15:0] conv0  = a0 * b0;
    wire [15:0] conv1  = a1 * b1;
    wire [15:0] conv2  = a2 * b2;
    wire [15:0] conv3  = a3 * b3;

    wire [16:0] add0 = conv0 + conv1;
    wire [16:0] add1 = conv2 + conv3;

    assign add5 = add0 + add1;

endmodule
