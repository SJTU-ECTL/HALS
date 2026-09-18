#include "design_error_model.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>


using namespace std;


static string ReadTextFile(const string& path) {
    ifstream in(path);
    if (!in.good())
        return "";
    ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}


enum class TopLevelStringStatus {
    MISSING,
    PARSED,
    INVALID,
};


static bool ParseJsonStringToken(
    const string& text,
    size_t begin,
    string& value,
    size_t& end
) {
    if (begin >= text.size() || text[begin] != '"')
        return false;
    value.clear();
    bool escaped = false;
    for (size_t pos = begin + 1; pos < text.size(); ++pos) {
        const char ch = text[pos];
        if (escaped) {
            // prediction_semantics and its key are intentionally restricted to
            // their canonical, unescaped spelling.  Other JSON string tokens
            // do not need to be decoded while locating the top-level field.
            value.push_back('\\');
            value.push_back(ch);
            escaped = false;
        }
        else if (ch == '\\')
            escaped = true;
        else if (ch == '"') {
            end = pos + 1;
            return true;
        }
        else
            value.push_back(ch);
    }
    return false;
}


static TopLevelStringStatus ParseTopLevelStringAfterKey(
    const string& text,
    const string& key,
    string& value
) {
    int objectDepth = 0;
    int arrayDepth = 0;
    bool found = false;
    size_t pos = 0;
    while (pos < text.size()) {
        const char ch = text[pos];
        if (ch == '"') {
            string token;
            size_t tokenEnd = pos;
            if (!ParseJsonStringToken(text, pos, token, tokenEnd))
                return TopLevelStringStatus::INVALID;
            if (objectDepth == 1 && arrayDepth == 0) {
                size_t after = tokenEnd;
                while (after < text.size() &&
                       isspace(static_cast<unsigned char>(text[after])))
                    ++after;
                if (after < text.size() && text[after] == ':' && token == key) {
                    if (found)
                        return TopLevelStringStatus::INVALID;
                    found = true;
                    ++after;
                    while (after < text.size() &&
                           isspace(static_cast<unsigned char>(text[after])))
                        ++after;
                    string parsedValue;
                    size_t valueEnd = after;
                    if (!ParseJsonStringToken(text, after, parsedValue, valueEnd))
                        return TopLevelStringStatus::INVALID;
                    value = parsedValue;
                }
            }
            pos = tokenEnd;
            continue;
        }
        if (ch == '{')
            ++objectDepth;
        else if (ch == '}') {
            --objectDepth;
            if (objectDepth < 0)
                return TopLevelStringStatus::INVALID;
        }
        else if (ch == '[')
            ++arrayDepth;
        else if (ch == ']') {
            --arrayDepth;
            if (arrayDepth < 0)
                return TopLevelStringStatus::INVALID;
        }
        ++pos;
    }
    if (objectDepth != 0 || arrayDepth != 0)
        return TopLevelStringStatus::INVALID;
    return found? TopLevelStringStatus::PARSED: TopLevelStringStatus::MISSING;
}


static bool ParseNumberAfterKey(const string& text, const string& key, double& value) {
    size_t pos = text.find("\"" + key + "\"");
    if (pos == string::npos)
        return false;
    pos = text.find(':', pos);
    if (pos == string::npos)
        return false;
    ++pos;
    const char* begin = text.c_str() + pos;
    char* end = nullptr;
    value = strtod(begin, &end);
    return end != begin;
}

static void SkipWhitespace(const string& text, size_t& pos) {
    while (pos < text.size() && isspace(static_cast<unsigned char>(text[pos])))
        ++pos;
}


static string ParseJsonStringValue(const string& text) {
    size_t end = 0;
    string value;
    size_t begin = 0;
    SkipWhitespace(text, begin);
    if (!ParseJsonStringToken(text, begin, value, end))
        return "";
    size_t trailing = end;
    SkipWhitespace(text, trailing);
    if (trailing != text.size())
        return "";
    return value;
}


