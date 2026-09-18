`include "kernels/atax_tmp_acc.v"
`include "kernels/atax_y_acc.v"

module atax_wrapper (
    input clk,
    input rst,
    input [15:0] Aij,
    input [15:0] xj,
    output reg valid_out,
    output reg [15:0] out
);
  localparam M = 38;
  localparam N = 42;
  localparam PHASE_LOAD = 2'd0;
  localparam PHASE_UPDATE = 2'd1;
  localparam PHASE_FLUSH = 2'd2;

  reg [1:0] phase;
  reg [5:0] i_idx;
  reg [5:0] j_idx;
  reg [5:0] update_idx;
  reg [5:0] flush_idx;
  reg [15:0] row_a [0:N-1];
  reg [15:0] y_reg [0:N-1];
  reg [15:0] tmp_acc;
  reg [15:0] tmp_final;
  wire [15:0] tmp_next;
  wire [15:0] y_next;
  integer k;

  atax_tmp_acc tmp_inst(.tmp_acc(tmp_acc), .Aij(Aij), .xj(xj), .tmp_next(tmp_next));
  atax_y_acc y_inst(.y_acc(y_reg[update_idx]), .Aij(row_a[update_idx]),
                    .tmp_final(tmp_final), .y_next(y_next));

  initial begin
    phase = PHASE_LOAD;
    i_idx = 0;
    j_idx = 0;
    update_idx = 0;
    flush_idx = 0;
    tmp_acc = 0;
    tmp_final = 0;
    valid_out = 0;
    out = 0;
    for (k = 0; k < N; k = k + 1) begin
      row_a[k] = 0;
      y_reg[k] = 0;
    end
  end

  always @(posedge clk) begin
    if (rst) begin
      phase <= PHASE_LOAD;
      i_idx <= 0;
      j_idx <= 0;
      update_idx <= 0;
      flush_idx <= 0;
      tmp_acc <= 0;
      tmp_final <= 0;
      valid_out <= 0;
      out <= 0;
      for (k = 0; k < N; k = k + 1) begin
        row_a[k] <= 0;
        y_reg[k] <= 0;
      end
    end else if (phase == PHASE_LOAD) begin
      valid_out <= 0;
      row_a[j_idx] <= Aij;
      tmp_acc <= tmp_next;
      if (j_idx == N - 1) begin
        tmp_final <= tmp_next;
        tmp_acc <= 0;
        j_idx <= 0;
        update_idx <= 0;
        phase <= PHASE_UPDATE;
      end else begin
        j_idx <= j_idx + 1;
      end
    end else if (phase == PHASE_UPDATE) begin
      valid_out <= 0;
      y_reg[update_idx] <= y_next;
      if (update_idx == N - 1) begin
        update_idx <= 0;
        if (i_idx == M - 1) begin
          i_idx <= 0;
          flush_idx <= 0;
          phase <= PHASE_FLUSH;
        end else begin
          i_idx <= i_idx + 1;
          phase <= PHASE_LOAD;
        end
      end else begin
        update_idx <= update_idx + 1;
      end
    end else begin
      valid_out <= 1;
      out <= y_reg[flush_idx];
      if (flush_idx == N - 1) begin
        phase <= PHASE_LOAD;
        flush_idx <= 0;
        for (k = 0; k < N; k = k + 1) begin
          y_reg[k] <= 0;
        end
      end else begin
        flush_idx <= flush_idx + 1;
      end
    end
  end
endmodule
