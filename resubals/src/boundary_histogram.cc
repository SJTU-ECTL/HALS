#include "boundary_histogram.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>


namespace {

constexpr std::array<double, 11> ErrorEdges = {
    -std::numeric_limits<double>::infinity(),
    -0.1, -0.01, -0.001, -1e-6, 0.0,
    1e-6, 0.001, 0.01, 0.1,
    std::numeric_limits<double>::infinity(),
};

constexpr std::array<double, 10> SignalEdges = {
    -std::numeric_limits<double>::infinity(),
    -0.5, -0.25, 0.0, 0.125, 0.25, 0.5, 0.75, 1.0,
    std::numeric_limits<double>::infinity(),
};

template <std::size_t N>
int Bin(const std::array<double, N>& edges, double value) {
    if (!std::isfinite(value))
        throw std::invalid_argument("conditional histogram sample must be finite");
    const auto iterator = std::upper_bound(edges.begin(), edges.end(), value);
    const auto raw = static_cast<int>(iterator - edges.begin()) - 1;
    return std::clamp(raw, 0, static_cast<int>(N) - 2);
}

template <std::size_t N>
void AppendNormalized(const std::array<std::uint64_t, N>& counts,
                      std::uint64_t total,
                      std::vector<double>& output) {
    for (const auto count: counts)
        output.push_back(static_cast<double>(count) /
                         static_cast<double>(total));
}

}


void BoundaryConditionalHistogramSketch::Add(
        double zExact, double eCurrent, double eCandidate) {
    const double delta = eCandidate - eCurrent;
    if (!std::isfinite(delta))
        throw std::invalid_argument("conditional histogram delta must be finite");
    const int deltaBin = Bin(ErrorEdges, delta);
    ++signalDelta[Bin(SignalEdges, zExact) * ErrorBins + deltaBin];
    ++currentDelta[Bin(ErrorEdges, eCurrent) * ErrorBins + deltaBin];
    ++candidateDelta[Bin(ErrorEdges, eCandidate) * ErrorBins + deltaBin];
    ++sampleCount;
}


std::vector<double> BoundaryConditionalHistogramSketch::FeatureVector() const {
    if (sampleCount == 0)
        throw std::logic_error("cannot finalize an empty conditional histogram");
    std::vector<double> result;
    result.reserve(FeatureWidth);
    AppendNormalized(signalDelta, sampleCount, result);
    AppendNormalized(currentDelta, sampleCount, result);
    AppendNormalized(candidateDelta, sampleCount, result);
    if (static_cast<int>(result.size()) != FeatureWidth)
        throw std::logic_error("conditional histogram feature width drifted");
    return result;
}
