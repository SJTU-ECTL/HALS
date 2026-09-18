`include "kernels/conv3x3_kernel1.v"
`include "kernels/conv3x3_kernel2.v"

module conv3x3_wrapper (
    input [7:0] a0,
    input [7:0] a1,
    input [7:0] a2,
    input [7:0] a3,
    input [7:0] a4,
    input [7:0] a5,
    input [7:0] a6,
    input [7:0] a7,
    input [7:0] a8,
    input [7:0] b0,
    input [7:0] b1,
    input [7:0] b2,
    input [7:0] b3,
    input [7:0] b4,
    input [7:0] b5,
    input [7:0] b6,
    input [7:0] b7,
    input [7:0] b8,
    output reg [19:0] result
);
    wire [17:0] add5;

    conv3x3_kernel1 conv3x3_kernel1 (
        .a0(a0), .a1(a1), .a2(a2), .a3(a3),
        .b0(b0), .b1(b1), .b2(b2), .b3(b3),
        .add5(add5)
    );

    conv3x3_kernel2 conv3x3_kernel2 (
        .a4(a4), .a5(a5), .a6(a6), .a7(a7), .a8(a8),
        .b4(b4), .b5(b5), .b6(b6), .b7(b7), .b8(b8),
        .add5(add5),
        .result(result)
    );
endmodule
