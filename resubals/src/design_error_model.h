#pragma once

#include <string>
#include <vector>


class DesignErrorModel {
private:
    enum class Backend {
        POLYNOMIAL,
        MLP_RELU,
        HALS_POWER,
    };

    struct Term {
        std::vector<int> powers;
        double coef;
    };

    struct MlpLayer {
        std::vector<std::vector<double>> weights;
        std::vector<double> bias;
        std::string activation;
    };

    Backend backend = Backend::POLYNOMIAL;
    double intercept = 0.0;
    double zeroPrediction = 0.0;
    int featureNum = 0;
    std::vector<Term> terms;
    std::vector<double> inputMean;
    std::vector<double> inputScale;
    std::vector<double> powerAlpha;
    std::vector<double> powerBeta;
    double outputMean = 0.0;
    double outputScale = 1.0;
    std::vector<MlpLayer> mlpLayers;
    std::vector<double> featureMins;
    std::vector<double> featureMaxs;
    std::vector<bool> featureUsed;
    bool loaded = false;

    double RawPredict(const std::vector<double>& features) const;
    std::vector<double> RawGradient(const std::vector<double>& features) const;

public:
    bool Load(const std::string& path);
    bool IsLoaded() const { return loaded; }
    int FeatureNum() const { return featureNum; }
    bool HasFeatureRange(int index) const;
    double FeatureMin(int index) const;
    double FeatureMax(int index) const;
    bool InFeatureRange(const std::vector<double>& features) const;
    double Predict(const std::vector<double>& features) const;
    std::vector<double> Gradient(const std::vector<double>& features) const;
};