static bool FindMatchingBracket(
    const string& text,
    size_t begin,
    size_t& end
) {
    if (begin >= text.size())
        return false;
    const char open = text[begin];
    const char close = open == '{'? '}': (open == '['? ']': '\0');
    if (close == '\0')
        return false;
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (size_t pos = begin; pos < text.size(); ++pos) {
        const char ch = text[pos];
        if (inString) {
            if (escaped)
                escaped = false;
            else if (ch == '\\')
                escaped = true;
            else if (ch == '"')
                inString = false;
            continue;
        }
        if (ch == '"') {
            inString = true;
            continue;
        }
        if (ch == open)
            ++depth;
        else if (ch == close) {
            --depth;
            if (depth == 0) {
                end = pos;
                return true;
            }
        }
    }
    return false;
}


static string ParseJsonValueAfterKey(const string& text, const string& key) {
    size_t pos = text.find("\"" + key + "\"");
    if (pos == string::npos)
        return "";
    pos = text.find(':', pos);
    if (pos == string::npos)
        return "";
    ++pos;
    SkipWhitespace(text, pos);
    if (pos >= text.size())
        return "";
    if (text[pos] == '{' || text[pos] == '[') {
        size_t end = pos;
        if (!FindMatchingBracket(text, pos, end))
            return "";
        return text.substr(pos, end - pos + 1);
    }
    if (text[pos] == '"') {
        string ignored;
        size_t end = pos;
        if (!ParseJsonStringToken(text, pos, ignored, end))
            return "";
        return text.substr(pos, end - pos);
    }
    size_t end = pos;
    while (end < text.size() && text[end] != ',' && text[end] != '}' &&
           text[end] != ']')
        ++end;
    return text.substr(pos, end - pos);
}


static vector<double> ParseDoubleArray(const string& arrayText, bool& ok) {
    vector<double> values;
    ok = false;
    size_t begin = 0;
    SkipWhitespace(arrayText, begin);
    if (begin >= arrayText.size() || arrayText[begin] != '[')
        return values;
    size_t pos = begin + 1;
    while (pos < arrayText.size()) {
        SkipWhitespace(arrayText, pos);
        if (pos < arrayText.size() && arrayText[pos] == ']') {
            ok = true;
            return values;
        }
        const char* numberBegin = arrayText.c_str() + pos;
        char* numberEnd = nullptr;
        double value = strtod(numberBegin, &numberEnd);
        if (numberEnd == numberBegin || !isfinite(value))
            return {};
        values.emplace_back(value);
        pos = static_cast<size_t>(numberEnd - arrayText.c_str());
        SkipWhitespace(arrayText, pos);
        if (pos < arrayText.size() && arrayText[pos] == ',') {
            ++pos;
            continue;
        }
        if (pos < arrayText.size() && arrayText[pos] == ']') {
            ok = true;
            return values;
        }
        return {};
    }
    return {};
}


static vector<string> ParseStringArray(const string& arrayText, bool& ok) {
    vector<string> values;
    ok = false;
    size_t begin = 0;
    SkipWhitespace(arrayText, begin);
    if (begin >= arrayText.size() || arrayText[begin] != '[')
        return values;
    size_t pos = begin + 1;
    while (pos < arrayText.size()) {
        SkipWhitespace(arrayText, pos);
        if (pos < arrayText.size() && arrayText[pos] == ']') {
            ok = true;
            return values;
        }
        string value;
        size_t end = pos;
        if (!ParseJsonStringToken(arrayText, pos, value, end))
            return {};
        values.emplace_back(value);
        pos = end;
        SkipWhitespace(arrayText, pos);
        if (pos < arrayText.size() && arrayText[pos] == ',') {
            ++pos;
            continue;
        }
        if (pos < arrayText.size() && arrayText[pos] == ']') {
            ok = true;
            return values;
        }
        return {};
    }
    return {};
}


