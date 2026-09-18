#pragma once

#include <array>
#include <cstdint>
#include <vector>


// Experimental collision-gated representation used after the D3/K2 GMM-only
// pilot failed identifiability.  This fixed, label-free encoder concatenates
// hist(z_exact, delta), hist(e_current, delta), and
// hist(e_candidate, delta).  Each block is normalized by all policy patterns.
class BoundaryConditionalHistogramSketch {
public:
    static constexpr int SignalBins = 9;
    static constexpr int ErrorBins = 10;
    static constexpr int FeatureWidth = SignalBins * ErrorBins
        + 2 * ErrorBins * ErrorBins;

    void Add(double zExact, double eCurrent, double eCandidate);
    std::vector<double> FeatureVector() const;
    std::uint64_t SampleCount() const { return sampleCount; }

private:
    std::uint64_t sampleCount = 0;
    std::array<std::uint64_t, SignalBins * ErrorBins> signalDelta{};
    std::array<std::uint64_t, ErrorBins * ErrorBins> currentDelta{};
    std::array<std::uint64_t, ErrorBins * ErrorBins> candidateDelta{};
};
