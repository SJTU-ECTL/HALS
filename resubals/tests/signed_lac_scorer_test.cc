#include "signed_lac_scorer.h"

#include <cassert>
#include <cmath>
#include <iostream>


TransitionGMMRecord MakeRecord(
    int candidateId,
    const std::string& graphHash,
    const std::string& prototypeHash,
    double p0,
    double currentMean,
    double candidateMean
) {
    TransitionGMMRecord record;
    record.candidateId = candidateId;
    record.graphHash = graphHash;
    record.prototypeHash = prototypeHash;
    record.targetRegion = "r0";
    record.boundaryNode = "boundary";
    record.zeroProbability = p0;
    record.dimension = 2;
    record.components = 1;
    TransitionGMMComponent component;
    component.weight = 1.0;
    component.mean = {currentMean, candidateMean};
    component.logVariance = {-8.0, -7.0};
    component.correlation = {0.2};
    record.mixture.emplace_back(component);
    return record;
}


int main(int argc, char** argv) {
    assert(argc == 9);
    SignedLACScorerConfig config;
    config.modelPath = argv[1];
    config.graphTensorPath = argv[2];
    config.modelHash = argv[3];
    config.graphHash = argv[4];
    config.graphTensorHash = argv[5];
    config.prototypeHash = argv[6];
    config.metric = "NMED";
    config.targetRegion = "r0";
    config.batchSize = 1;
    config.torchThreads = 1;
    SignedLACScorer scorer;
    std::string error;
    assert(SignedLACScorer::BuiltWithLibTorch());
    if (!scorer.Configure(config, error)) {
        std::cerr << error << std::endl;
        return 1;
    }
    std::vector<std::vector<TransitionGMMRecord>> records(2);
    records[0].emplace_back(MakeRecord(
        0, config.graphHash, config.prototypeHash, 0.25, 0.01, 0.02));
    records[1].emplace_back(MakeRecord(
        1, config.graphHash, config.prototypeHash, 0.50, -0.03, 0.04));
    std::vector<double> predictions;
    if (!scorer.Score(records, 2, predictions, error)) {
        std::cerr << error << std::endl;
        return 1;
    }
    assert(predictions.size() == 2);
    const double expected0 = std::stod(argv[7]);
    const double expected1 = std::stod(argv[8]);
    assert(std::abs(predictions[0] - expected0) <= 1e-6);
    assert(std::abs(predictions[1] - expected1) <= 1e-6);
    std::cout << "signed_lac_scorer_test: PASS "
              << predictions[0] << " " << predictions[1] << std::endl;
    return 0;
}