static vector<vector<double>> ParseDoubleMatrix(const string& matrixText, bool& ok) {
    vector<vector<double>> matrix;
    ok = false;
    size_t begin = 0;
    SkipWhitespace(matrixText, begin);
    if (begin >= matrixText.size() || matrixText[begin] != '[')
        return matrix;
    size_t pos = begin + 1;
    while (pos < matrixText.size()) {
        SkipWhitespace(matrixText, pos);
        if (pos < matrixText.size() && matrixText[pos] == ']') {
            ok = true;
            return matrix;
        }
        if (pos >= matrixText.size() || matrixText[pos] != '[')
            return {};
        size_t rowEnd = pos;
        if (!FindMatchingBracket(matrixText, pos, rowEnd))
            return {};
        bool rowOk = false;
        vector<double> row = ParseDoubleArray(
            matrixText.substr(pos, rowEnd - pos + 1),
            rowOk
        );
        if (!rowOk)
            return {};
        matrix.emplace_back(row);
        pos = rowEnd + 1;
        SkipWhitespace(matrixText, pos);
        if (pos < matrixText.size() && matrixText[pos] == ',') {
            ++pos;
            continue;
        }
        if (pos < matrixText.size() && matrixText[pos] == ']') {
            ok = true;
            return matrix;
        }
        return {};
    }
    return {};
}


static vector<string> ParseObjectArray(const string& arrayText, bool& ok) {
    vector<string> objects;
    ok = false;
    size_t begin = 0;
    SkipWhitespace(arrayText, begin);
    if (begin >= arrayText.size() || arrayText[begin] != '[')
        return objects;
    size_t pos = begin + 1;
    while (pos < arrayText.size()) {
        SkipWhitespace(arrayText, pos);
        if (pos < arrayText.size() && arrayText[pos] == ']') {
            ok = true;
            return objects;
        }
        if (pos >= arrayText.size() || arrayText[pos] != '{')
            return {};
        size_t objectEnd = pos;
        if (!FindMatchingBracket(arrayText, pos, objectEnd))
            return {};
        objects.emplace_back(arrayText.substr(pos, objectEnd - pos + 1));
        pos = objectEnd + 1;
        SkipWhitespace(arrayText, pos);
        if (pos < arrayText.size() && arrayText[pos] == ',') {
            ++pos;
            continue;
        }
        if (pos < arrayText.size() && arrayText[pos] == ']') {
            ok = true;
            return objects;
        }
        return {};
    }
    return {};
}


static vector<int> ParseIntArray(const string& arrayText) {
    vector<int> values;
    size_t pos = 0;
    while (pos < arrayText.size()) {
        while (pos < arrayText.size() && !(arrayText[pos] == '-' || isdigit(arrayText[pos])))
            ++pos;
        if (pos >= arrayText.size())
            break;
        const char* begin = arrayText.c_str() + pos;
        char* end = nullptr;
        long value = strtol(begin, &end, 10);
        if (end == begin)
            break;
        values.emplace_back(static_cast<int>(value));
        pos = static_cast<size_t>(end - arrayText.c_str());
    }
    return values;
}


static string ParseObjectAfterKey(const string& text, const string& key) {
    size_t pos = text.find("\"" + key + "\"");
    if (pos == string::npos)
        return "";
    size_t begin = text.find('{', pos);
    if (begin == string::npos)
        return "";
    int depth = 0;
    for (size_t end = begin; end < text.size(); ++end) {
        if (text[end] == '{')
            ++depth;
        else if (text[end] == '}' && --depth == 0)
            return text.substr(begin, end - begin + 1);
    }
    return "";
}


static void ParseFeatureRanges(
    const string& text,
    const vector<string>& featureNames,
    vector<double>& mins,
    vector<double>& maxs
) {
    mins.clear();
    maxs.clear();
    string ranges = ParseObjectAfterKey(text, "feature_ranges");
    for (const auto& featureName: featureNames) {
        string featureRange = ParseObjectAfterKey(ranges, featureName);
        double minimum = 0.0;
        double maximum = 0.0;
        if (featureRange.empty() ||
            !ParseNumberAfterKey(featureRange, "min", minimum) ||
            !ParseNumberAfterKey(featureRange, "max", maximum)) {
            mins.clear();
            maxs.clear();
            return;
        }
        mins.emplace_back(minimum);
        maxs.emplace_back(maximum);
    }
}


