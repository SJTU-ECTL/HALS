#include "transition_gmm.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <unistd.h>


int main() {
    TransitionGMMPrototypeSet prototypes;
    prototypes.dimension = 2;
    prototypes.components = 2;
    prototypes.sha256 = "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    prototypes.varianceFloor = 1e-8;
    prototypes.centers = {{-0.1, -0.2}, {0.1, 0.2}};
    std::string error;
    assert(prototypes.IsValid(error));

    TransitionGMMSketch sketch(prototypes);
    sketch.Add({0.0, 0.0});
    sketch.Add({-0.1, -0.2});
    sketch.Add({0.1, 0.2});
    sketch.Add({0.2, 0.3});
    auto record = sketch.Finalize();
    record.runId = "test-run";
    record.graphHash = "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    record.baseStateHash = "sha256:cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";
    record.candidateHash = "sha256:dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd";
    record.prototypeHash = prototypes.sha256;
    record.patternHash = "sha256:eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee";
    record.targetRegion = "region";
    record.boundaryNode = "boundary";
    record.candidateId = 0;
    record.targetNodeId = 1;
    record.outputIndex = 0;
    record.extractionMethod = "unit_test";
    record.vecbeeScalarError = 0.125;
    assert(record.patternCount == 4);
    assert(std::abs(record.zeroProbability - 0.25) < 1e-12);
    assert(record.mixture.size() == 2);
    assert(std::abs(record.mixture[0].weight + record.mixture[1].weight - 1.0)
        < 1e-12);
    assert(record.mixture[0].mean[1] - record.mixture[0].mean[0]
        <= record.mixture[1].mean[1] - record.mixture[1].mean[0]);
    const auto json = record.ToJson();
    assert(json.find("hals_lac_transition_gmm_v2") != std::string::npos);
    assert(json.find(prototypes.sha256) != std::string::npos);
    assert(json.find("\"vecbee_scalar_error\":0.125") != std::string::npos);
    assert(StableTransitionCandidateHash("same")
        == StableTransitionCandidateHash("same"));
    assert(IsSha256Identity(StableTransitionCandidateHash("same")));

    TransitionGMMPrototypeSet conditioned;
    conditioned.dimension = 3;
    conditioned.components = 1;
    conditioned.varianceFloor = 1e-8;
    conditioned.centers = {{0.5, 0.0, 0.0}};
    assert(conditioned.IsValid(error));
    TransitionGMMSketch conditionedSketch(conditioned);
    conditionedSketch.Add({0.75, 0.0, 0.0});
    const auto conditionedRecord = conditionedSketch.Finalize();
    assert(std::abs(conditionedRecord.zeroProbability - 1.0) < 1e-12);
    assert(conditionedRecord.mixture[0].weight == 0.0);

    const auto tracePath = std::filesystem::temp_directory_path()
        / ("hals_transition_trace_" + std::to_string(getpid()) + ".hvt");
    std::filesystem::remove(tracePath);
    std::string traceHash;
    int bytesPerValue = 0;
    assert(WritePackedTransitionTrace(
        tracePath.string(), 9, true, BigIntVect{-1, 0, 257},
        traceHash, bytesPerValue, error));
    assert(bytesPerValue == 2);
    assert(IsSha256Identity(traceHash));
    std::ifstream trace(tracePath, std::ios::binary);
    const std::string bytes(
        (std::istreambuf_iterator<char>(trace)),
        std::istreambuf_iterator<char>());
    assert(bytes.size() == 28 + 3 * 2);
    assert(bytes.substr(0, 8) == "HVTBIN01");
    // Payload starts at byte 28; -1 in 9-bit two's complement is 0x01ff.
    assert(static_cast<unsigned char>(bytes[28]) == 0xff);
    assert(static_cast<unsigned char>(bytes[29]) == 0x01);
    assert(static_cast<unsigned char>(bytes[32]) == 0x01);
    assert(static_cast<unsigned char>(bytes[33]) == 0x01);
    std::filesystem::remove(tracePath);
    std::cout << "transition_gmm_test: PASS\n";
    return 0;
}
