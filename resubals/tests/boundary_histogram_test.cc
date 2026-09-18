#include "boundary_histogram.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>


int main() {
    BoundaryConditionalHistogramSketch sketch;
    sketch.Add(0.0, 0.0, 0.0);
    sketch.Add(0.25, -0.01, 0.02);
    sketch.Add(0.75, 0.10, -0.10);
    const auto feature = sketch.FeatureVector();
    assert(sketch.SampleCount() == 3);
    assert(feature.size() == BoundaryConditionalHistogramSketch::FeatureWidth);
    double sum = 0.0;
    for (double value: feature) {
        assert(value >= 0.0 && value <= 1.0);
        sum += value;
    }
    // Three complete normalized histogram blocks are concatenated.
    assert(std::abs(sum - 3.0) < 1e-12);
    bool rejected = false;
    try {
        sketch.Add(std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0);
    }
    catch (const std::invalid_argument&) {
        rejected = true;
    }
    assert(rejected);
    std::cout << "boundary_histogram_test: PASS\n";
    return 0;
}
