import os
import json
import subprocess
import re
import csv
import argparse
import shutil
import glob
import sys
import shlex


SIMULATION_WORD_BITS = 64


def count_pattern_lines(pattern_path):
    if not os.path.exists(pattern_path):
        return None
    with open(pattern_path, "r") as f:
        return sum(1 for line in f if line.strip())


def compute_output_num(config):
    """Compute outputNum from JSON IO section: number of output ports.
    
    ResubALS uses outputNum to divide total PO bits into individual values.
    Each output port (direction='output') is one multi-bit value.
    """
    io = config.get("IO", {})
    count = sum(1 for name, port in io.items() if port.get("direction") == "output")
    return count if count > 0 else 1


def find_blif(config_path, config):
    """Find the BLIF file for the given config.
    
    Priority:
    1. --blif CLI arg (handled in caller)
    2. config['blifPath'] field
    3. config['module'] + '.blif' in same dir as JSON
    4. config['module'] + '.blif' in 'blif/' subdir relative to JSON
    5. *.blif in 'blif/' subdir (first match)
    """
    config_dir = os.path.dirname(os.path.abspath(config_path))
    module = config.get("module", "")

    # Check for explicit blifPath in config
    blif_path = config.get("blifPath", "")
    if blif_path:
        abs_blif = os.path.join(config_dir, blif_path) if not os.path.isabs(blif_path) else blif_path
        if os.path.exists(abs_blif):
            return abs_blif

    # Try module name in same dir
    if module:
        candidate = os.path.join(config_dir, module + ".blif")
        if os.path.exists(candidate):
            return candidate

    # Try module name in blif/ subdir
    blif_dir = os.path.join(config_dir, "blif")
    if os.path.isdir(blif_dir):
        if module:
            candidate = os.path.join(blif_dir, module + ".blif")
            if os.path.exists(candidate):
                return candidate
        # Fallback: any .blif in blif/ dir
        blifs = glob.glob(os.path.join(blif_dir, "*.blif"))
        if blifs:
            return blifs[0]

    return None


def find_pattern(pattern_file, config_path, examples_dir):
    """Find the simulation pattern file."""
    if not pattern_file:
        return None, None
    config_dir = os.path.dirname(os.path.abspath(config_path))
    candidates = [
        os.path.join(config_dir, pattern_file),
        os.path.join(config_dir, "..", pattern_file),
        os.path.join(examples_dir, pattern_file),
        pattern_file,  # absolute path
    ]
    for path in candidates:
        if os.path.exists(path):
            return path, count_pattern_lines(path)
    return None, None