bool DesignErrorModel::Load(const string& path) {
    loaded = false;
    backend = Backend::POLYNOMIAL;
    intercept = 0.0;
    zeroPrediction = 0.0;
    featureNum = 0;
    terms.clear();
    inputMean.clear();
    inputScale.clear();
    powerAlpha.clear();
    powerBeta.clear();
    outputMean = 0.0;
    outputScale = 1.0;
    mlpLayers.clear();
    featureMins.clear();
    featureMaxs.clear();
    featureUsed.clear();

    string text = ReadTextFile(path);
    if (text.empty()) {
        cerr << "ERROR: cannot read design error model: " << path << endl;
        return false;
    }
    string predictionSemantics;
    const TopLevelStringStatus semanticsStatus = ParseTopLevelStringAfterKey(
        text,
        "prediction_semantics",
        predictionSemantics
    );
    if (semanticsStatus == TopLevelStringStatus::MISSING) {
        cerr << "ERROR: design error model has no top-level prediction_semantics: "
             << path << endl;
        return false;
    }
    if (semanticsStatus == TopLevelStringStatus::INVALID) {
        cerr << "ERROR: design error model has invalid or duplicate "
             << "prediction_semantics: " << path << endl;
        return false;
    }
    if (predictionSemantics != "zero_anchored_clamped_delta" &&
        predictionSemantics != "clamped_raw") {
        cerr << "ERROR: unsupported design error model prediction_semantics '"
             << predictionSemantics << "': " << path << endl;
        return false;
    }

    string backendName = "polynomial";
    const TopLevelStringStatus backendStatus = ParseTopLevelStringAfterKey(
        text,
        "backend",
        backendName
    );
    if (backendStatus == TopLevelStringStatus::INVALID) {
        cerr << "ERROR: design error model has invalid or duplicate backend: "
             << path << endl;
        return false;
    }
    if (backendName == "polynomial")
        backend = Backend::POLYNOMIAL;
    else if (backendName == "mlp_relu")
        backend = Backend::MLP_RELU;
    else if (backendName == "hals_power")
        backend = Backend::HALS_POWER;
    else {
        cerr << "ERROR: unsupported design error model backend '"
             << backendName << "': " << path << endl;
        return false;
    }

    if (backend == Backend::POLYNOMIAL) {
        if (!ParseNumberAfterKey(text, "intercept", intercept)) {
            cerr << "ERROR: design error model has no numeric intercept: " << path << endl;
            return false;
        }

        bool termsOk = false;
        vector<string> termTexts = ParseObjectArray(
            ParseJsonValueAfterKey(text, "terms"),
            termsOk
        );
        if (!termsOk) {
            cerr << "ERROR: design error model has an invalid terms array: "
                 << path << endl;
            return false;
        }
        for (const auto& termText: termTexts) {
            vector<int> powers = ParseIntArray(
                ParseJsonValueAfterKey(termText, "powers")
            );
            double coef = 0.0;
            if (powers.empty() || !ParseNumberAfterKey(termText, "coef", coef)) {
                cerr << "ERROR: invalid polynomial term in design error model: "
                     << path << endl;
                return false;
            }
            if (featureNum == 0)
                featureNum = static_cast<int>(powers.size());
            if (static_cast<int>(powers.size()) != featureNum) {
                cerr << "ERROR: inconsistent term width in design error model: " << path << endl;
                return false;
            }
            terms.push_back(Term{powers, coef});
        }

        if (terms.empty() || featureNum <= 0) {
            cerr << "ERROR: design error model has no polynomial terms: " << path << endl;
            return false;
        }
    }
    else if (backend == Backend::HALS_POWER) {
        bool alphaOk = false, betaOk = false, scaleOk = false;
        powerAlpha = ParseDoubleArray(ParseJsonValueAfterKey(text, "alpha"), alphaOk);
        powerBeta = ParseDoubleArray(ParseJsonValueAfterKey(text, "beta"), betaOk);
        inputScale = ParseDoubleArray(ParseJsonValueAfterKey(text, "input_scale"), scaleOk);
        if (!alphaOk || !betaOk || !scaleOk || powerAlpha.empty() ||
            powerAlpha.size() != powerBeta.size() || powerAlpha.size() != inputScale.size() ||
            !ParseNumberAfterKey(text, "intercept", intercept) || !isfinite(intercept)) {
            cerr << "ERROR: malformed HALS power model: " << path << endl;
            return false;
        }
        featureNum = static_cast<int>(powerAlpha.size());
        for (int i = 0; i < featureNum; ++i) {
            if (!isfinite(powerAlpha[i]) || powerAlpha[i] < 0.0 ||
                !isfinite(powerBeta[i]) || powerBeta[i] <= 0.0 ||
                !isfinite(inputScale[i]) || inputScale[i] <= 0.0) {
                cerr << "ERROR: invalid HALS power parameters: " << path << endl;
                return false;
            }
        }
    }
    else {
        string inputScaler = ParseObjectAfterKey(text, "input_scaler");
        string outputScaler = ParseObjectAfterKey(text, "output_scaler");
        bool meanOk = false;
        bool scaleOk = false;
        inputMean = ParseDoubleArray(
            ParseJsonValueAfterKey(inputScaler, "mean"),
            meanOk
        );
        inputScale = ParseDoubleArray(
            ParseJsonValueAfterKey(inputScaler, "scale"),
            scaleOk
        );
        if (!meanOk || !scaleOk || inputMean.empty() ||
            inputMean.size() != inputScale.size()) {
            cerr << "ERROR: MLP design error model has invalid input scaler: "
                 << path << endl;
            return false;
        }
        for (double scale: inputScale) {
            if (!isfinite(scale) || abs(scale) <= 1e-30) {
                cerr << "ERROR: MLP design error model has a zero/non-finite "
                     << "input scale: " << path << endl;
                return false;
            }
        }
        if (!ParseNumberAfterKey(outputScaler, "mean", outputMean) ||
            !ParseNumberAfterKey(outputScaler, "scale", outputScale) ||
            !isfinite(outputMean) || !isfinite(outputScale)) {
            cerr << "ERROR: MLP design error model has invalid output scaler: "
                 << path << endl;
            return false;
        }
        featureNum = static_cast<int>(inputMean.size());
        bool layersOk = false;
        vector<string> layerTexts = ParseObjectArray(
            ParseJsonValueAfterKey(text, "layers"),
            layersOk
        );
        if (!layersOk || layerTexts.empty()) {
            cerr << "ERROR: MLP design error model has no layers: " << path << endl;
            return false;
        }
        int expectedInputs = featureNum;
        for (const auto& layerText: layerTexts) {
            bool weightsOk = false;
            bool biasOk = false;
            MlpLayer layer;
            layer.weights = ParseDoubleMatrix(
                ParseJsonValueAfterKey(layerText, "weights"),
                weightsOk
            );
            layer.bias = ParseDoubleArray(
                ParseJsonValueAfterKey(layerText, "bias"),
                biasOk
            );
            layer.activation = ParseJsonStringValue(
                ParseJsonValueAfterKey(layerText, "activation")
            );
            if (!weightsOk || !biasOk || layer.weights.empty() ||
                layer.bias.empty() || static_cast<int>(layer.weights.size()) != expectedInputs) {
                cerr << "ERROR: MLP layer dimension mismatch in model: "
                     << path << endl;
                return false;
            }
            for (const auto& row: layer.weights) {
                if (row.size() != layer.bias.size()) {
                    cerr << "ERROR: MLP layer weight row width mismatch in model: "
                         << path << endl;
                    return false;
                }
            }
            if (layer.activation != "relu" && layer.activation != "identity" &&
                layer.activation != "linear") {
                cerr << "ERROR: unsupported MLP layer activation '"
                     << layer.activation << "': " << path << endl;
                return false;
            }
            expectedInputs = static_cast<int>(layer.bias.size());
            mlpLayers.emplace_back(layer);
        }
        if (expectedInputs != 1) {
            cerr << "ERROR: MLP design error model must have one scalar output: "
                 << path << endl;
            return false;
        }
    }

    bool featureNamesOk = false;
    vector<string> featureNames = ParseStringArray(
        ParseJsonValueAfterKey(text, "feature_cols"),
        featureNamesOk
    );
    if (!featureNamesOk || static_cast<int>(featureNames.size()) != featureNum) {
        cerr << "WARNING: ignoring feature ranges because feature_cols are "
             << "missing or inconsistent in design error model: " << path << endl;
        featureNames.clear();
    }
    ParseFeatureRanges(text, featureNames, featureMins, featureMaxs);
    if (static_cast<int>(featureMins.size()) != featureNum ||
        static_cast<int>(featureMaxs.size()) != featureNum) {
        if (!featureMins.empty() || !featureMaxs.empty())
            cerr << "WARNING: ignoring incomplete feature ranges in design error model: " << path << endl;
        featureMins.clear();
        featureMaxs.clear();
    }
    featureUsed.assign(featureNum, false);
    if (backend == Backend::POLYNOMIAL) {
        for (const auto& term: terms) {
            if (abs(term.coef) <= 1e-18)
                continue;
            for (int i = 0; i < featureNum; ++i) {
                if (term.powers[i] > 0)
                    featureUsed[i] = true;
            }
        }
    }
    else
        featureUsed.assign(featureNum, true);
    vector<double> zeros(featureNum, 0.0);
    zeroPrediction = predictionSemantics == "zero_anchored_clamped_delta"
        ? RawPredict(zeros) : 0.0;
    loaded = true;
    cout << "loaded design error model: " << path
         << " (#features = " << featureNum
         << ", backend = " << backendName;
    if (backend == Backend::POLYNOMIAL)
        cout << ", #terms = " << terms.size();
    else
        cout << ", #layers = " << mlpLayers.size();
    cout << ")" << endl;
    return true;
}


