#pragma once

#include "transition_gmm.h"

#include <memory>
#include <string>
#include <vector>


struct SignedLACScorerConfig {
    std::string modelPath;
    std::string graphTensorPath;
    std::string modelHash;
    std::string graphHash;
    std::string graphTensorHash;
    std::string prototypeHash;
    std::string metric;
    std::string targetRegion;
    std::string wrapperHash;
    std::string irHash;
    std::string truthPatternHash;
    std::string executionBackend;
    double mapeBound = -1.0;
    double nmedBound = -1.0;
    double nmseBound = -1.0;
    double mseBound = -1.0;
    // Whole-design metric state is kept in the checkpoint metric's own unit.
    // The exact controller updates qCurrent after each accepted candidate.
    double qCurrent = -1.0;
    double qMax = -1.0;
    std::string controllerDirectory;
    int topB = 8;
    double decisionTimeoutSeconds = 600.0;
    int decisionPollMilliseconds = 100;
    int batchSize = 256;
    int torchThreads = 1;

    bool Enabled() const { return !modelPath.empty() || !graphTensorPath.empty(); }
};


class SignedLACScorer {
private:
    struct Impl;
    std::unique_ptr<Impl> impl;

public:
    SignedLACScorer();
    ~SignedLACScorer();
    SignedLACScorer(const SignedLACScorer&) = delete;
    SignedLACScorer& operator=(const SignedLACScorer&) = delete;

    bool Configure(const SignedLACScorerConfig& config, std::string& error);
    bool Score(
        const std::vector<std::vector<TransitionGMMRecord>>& records,
        int candidateCount,
        std::vector<double>& predictions,
        std::string& error
    );
    bool IsReady() const;
    static bool BuiltWithLibTorch();
};
