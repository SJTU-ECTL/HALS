module study_inject20(input [19:0] x, input [3:0] mode,
        input [63:0] a,b, output reg [19:0] y);
      always @* begin
        case(mode)
          0: y=x;
          1: y=x & ~a;
          2: y=x | a;
          3: y=x ^ a;
          4: y=x & a;
          5: y=x + a;
          6: y=x >> a;
          7: y=(x*a)/(b == 0 ? 64'd1 : b);
          8: y=0;
          9: y=(x & ~a) + b;
          default: y=x;
        endcase
      end
    endmodule
