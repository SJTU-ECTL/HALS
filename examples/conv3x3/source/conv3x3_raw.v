module conv3x3_raw (
    input  wire [7:0] a0,
    input  wire [7:0] a1,
    input  wire [7:0] a2,
    input  wire [7:0] a3,
    input  wire [7:0] a4,
    input  wire [7:0] a5,
    input  wire [7:0] a6,
    input  wire [7:0] a7,
    input  wire [7:0] a8,
    input  wire [7:0] b0,
    input  wire [7:0] b1,
    input  wire [7:0] b2,
    input  wire [7:0] b3,
    input  wire [7:0] b4,
    input  wire [7:0] b5,
    input  wire [7:0] b6,
    input  wire [7:0] b7,
    input  wire [7:0] b8,
    output reg  [19:0] result
);

  
    wire [15:0] conv0  = a0 * b0;
    wire [15:0] conv1  = a1 * b1;
    wire [15:0] conv2  = a2 * b2;
    wire [15:0] conv3  = a3 * b3;
    wire [15:0] conv4  = a4 * b4;
    wire [15:0] conv5  = a5 * b5;
    wire [15:0] conv6  = a6 * b6;
    wire [15:0] conv7  = a7 * b7;

    wire [16:0] add0 = conv0 + conv1;
    wire [16:0] add1 = conv2 + conv3;
    wire [16:0] add2 = conv4 + conv5;
    wire [16:0] add3 = conv6 + conv7;
    wire [16:0] add4 = a8 * b8;

    wire [17:0] add5 = add0 + add1;
    wire [17:0] add6 = add2 + add3;
    wire [18:0] add7 = {1'b0, add4} + add5;
    always @(*) begin
        result = {1'b0, add6} + add7;
    end

endmodule