def main():
    parser = argparse.ArgumentParser(description="Run ResubALS on a kernel.")
    parser.add_argument("--config", required=True, help="Path to JSON config file.")
    parser.add_argument("--blif", default=None, help="Path to BLIF netlist (auto-detected if not given).")
    parser.add_argument("--examples-dir", default=None, help="Path to HALS repository root or examples directory.")
    parser.add_argument("--working-dir", default=None, help="Working directory for ResubALS execution.")
    parser.add_argument("--nthread", type=int, default=16, help="Number of threads.")
    parser.add_argument("--seed", type=int, default=None, help="ResubALS random seed.")
    parser.add_argument("--max-cand-resub", type=int, default=None, help="Max candidate app-resubs.")
    parser.add_argument("--max-exact-cand-validate", type=int, default=None, help="Max exact simulated candidate validations per ALS round.")
    parser.add_argument("--candidate-audit-dir", default=None, help="Write-only candidate audit directory.")
    parser.add_argument("--candidate-audit-limit", type=int, default=None, help="Maximum audited candidates per round.")
    parser.add_argument("--publication-candidate-dir", default=None, help="Hash-ordered pre-estimation candidate BLIF pool directory.")
    parser.add_argument("--publication-candidate-limit", type=int, default=None, help="Maximum publication candidates exported per round.")
    parser.add_argument("--transition-gmm-prototypes", default=None,
                        help="Frozen transition-GMM prototype text file.")
    parser.add_argument("--transition-gmm-output", default=None,
                        help="VECBEE transition-GMM JSONL output.")
    parser.add_argument("--transition-gmm-graph-hash", default=None)
    parser.add_argument("--transition-gmm-base-state-hash", default=None)
    parser.add_argument("--transition-gmm-pattern-hash", default=None)
    parser.add_argument("--transition-gmm-target-region", default=None)
    parser.add_argument("--transition-gmm-boundary-nodes", default=None,
                        help="Comma-separated typed-IR boundary node ids.")
    parser.add_argument("--transition-gmm-policy-pattern-file", default=None)
    parser.add_argument("--transition-gmm-policy-frames", type=int, default=None)
    parser.add_argument("--transition-gmm-include-raw-trace", action="store_true")
    parser.add_argument("--transition-gmm-packed-trace-dir", default=None)
    parser.add_argument("--error-bounds", default=None, help="Comma-separated tight-to-loose bounds overriding config errorBounds/errorBound.")
    parser.add_argument(
        "--candidate-validation-policy",
        choices=("size_gain", "complete_size_gain", "stratified", "gradient", "gradient_stratified", "mlp_guarded"),
        default=None,
    )
    parser.add_argument(
        "--candidate-feature-source",
        choices=("simulation", "vecbee", "vecbee_shadow"),
        default=None,
        help="Candidate full-feature source used by ResubALS guard validation.",
    )
    parser.add_argument(
        "--early-exit-policy",
        choices=("legacy_scalar", "disabled"),
        default=None,
        help="VECBEE early-exit policy.",
    )
    parser.add_argument("--max-round", type=int, default=None, help="Max ALS rounds (0=unlimited).")
    parser.add_argument("--max-zero-error-fallback-rounds", type=int, default=None, help="Max consecutive 0-error fallback rounds.")
    parser.add_argument("--probe-round", type=int, default=None, help="Pause after this ALS round for a target decision (0 disables).")
    parser.add_argument("--probe-summary", default=None, help="Probe summary JSON path; relative paths are resolved beside the config.")
    parser.add_argument("--target-decision", default=None, help="Target decision JSON path; relative paths are resolved beside the config.")
    parser.add_argument("--probe-timeout-sec", type=float, default=None, help="Decision wait timeout; 0 waits indefinitely.")
    parser.add_argument("--probe-poll-ms", type=int, default=None, help="Decision polling interval in milliseconds.")
    parser.add_argument("--run-id", default=None, help="Optional reproducible run identifier for decision matching and output isolation.")
    parser.add_argument(
        "--n-frame",
        type=int,
        default=None,
        help="Number of simulation patterns; SELF keeps the exact count and UNIF rounds up to a 64-frame word.",
    )
    parser.add_argument(
        "--search-frames", type=int, default=None,
        help="Number of patterns used only for AppResub candidate generation.",
    )
    parser.add_argument(
        "--distr-type", choices=("SELF", "UNIF"), default=None,
        help="Override the config distribution for partitioned campaigns.",
    )
    parser.add_argument("--allow-partial-results", action="store_true", help="Copy an existing Pareto CSV even when ResubALS exits non-zero.")
    args = parser.parse_args()

    json_path = os.path.abspath(args.config)
    if not os.path.exists(json_path):
        raise FileNotFoundError(f"JSON config not found: {json_path}")

    config_dir = os.path.dirname(json_path)

    # Determine examples directory
    if args.examples_dir:
        examples_dir = os.path.abspath(args.examples_dir)
    else:
        # Guess: go up from config_dir until we find 'examples' or repo root
        d = config_dir
        while d != "/":
            if os.path.basename(d) == "examples" or os.path.exists(os.path.join(d, "examples")):
                examples_dir = d if os.path.basename(d) == "examples" else os.path.join(d, "examples")
                break
            d = os.path.dirname(d)
        else:
            examples_dir = config_dir  # fallback

    # Read JSON config
    with open(json_path, "r") as f:
        config = json.load(f)

    module = config["module"]
    error_type = config["errorType"]
    error_bounds = config.get("errorBounds", [])
    error_bound = config.get("errorBound", error_bounds[0] if error_bounds else None)
    if error_bound is None:
        raise ValueError("config must provide errorBound or errorBounds")
    if args.error_bounds:
        error_bounds = [float(value) for value in args.error_bounds.split(",") if value.strip()]
    if error_bounds:
        error_bounds = [float(value) for value in error_bounds]
        if any(error_bounds[i] < error_bounds[i - 1] for i in range(1, len(error_bounds))):
            raise ValueError(f"errorBounds must be tight-to-loose: {error_bounds}")
        error_bound = error_bounds[0]
    is_signed = config.get("isSigned", False)
    design_metric_weight = config.get("designMetricWeight", 1.0)
    design_error_bounds = config.get(
        "designErrUppBounds",
        config.get("designTargetBounds", []),
    )
    design_error_bound = config.get(
        "designErrUppBound",
        design_error_bounds[0] if design_error_bounds else None,
    )
    design_model_path = config.get("designModelPath", "")
    feature_indices = config.get("featureIndices", [])
    feature_metrics = config.get("featureMetrics", [])
    initial_feature_vector = config.get("initialFeatureVector", [])
    model_safety_margin = config.get("modelSafetyMargin", 0.0)
    scalar_kernel_feature = config.get("scalarKernelFeature", False)
    max_zero_error_fallback_rounds = config.get("maxZeroErrorFallbackRounds", None)
    max_cand_resub = config.get("maxCandResub", None)
    max_exact_cand_validate = config.get("maxExactCandValidate", None)
    candidate_validation_policy = (
        args.candidate_validation_policy
        if args.candidate_validation_policy is not None
        else config.get("candidateValidationPolicy", "size_gain")
    )
    candidate_feature_source = (
        args.candidate_feature_source
        if args.candidate_feature_source is not None
        else config.get("candidateFeatureSource", "simulation")
    )
    early_exit_policy = (
        args.early_exit_policy
        if args.early_exit_policy is not None
        else config.get("earlyExitPolicy", "disabled")
    )
    if early_exit_policy not in {"legacy_scalar", "disabled"}:
        raise ValueError(f"unknown earlyExitPolicy: {early_exit_policy}")
    enable_fast_err_est = config.get("enableFastErrEst", False)
    pattern_file = config.get("patternFile", "")
    distr_type = (args.distr_type or str(config.get(
        "distrType", "SELF" if pattern_file else "UNIF"))).upper()
    n_frame = args.n_frame if args.n_frame is not None else config.get("nFrame")
    source_seed = args.seed if args.seed is not None else config.get("seed", None)
    probe_round = args.probe_round if args.probe_round is not None else int(config.get("probeRound", 0))
    probe_summary = args.probe_summary if args.probe_summary is not None else config.get("probeSummaryPath", "")
    target_decision = args.target_decision if args.target_decision is not None else config.get("targetDecisionPath", "")
    probe_timeout_sec = (
        args.probe_timeout_sec if args.probe_timeout_sec is not None else float(config.get("probeTimeoutSec", 0.0))
    )
    probe_poll_ms = args.probe_poll_ms if args.probe_poll_ms is not None else int(config.get("probePollMs", 100))
    run_id = args.run_id if args.run_id is not None else str(config.get("runId", ""))
    max_round = args.max_round if args.max_round is not None else config.get("maxRound", None)

    if probe_round < 0 or probe_timeout_sec < 0 or probe_poll_ms <= 0:
        raise ValueError("probeRound/probeTimeoutSec must be non-negative and probePollMs must be positive")
    if probe_round > 0 and max_round is not None and int(max_round) > 0 and probe_round > int(max_round):
        raise ValueError("probeRound cannot exceed a finite maxRound")
    if run_id and not re.fullmatch(r"[A-Za-z0-9_.-]+", run_id):
        raise ValueError("runId may contain only letters, digits, '.', '_' and '-'")
    if probe_round > 0 and source_seed is None:
        source_seed = 1
        print("Probe mode requires paired reproducibility; defaulting the unspecified seed to 1")

    def resolve_config_relative(path):
        if not path:
            return ""
        return path if os.path.isabs(path) else os.path.abspath(os.path.join(config_dir, path))

    probe_summary = resolve_config_relative(probe_summary)
    target_decision = resolve_config_relative(target_decision)

    # Compute outputNum from IO if not explicitly set
    output_num = config.get("outputNum", None)
    if output_num is None:
        output_num = compute_output_num(config)
        print(f"Auto-computed outputNum = {output_num} from IO configuration")
    output_widths = config.get("outputWidths", [])

    # Find BLIF file
    blif_path = args.blif
    if not blif_path:
        blif_path = find_blif(json_path, config)
    if not blif_path or not os.path.exists(blif_path):
        raise FileNotFoundError(
            f"BLIF file not found. Module='{module}', config_dir='{config_dir}'. "
            f"Please specify --blif or add 'blifPath' to the JSON config."
        )
    print(f"Using BLIF: {blif_path}")

    # Find pattern file
    pattern_path = None
    if pattern_file:
        pattern_path, detected_n_frame = find_pattern(pattern_file, json_path, examples_dir)
        if pattern_path:
            print(f"Using pattern: {pattern_path}")
            if detected_n_frame:
                if n_frame is None:
                    n_frame = detected_n_frame
                elif n_frame > detected_n_frame:
                    print(
                        f"WARNING: requested {n_frame} frames but pattern has {detected_n_frame}; "
                        "using the available frames"
                    )
                    n_frame = detected_n_frame
        else:
            print(f"WARNING: pattern file '{pattern_file}' not found, will use UNIF distribution")
            distr_type = "UNIF"

    if n_frame is not None:
        if n_frame <= 0:
            raise ValueError(f"nFrame must be positive, got {n_frame}")
        if distr_type == "UNIF" and n_frame % SIMULATION_WORD_BITS:
            aligned_n_frame = ((n_frame + SIMULATION_WORD_BITS - 1) // SIMULATION_WORD_BITS) * SIMULATION_WORD_BITS
            print(
                f"WARNING: aligning uniform nFrame from {n_frame} to {aligned_n_frame}; "
                f"ResubALS processes {SIMULATION_WORD_BITS}-frame words"
            )
            n_frame = aligned_n_frame

    # Setup working directory and output paths
    if args.working_dir:
        working_dir = os.path.abspath(args.working_dir)
    else:
        working_dir = examples_dir if os.path.isdir(examples_dir) else config_dir

    # Output path for ALS results
    results_dir = os.path.join(config_dir, "als_results")
    outp_path = os.path.join(results_dir, module, run_id) if run_id else os.path.join(results_dir, module)
    os.makedirs(results_dir, exist_ok=True)

    # Build command
    resubals_bin = os.environ.get("RESUBALS_BIN", "")
    if resubals_bin:
        resubals_bin = os.path.abspath(resubals_bin)
        if not os.path.exists(resubals_bin):
            raise FileNotFoundError(f"RESUBALS_BIN does not exist: {resubals_bin}")
    else:
        resubals_bin = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "resubals.out")
        if not os.path.exists(resubals_bin):
            resubals_bin = os.path.join(examples_dir, "..", "resubals.out")

    command = [
        resubals_bin,
        "--accCirc", blif_path,
        "--standCell", os.path.join(examples_dir, "nangate_45nm_typ.lib"),
        "--outpPath", outp_path,
        "--metrType", str(error_type),
        "--distrType", distr_type,
        "--errUppBound", str(error_bound),
        "--designMetricWeight", str(design_metric_weight),
        "--outputNum", str(output_num),
        "--nThread", str(args.nthread),
    ]
    if error_bounds:
        command.extend(("--errUppBounds", ",".join(str(value) for value in error_bounds)))
    if design_model_path and design_error_bound is not None:
        command.extend(("--designErrUppBound", str(design_error_bound)))
    if design_model_path and design_error_bounds:
        command.extend((
            "--designErrUppBounds",
            ",".join(str(value) for value in design_error_bounds),
        ))
    command.extend(("--candidateValidationPolicy", candidate_validation_policy))
    command.extend(("--candidateFeatureSource", candidate_feature_source))
    command.extend(("--earlyExitPolicy", str(early_exit_policy)))
    if output_widths:
        command.extend(("--outputWidths", ",".join(str(x) for x in output_widths)))
    if design_model_path:
        if not os.path.isabs(design_model_path):
            design_model_path = os.path.abspath(os.path.join(config_dir, design_model_path))
        command.extend(("--designModelPath", design_model_path))
        command.extend(("--featureIndices", ",".join(str(x) for x in feature_indices)))
        if feature_metrics:
            command.extend(("--featureMetrics", ",".join(str(x) for x in feature_metrics)))
        command.extend(("--initialFeatureVector", ",".join(str(x) for x in initial_feature_vector)))
        command.extend(("--modelSafetyMargin", str(model_safety_margin)))
        if scalar_kernel_feature:
            command.append("--scalarKernelFeature")
    if is_signed:
        command.append("--isSigned")
    if enable_fast_err_est:
        command.append("--enableFastErrEst")
    if pattern_path:
        command.extend(("--patternFile", pattern_path))
    if n_frame is not None:
        command.extend(("--nFrame", str(n_frame)))
    if args.search_frames is not None:
        if args.search_frames <= 0:
            raise ValueError("search frames must be positive")
        command.extend(("--nFrame4ResubGen", str(args.search_frames)))
    if source_seed is not None:
        command.extend(("--seed", str(source_seed)))
    if args.max_cand_resub is not None:
        command.extend(("--maxCandResub", str(args.max_cand_resub)))
    elif max_cand_resub is not None:
        command.extend(("--maxCandResub", str(max_cand_resub)))
    if args.max_exact_cand_validate is not None:
        command.extend(("--maxExactCandValidate", str(args.max_exact_cand_validate)))
    elif max_exact_cand_validate is not None:
        command.extend(("--maxExactCandValidate", str(max_exact_cand_validate)))
    if args.candidate_audit_dir:
        command.extend(("--candidateAuditDir", os.path.abspath(args.candidate_audit_dir)))
    if args.candidate_audit_limit is not None:
        command.extend(("--candidateAuditLimit", str(args.candidate_audit_limit)))
    if args.publication_candidate_dir:
        command.extend(("--publicationCandidateDir", os.path.abspath(args.publication_candidate_dir)))
    if args.publication_candidate_limit is not None:
        command.extend(("--publicationCandidateLimit", str(args.publication_candidate_limit)))
    transition_values = (
        ("transitionGMMPrototypes", args.transition_gmm_prototypes),
        ("transitionGMMOutput", args.transition_gmm_output),
        ("transitionGMMGraphHash", args.transition_gmm_graph_hash),
        ("transitionGMMBaseStateHash", args.transition_gmm_base_state_hash),
        ("transitionGMMPatternHash", args.transition_gmm_pattern_hash),
        ("transitionGMMTargetRegion", args.transition_gmm_target_region),
        ("transitionGMMBoundaryNodes", args.transition_gmm_boundary_nodes),
        ("transitionGMMPolicyPatternFile",
         args.transition_gmm_policy_pattern_file),
        ("transitionGMMPolicyFrames", args.transition_gmm_policy_frames),
        ("transitionGMMPackedTraceDir", args.transition_gmm_packed_trace_dir),
    )
    for option, value in transition_values:
        if value is not None:
            command.extend((f"--{option}", str(value)))
    if args.transition_gmm_include_raw_trace:
        command.append("--transitionGMMIncludeRawTrace")
    if run_id:
        command.extend(("--runId", run_id))
    if max_round is not None:
        command.extend(("--maxRound", str(max_round)))
    if args.max_zero_error_fallback_rounds is not None:
        command.extend(("--maxZeroErrorFallbackRounds", str(args.max_zero_error_fallback_rounds)))
    elif max_zero_error_fallback_rounds is not None:
        command.extend(("--maxZeroErrorFallbackRounds", str(max_zero_error_fallback_rounds)))
    if probe_round > 0:
        command.extend(("--probeRound", str(probe_round)))
        if probe_summary:
            command.extend(("--probeSummaryPath", probe_summary))
        if target_decision:
            command.extend(("--targetDecisionPath", target_decision))
        command.extend(("--probeTimeoutSec", str(probe_timeout_sec)))
        command.extend(("--probePollMs", str(probe_poll_ms)))
    command_str = shlex.join(command)
    print(f"Command: {command_str}")
    print(f"Working directory: {working_dir}")

    # Execute
    result_stem = f"{module}.{run_id}" if run_id else module
    log_file = os.path.join(results_dir, f"{result_stem}.log")
    with open(log_file, "w") as log:
        log.write(f"Command: {command_str}\n")
        log.write(f"Working dir: {working_dir}\n")
        log.write(f"Config: {json_path}\n")
        log.write(f"BLIF: {blif_path}\n")
        log.write("=" * 70 + "\n\n")
        log.flush()
        process = subprocess.run(
            command,
            shell=False,
            stdout=log,
            stderr=subprocess.STDOUT,
            cwd=working_dir,
        )

    # Copy Pareto CSV
    pareto_file = os.path.join(outp_path, "pareto.csv")
    csv_file = os.path.join(results_dir, f"{result_stem}.csv")
    if os.path.exists(pareto_file):
        if process.returncode != 0:
            if not args.allow_partial_results:
                print(f"Command failed with return code {process.returncode}. Check log: {log_file}")
                with open(log_file, "r") as log:
                    lines = log.readlines()
                    for line in lines[-20:]:
                        print(f"  [log] {line.rstrip()}")
                sys.exit(process.returncode)
            print(f"WARNING: ResubALS exited with return code {process.returncode}, but Pareto front exists; continuing with partial results.")
            with open(log_file, "r") as log:
                lines = log.readlines()
                for line in lines[-20:]:
                    print(f"  [log] {line.rstrip()}")
        shutil.copyfile(pareto_file, csv_file)
        trajectory_file = os.path.join(outp_path, "trajectory.csv")
        if os.path.exists(trajectory_file):
            shutil.copyfile(trajectory_file, os.path.join(results_dir, f"{result_stem}.trajectory.csv"))
        trajectory_jsonl = os.path.join(outp_path, "trajectory.jsonl")
        if os.path.exists(trajectory_jsonl):
            shutil.copyfile(trajectory_jsonl, os.path.join(results_dir, f"{result_stem}.trajectory.jsonl"))
        phases_file = os.path.join(outp_path, "bound_phases.csv")
        if os.path.exists(phases_file):
            shutil.copyfile(phases_file, os.path.join(results_dir, f"{result_stem}.bound_phases.csv"))
        for phase_file in glob.glob(os.path.join(outp_path, "pareto_phase_*.csv")):
            suffix = os.path.basename(phase_file).removeprefix("pareto_")
            shutil.copyfile(phase_file, os.path.join(results_dir, f"{result_stem}.{suffix}"))
        for artifact in ("probe_summary.json", "target_decision_applied.json"):
            artifact_path = os.path.join(outp_path, artifact)
            if os.path.exists(artifact_path):
                shutil.copyfile(artifact_path, os.path.join(results_dir, f"{result_stem}.{artifact}"))
        if probe_summary and os.path.exists(probe_summary):
            copied_summary = os.path.join(results_dir, f"{result_stem}.probe_summary.json")
            if os.path.abspath(probe_summary) != os.path.abspath(copied_summary):
                shutil.copyfile(probe_summary, copied_summary)
        print(f"Pareto front saved to {csv_file}")
        return

    if process.returncode != 0:
        print(f"Command failed with return code {process.returncode}. Check log: {log_file}")
        # Print last 20 lines of log for quick diagnostics
        with open(log_file, "r") as log:
            lines = log.readlines()
            for line in lines[-20:]:
                print(f"  [log] {line.rstrip()}")
        sys.exit(process.returncode)

    # Legacy fallback
    area_delay_pattern = r"current best: area = ([\d.]+), delay = ([\d.]+)"
    results = []
    with open(log_file, "r") as log:
        for line in log:
            match = re.search(area_delay_pattern, line)
            if match:
                results.append({"area": float(match.group(1)), "delay": float(match.group(2))})
    if results:
        with open(csv_file, "w", newline="") as csvfile:
            writer = csv.DictWriter(csvfile, fieldnames=["area", "delay"])
            writer.writeheader()
            writer.writerows(results)
        print(f"Results saved to {csv_file}")
    else:
        print("No Pareto CSV or legacy results found. Check the log file.")

    print(f"ALS completed. Results in: {results_dir}")


if __name__ == "__main__":
    main()