bool DesignErrorModel::InFeatureRange(const vector<double>& features) const {
    assert(loaded);
    assert(static_cast<int>(features.size()) == featureNum);
    if (featureMins.empty())
        return true;
    bool allZero = true;
    for (double value: features)
        allZero = allZero && abs(value) <= 1e-18;
    if (allZero)
        return true;
    for (int i = 0; i < featureNum; ++i) {
        if (!featureUsed[i])
            continue;
        double scale = max({featureMaxs[i] - featureMins[i], abs(featureMins[i]), abs(featureMaxs[i])});
        double tolerance = max(1e-12, 0.05 * scale);
        if (features[i] < featureMins[i] - tolerance || features[i] > featureMaxs[i] + tolerance)
            return false;
    }
    return true;
}


bool DesignErrorModel::HasFeatureRange(int index) const {
    assert(loaded);
    return index >= 0 && index < featureNum &&
           static_cast<int>(featureMins.size()) == featureNum &&
           static_cast<int>(featureMaxs.size()) == featureNum;
}


double DesignErrorModel::FeatureMin(int index) const {
    assert(HasFeatureRange(index));
    return featureMins[index];
}


double DesignErrorModel::FeatureMax(int index) const {
    assert(HasFeatureRange(index));
    return featureMaxs[index];
}


