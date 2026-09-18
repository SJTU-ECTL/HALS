#pragma once

#include "header.h"


// Frozen-prototype, fixed-cost transition-GMM support for VECBEE.  The
// prototype file is intentionally a small whitespace-delimited format so the
// C++ estimator does not need a JSON dependency:
//
//   HALS_TRANSITION_GMM_PROTOTYPES_V1 <dimension> <K> <variance_floor>
//   <center[0][0]> ... <center[0][dimension-1]>
//   ... K rows ...
//
// The online path performs one nearest-prototype assignment and accumulates
// count/sum/cross-product statistics.  It never runs candidate-specific EM.
struct TransitionGMMPrototypeSet {
    int dimension = 0;
    int components = 0;
    double varianceFloor = 1e-8;
    std::string sha256;
    std::vector<std::vector<double>> centers;

    bool Load(const std::string& path, std::string& error);
    bool IsValid(std::string& error) const;
};


struct TransitionGMMComponent {
    double weight = 0.0;
    std::vector<double> mean;
    std::vector<double> logVariance;
    // Packed upper-triangular correlations: (0,1), (0,2), ...
    std::vector<double> correlation;
};


struct TransitionGMMRecord {
    std::string runId;
    std::string graphHash;
    std::string baseStateHash;
    std::string candidateHash;
    std::string prototypeHash;
    std::string patternHash;
    std::string targetRegion;
    std::string boundaryNode;
    std::string extractionMethod =
        "vecbee_boolean_difference_fixed_prototype";
    int candidateId = -1;
    int targetNodeId = -1;
    int outputIndex = -1;
    int outputWidth = 0;
    bool outputSigned = false;
    int patternCount = 0;
    int dimension = 0;
    int components = 0;
    double zeroProbability = 0.0;
    // Existing VECBEE aggregate candidate-error estimate, normalized by the
    // metric's frame/output scale.  It is repeated on boundary-output records
    // so the scalar baseline remains candidate-aligned in the same JSONL.
    double vecbeeScalarError = 0.0;
    long long traceTimeUs = 0;
    long long sketchTimeUs = 0;
    long long packedTraceTimeUs = 0;
    // Shared pattern simulation and Boolean-difference work for the whole
    // candidate batch.  Count once per (base_state_hash, pattern_hash).
    long long sharedAnalysisTimeUs = 0;
    std::vector<TransitionGMMComponent> mixture;
    // Debug-only paired trace.  Values are decimal strings to preserve wide
    // integer outputs in JSON consumers.  Empty in the production path.
    std::vector<std::string> exactTrace;
    std::vector<std::string> currentTrace;
    std::vector<std::string> candidateTrace;
    // Phase-B-only compact sidecar for the VECBEE-predicted candidate trace.
    // The production online sketch leaves these fields empty.
    std::string packedCandidateTracePath;
    std::string packedCandidateTraceHash;
    int packedCandidateTraceBytesPerValue = 0;

    std::string ToJson() const;
};


struct TransitionGMMExportConfig {
    std::string prototypePath;
    std::string outputPath;
    std::string runId;
    std::string graphHash;
    std::string baseStateHash;
    std::string patternHash;
    std::string targetRegion;
    std::vector<std::string> boundaryNodes;
    bool includeRawTrace = false;
    std::string packedTraceDirectory;
    long long sharedAnalysisTimeUs = 0;

    bool Enabled() const {
        return !prototypePath.empty() || !outputPath.empty();
    }
};


class TransitionGMMSketch {
private:
    const TransitionGMMPrototypeSet& prototypes;
    long long totalCount = 0;
    long long zeroCount = 0;
    std::vector<long long> counts;
    std::vector<std::vector<double>> sums;
    std::vector<std::vector<std::vector<double>>> crossProducts;

public:
    explicit TransitionGMMSketch(const TransitionGMMPrototypeSet& prototypeSet);
    void Add(const std::vector<double>& value);
    TransitionGMMRecord Finalize() const;
};


std::string StableTransitionCandidateHash(const std::string& representation);
bool IsSha256Identity(const std::string& value);
bool WriteTransitionGMMJsonl(
    const std::string& path,
    const std::vector<std::vector<TransitionGMMRecord>>& records,
    bool append,
    std::string& error
);

bool WritePackedTransitionTrace(
    const std::string& path,
    int outputWidth,
    bool outputSigned,
    const BigIntVect& values,
    std::string& sha256,
    int& bytesPerValue,
    std::string& error
);
