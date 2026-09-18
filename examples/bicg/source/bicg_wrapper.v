`include "kernels/bicg_s_acc.v"
`include "kernels/bicg_q_acc.v"

module bicg_wrapper (
    input clk,
    input rst,
    input [15:0] Aij,
    input [15:0] pj,
    input [15:0] ri,
    output reg valid_out,
    output reg [15:0] out
);
  localparam M = 38;
  localparam N = 42;
  localparam PHASE_LOAD = 1'b0;
  localparam PHASE_FLUSH = 1'b1;

  reg phase;
  reg [5:0] i_idx;
  reg [5:0] j_idx;
  reg [5:0] flush_idx;
  reg [15:0] s_reg [0:M-1];
  reg [15:0] q_acc;
  wire [15:0] s_next;
  wire [15:0] q_next;
  integer k;

  bicg_s_acc s_inst(.s_acc(s_reg[j_idx]), .Aij(Aij), .ri(ri), .s_next(s_next));
  bicg_q_acc q_inst(.q_acc(q_acc), .Aij(Aij), .pj(pj), .q_next(q_next));

  initial begin
    phase = PHASE_LOAD;
    i_idx = 0;
    j_idx = 0;
    flush_idx = 0;
    q_acc = 0;
    valid_out = 0;
    out = 0;
    for (k = 0; k < M; k = k + 1) begin
      s_reg[k] = 0;
    end
  end

  always @(posedge clk) begin
    if (rst) begin
      phase <= PHASE_LOAD;
      i_idx <= 0;
      j_idx <= 0;
      flush_idx <= 0;
      q_acc <= 0;
      valid_out <= 0;
      out <= 0;
      for (k = 0; k < M; k = k + 1) begin
        s_reg[k] <= 0;
      end
    end else if (phase == PHASE_LOAD) begin
      s_reg[j_idx] <= s_next;
      q_acc <= q_next;
      valid_out <= 0;
      if (j_idx == M - 1) begin
        valid_out <= 1;
        out <= q_next;
        q_acc <= 0;
        j_idx <= 0;
        if (i_idx == N - 1) begin
          i_idx <= 0;
          flush_idx <= 0;
          phase <= PHASE_FLUSH;
        end else begin
          i_idx <= i_idx + 1;
        end
      end else begin
        j_idx <= j_idx + 1;
      end
    end else begin
      valid_out <= 1;
      out <= s_reg[flush_idx];
      if (flush_idx == M - 1) begin
        phase <= PHASE_LOAD;
        flush_idx <= 0;
        for (k = 0; k < M; k = k + 1) begin
          s_reg[k] <= 0;
        end
      end else begin
        flush_idx <= flush_idx + 1;
      end
    end
  end
endmodule