double DesignErrorModel::RawPredict(const vector<double>& features) const {
    assert(static_cast<int>(features.size()) == featureNum);
    if (backend == Backend::HALS_POWER) {
        double result = intercept;
        for (int i = 0; i < featureNum; ++i)
            result += powerAlpha[i] * pow(max(0.0, features[i] / inputScale[i]), powerBeta[i]);
        return result;
    }
    if (backend == Backend::POLYNOMIAL) {
        double result = intercept;
        for (const auto& term: terms) {
            double product = term.coef;
            for (int i = 0; i < featureNum; ++i) {
                for (int p = 0; p < term.powers[i]; ++p)
                    product *= features[i];
            }
            result += product;
        }
        return result;
    }
    vector<double> activations(featureNum, 0.0);
    for (int i = 0; i < featureNum; ++i)
        activations[i] = (features[i] - inputMean[i]) / inputScale[i];
    for (const auto& layer: mlpLayers) {
        vector<double> next(layer.bias);
        for (size_t i = 0; i < activations.size(); ++i) {
            for (size_t j = 0; j < next.size(); ++j)
                next[j] += activations[i] * layer.weights[i][j];
        }
        if (layer.activation == "relu") {
            for (double& value: next)
                if (value < 0.0)
                    value = 0.0;
        }
        activations.swap(next);
    }
    assert(activations.size() == 1);
    return outputMean + outputScale * activations[0];
}


vector<double> DesignErrorModel::RawGradient(const vector<double>& features) const {
    assert(static_cast<int>(features.size()) == featureNum);
    vector<double> gradient(featureNum, 0.0);
    if (backend == Backend::HALS_POWER) {
        for (int i = 0; i < featureNum; ++i) {
            const double scaled = max(features[i] / inputScale[i], 1e-15);
            gradient[i] = powerAlpha[i] * powerBeta[i] * pow(scaled, powerBeta[i] - 1.0) / inputScale[i];
        }
        return gradient;
    }
    if (backend == Backend::POLYNOMIAL) {
        for (const auto& term: terms) {
            for (int dim = 0; dim < featureNum; ++dim) {
                if (term.powers[dim] <= 0)
                    continue;
                double product = term.coef * static_cast<double>(term.powers[dim]);
                for (int i = 0; i < featureNum; ++i) {
                    const int exponent = term.powers[i] - (i == dim? 1: 0);
                    for (int p = 0; p < exponent; ++p)
                        product *= features[i];
                }
                gradient[dim] += product;
            }
        }
        return gradient;
    }

    vector<vector<double>> activations;
    vector<vector<double>> preActivations;
    activations.emplace_back(featureNum, 0.0);
    for (int i = 0; i < featureNum; ++i)
        activations[0][i] = (features[i] - inputMean[i]) / inputScale[i];
    for (const auto& layer: mlpLayers) {
        vector<double> pre(layer.bias);
        const auto& prev = activations.back();
        for (size_t i = 0; i < prev.size(); ++i) {
            for (size_t j = 0; j < pre.size(); ++j)
                pre[j] += prev[i] * layer.weights[i][j];
        }
        vector<double> post(pre);
        if (layer.activation == "relu") {
            for (double& value: post)
                if (value < 0.0)
                    value = 0.0;
        }
        preActivations.emplace_back(pre);
        activations.emplace_back(post);
    }
    vector<double> delta(1, outputScale);
    for (int layerIndex = static_cast<int>(mlpLayers.size()) - 1; layerIndex >= 0; --layerIndex) {
        const auto& layer = mlpLayers[layerIndex];
        vector<double> deltaZ(delta);
        if (layer.activation == "relu") {
            for (size_t j = 0; j < deltaZ.size(); ++j) {
                if (preActivations[layerIndex][j] <= 0.0)
                    deltaZ[j] = 0.0;
            }
        }
        vector<double> previous(activations[layerIndex].size(), 0.0);
        for (size_t i = 0; i < previous.size(); ++i) {
            for (size_t j = 0; j < deltaZ.size(); ++j)
                previous[i] += deltaZ[j] * layer.weights[i][j];
        }
        delta.swap(previous);
    }
    assert(delta.size() == static_cast<size_t>(featureNum));
    for (int i = 0; i < featureNum; ++i)
        gradient[i] = delta[i] / inputScale[i];
    return gradient;
}


double DesignErrorModel::Predict(const vector<double>& features) const {
    assert(loaded);
    assert(static_cast<int>(features.size()) == featureNum);
    double result = RawPredict(features);
    result -= zeroPrediction;
    if (result < 0.0)
        return 0.0;
    return result;
}


vector<double> DesignErrorModel::Gradient(const vector<double>& features) const {
    assert(loaded);
    assert(static_cast<int>(features.size()) == featureNum);
    const double unclamped = RawPredict(features) - zeroPrediction;
    if (unclamped <= 0.0)
        return vector<double>(featureNum, 0.0);
    return RawGradient(features);
}
