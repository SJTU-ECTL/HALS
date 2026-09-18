#include "als.h"

#include <chrono>
#include <cmath>
#include <ctime>
#include <limits>
#include <optional>
#include <unistd.h>

using namespace std;
using namespace abc;
using namespace boost;

namespace {

using ProfClock = std::chrono::steady_clock;

double ProfSeconds(ProfClock::time_point beg, ProfClock::time_point end = ProfClock::now()) {
    return std::chrono::duration<double>(end - beg).count();
}

double ProfCpuSeconds(clock_t beg, clock_t end = std::clock()) {
    return static_cast<double>(end - beg) / CLOCKS_PER_SEC;
}

string JsonEscape(const string& value) {
    ostringstream out;
    for (unsigned char ch: value) {
        switch (ch) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (ch < 0x20)
                    out << "\\u" << hex << setw(4) << setfill('0') << int(ch) << dec << setfill(' ');
                else
                    out << ch;
        }
    }
    return out.str();
}

string JsonDouble(double value) {
    if (!isfinite(value))
        return "null";
    ostringstream out;
    out << setprecision(17) << value;
    return out.str();
}

string JsonDoubleVector(const vector<double>& values) {
    ostringstream out;
    out << "[";
    for (size_t i = 0; i < values.size(); ++i) {
        if (i)
            out << ",";
        out << JsonDouble(values[i]);
    }
    out << "]";
    return out.str();
}

bool IsSampleExactMetrics(const CombinedErrorMetrics& metrics) {
    constexpr double kZeroTol = 1e-15;
    if (fabs(metrics.metricErr) > kZeroTol)
        return false;
    for (double value: metrics.mapeVector) {
        if (fabs(value) > kZeroTol)
            return false;
    }
    return fabs(metrics.multiMetrics.nmed) <= kZeroTol &&
           fabs(metrics.multiMetrics.nmse) <= kZeroTol &&
           fabs(metrics.multiMetrics.er) <= kZeroTol &&
           fabs(metrics.multiMetrics.me) <= kZeroTol;
}

bool AtomicWriteText(const string& path, const string& contents) {
    filesystem::path finalPath(path);
    if (!finalPath.parent_path().empty())
        filesystem::create_directories(finalPath.parent_path());
    const filesystem::path tempPath = finalPath.string() + ".tmp." +
        to_string(static_cast<long long>(getpid()));
    {
        ofstream out(tempPath, ios::binary | ios::trunc);
        if (!out.good())
            return false;
        out << contents;
        out.flush();
        if (!out.good()) {
            filesystem::remove(tempPath);
            return false;
        }
    }
    std::error_code ec;
    filesystem::rename(tempPath, finalPath, ec);
    if (ec) {
        filesystem::remove(tempPath);
        return false;
    }
    return true;
}

bool SameNormalizedPath(const string& lhs, const string& rhs) {
    if (lhs.empty() || rhs.empty())
        return lhs == rhs;
    error_code lhsError;
    error_code rhsError;
    const filesystem::path lhsPath =
        filesystem::absolute(filesystem::path(lhs), lhsError).lexically_normal();
    const filesystem::path rhsPath =
        filesystem::absolute(filesystem::path(rhs), rhsError).lexically_normal();
    if (lhsError || rhsError)
        return lhs == rhs;
    return lhsPath == rhsPath;
}

double CombinedMetricMaxAbsDiff(
    const CombinedErrorMetrics& lhs,
    const CombinedErrorMetrics& rhs
) {
    double diff = fabs(lhs.metricErr - rhs.metricErr);
    diff = max(diff, fabs(lhs.multiMetrics.nmed - rhs.multiMetrics.nmed));
    diff = max(diff, fabs(lhs.multiMetrics.nmse - rhs.multiMetrics.nmse));
    diff = max(diff, fabs(lhs.multiMetrics.er - rhs.multiMetrics.er));
    diff = max(diff, fabs(lhs.multiMetrics.me - rhs.multiMetrics.me));
    if (lhs.mapeVector.size() != rhs.mapeVector.size())
        return numeric_limits<double>::infinity();
    for (size_t i = 0; i < lhs.mapeVector.size(); ++i)
        diff = max(diff, fabs(lhs.mapeVector[i] - rhs.mapeVector[i]));
    return diff;
}

bool IsCompactFeatureExact(
    double kernelErr,
    const vector<double>* errorVector,
    const ErrorMetrics* metrics
) {
    constexpr double EPS = 1e-15;
    if (fabs(kernelErr) > EPS)
        return false;
    if (errorVector != nullptr) {
        for (double value: *errorVector)
            if (fabs(value) > EPS)
                return false;
    }
    if (metrics != nullptr) {
        if (fabs(metrics->nmed) > EPS || fabs(metrics->nmse) > EPS ||
            fabs(metrics->er) > EPS || fabs(metrics->me) > EPS)
            return false;
    }
    return true;
}

double CompactFeatureMetricValue(
    const string& metric,
    double kernelErr,
    const vector<double>* errorVector,
    const ErrorMetrics* metrics,
    METR_TYPE metrType
) {
    if (metric == "IS_EXACT")
        return IsCompactFeatureExact(kernelErr, errorVector, metrics)? 1.0: 0.0;
    if (metric == "KERNEL_ERROR")
        return kernelErr;
    if (metric == "MAE")
        return metrics == nullptr? kernelErr: metrics->nmed;
    if (metric == "MAPE") {
        if (metrType == METR_TYPE::MAPE)
            return kernelErr;
        if (errorVector != nullptr && !errorVector->empty())
            return accumulate(errorVector->begin(), errorVector->end(), 0.0)
                / static_cast<double>(errorVector->size());
        return kernelErr;
    }
    if (metric == "NMED")
        return metrics == nullptr
            ? numeric_limits<double>::quiet_NaN()
            : metrics->nmed;
    if (metric == "NMSE")
        return metrics == nullptr
            ? numeric_limits<double>::quiet_NaN()
            : metrics->nmse;
    if (metric == "ER")
        return metrics == nullptr? (fabs(kernelErr) > 1e-15? 1.0: 0.0): metrics->er;
    if (metric == "ME" || metric == "BIAS")
        return metrics == nullptr? 0.0: metrics->me;
    return numeric_limits<double>::quiet_NaN();
}

optional<string> ReadCompleteJson(const string& path) {
    ifstream in(path, ios::binary);
    if (!in.good())
        return nullopt;
    string text((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
    const auto first = text.find_first_not_of(" \t\r\n");
    const auto last = text.find_last_not_of(" \t\r\n");
    if (first == string::npos || text[first] != '{' || text[last] != '}')
        return nullopt;
    return text;
}

string ReadTextFileStrict(const filesystem::path& path) {
    ifstream in(path, ios::binary);
    if (!in.good()) {
        cerr << "ERROR: cannot read publication candidate artifact: "
             << path << endl;
        exit(1);
    }
    return string((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
}

string JsonFieldPrefix(const string& key) {
    return "\\\"" + key + "\\\"\\s*:\\s*";
}

optional<bool> ReadJsonBool(const string& text, const string& key) {
    smatch match;
    regex pattern(JsonFieldPrefix(key) + "(true|false)", regex::icase);
    if (!regex_search(text, match, pattern))
        return nullopt;
    return match[1].str() == "true" || match[1].str() == "TRUE";
}

optional<long long> ReadJsonInteger(const string& text, const string& key) {
    smatch match;
    regex pattern(JsonFieldPrefix(key) + "(-?[0-9]+)");
    if (!regex_search(text, match, pattern))
        return nullopt;
    try {
        return stoll(match[1].str());
    }
    catch (...) {
        return nullopt;
    }
}

optional<double> ReadJsonNumber(const string& text, const string& key) {
    smatch match;
    regex pattern(JsonFieldPrefix(key) + "(-?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][+-]?[0-9]+)?)");
    if (!regex_search(text, match, pattern))
        return nullopt;
    try {
        const double value = stod(match[1].str());
        return isfinite(value)? optional<double>(value): nullopt;
    }
    catch (...) {
        return nullopt;
    }
}

optional<string> ReadJsonString(const string& text, const string& key) {
    smatch match;
    regex pattern(JsonFieldPrefix(key) + "\\\"([^\\\"]*)\\\"");
    if (!regex_search(text, match, pattern))
        return nullopt;
    return match[1].str();
}

vector<double> ReadJsonNumberArray(const string& text, const string& key) {
    smatch match;
    regex pattern(JsonFieldPrefix(key) + "\\[([^\\]]*)\\]");
    if (!regex_search(text, match, pattern))
        return {};
    vector<double> values;
    string token;
    stringstream stream(match[1].str());
    while (getline(stream, token, ',')) {
        try {
            size_t used = 0;
            const double value = stod(token, &used);
            if (isfinite(value) && token.find_first_not_of(" \t\r\n", used) == string::npos)
                values.emplace_back(value);
            else
                return {};
        }
        catch (...) {
            return {};
        }
    }
    return values;
}

}


void ALSOpt::Print() {
    cout << endl << "******************** options ********************" << endl;
    cout << (isSign? "use signed output": "use unsigned output") << endl;
    cout << "enable fast error estimation = " << (enableFastErrEst? "YES": "NO") << endl;
    cout << "source seed = " << sourceSeed << endl;
    cout << "lacType = " << lacType << endl;
    cout << "distrType = " << distrType << endl;
    cout << "metrType = " << metrType << endl;
    cout << "outputNum = " << outputNum << endl;
    if (!outputWidths.empty()) {
        cout << "outputWidths = [";
        for (size_t i = 0; i < outputWidths.size(); ++i) {
            if (i)
                cout << ",";
            cout << outputWidths[i];
        }
        cout << "]" << endl;
    }
    cout << "design metric weight = " << designMetricWeight << endl;
    if (!designModelPath.empty()) {
        cout << "design error model path = " << designModelPath << endl;
        cout << "model safety margin = " << modelSafetyMargin << endl;
        cout << "early-exit policy = " << earlyExitPolicy << endl;
        cout << "scalar kernel feature = " << (scalarKernelFeature? "YES": "NO") << endl;
        cout << "feature indices = [";
        for (size_t i = 0; i < featureIndices.size(); ++i) {
            if (i)
                cout << ",";
            cout << featureIndices[i];
        }
        cout << "]" << endl;
        if (!featureMetrics.empty()) {
            cout << "feature metrics = [";
            for (size_t i = 0; i < featureMetrics.size(); ++i) {
                if (i)
                    cout << ",";
                cout << featureMetrics[i];
            }
            cout << "]" << endl;
        }
    }
    cout << "#simulation patterns = " << nFrame << endl;
    cout << "#simulation patterns for candidate AppResub generation = " << nFrame4ResubGen << endl;
    cout << "max #candidate AppResubs = " << maxCandResub << endl;
    cout << "max #exact candidate validations = " << maxExactCandValidate << endl;
    cout << "candidate validation policy = " << candidateValidationPolicy << endl;
    cout << "candidate feature source = " << candidateFeatureSource << endl;
    if (!candidateAuditDir.empty()) {
        cout << "candidate audit directory = " << candidateAuditDir << endl;
        cout << "candidate audit limit = " << candidateAuditLimit << endl;
    }
    if (!transitionGMMPrototypePath.empty()) {
        cout << "transition GMM prototypes = "
             << transitionGMMPrototypePath << endl;
        cout << "transition GMM output = " << transitionGMMOutputPath << endl;
        if (!transitionGMMPolicyPatternFile.empty())
            cout << "transition GMM policy patterns = "
                 << transitionGMMPolicyPatternFile << endl;
        cout << "transition GMM policy frames = "
             << transitionGMMPolicyFrames << endl;
        cout << "transition GMM raw trace = "
             << (transitionGMMIncludeRawTrace? "YES": "NO") << endl;
        if (!transitionGMMPackedTraceDir.empty())
            cout << "transition GMM packed trace directory = "
                 << transitionGMMPackedTraceDir << endl;
    }
    cout << "#threads = " << nThread << endl;
    if (maxRound > 0)
        cout << "max #ALS rounds = " << maxRound << endl;
    cout << "max #0-error fallback rounds = " << maxZeroErrorFallbackRounds << endl;
    // cout << "max level difference = " << maxLevelDiff << endl;
    cout << metrType << " upper bounds = [";
    const auto& bounds = errUppBounds.empty()? vector<double>{errUppBound}: errUppBounds;
    for (size_t i = 0; i < bounds.size(); ++i) {
        if (i)
            cout << ",";
        cout << bounds[i];
    }
    cout << "]" << endl;
    if (!designModelPath.empty())
        cout << "design-model target upper bounds = [";
    else
        cout << "design-level " << metrType << " upper bounds = [";
    for (size_t i = 0; i < bounds.size(); ++i) {
        if (i)
            cout << ",";
        if (!designErrUppBounds.empty())
            cout << designErrUppBounds[i];
        else
            cout << (designModelPath.empty()? bounds[i] * designMetricWeight: bounds[i]);
    }
    cout << "]" << endl;
    if (metrType == METR_TYPE::MED) {
        int bitWidthPerOutput = outputWidths.empty()
            ? Abc_NtkPoNum(pNtk) / outputNum
            : *max_element(outputWidths.begin(), outputWidths.end());
        cout << "NMED upper bound = " << BigFlt(errUppBound) / GetErrorMetricNormalization(bitWidthPerOutput, isSign) << endl;
    }
    cout << "standard cell path: " << standCellPath << endl;
    cout << "output path: " << outpPath << endl;
    if (!patternFilePath.empty())
        cout << "pattern file path: " << patternFilePath << endl;
    if (probeRound > 0) {
        cout << "probe round = " << probeRound << endl;
        cout << "probe summary path = " << probeSummaryPath << endl;
        cout << "target decision path = " << targetDecisionPath << endl;
        cout << "probe timeout = " << probeTimeoutSec << " sec" << endl;
        cout << "probe poll interval = " << probePollMs << " ms" << endl;
        if (!runId.empty())
            cout << "run id = " << runId << endl;
    }
    cout << "*************************************************" << endl << endl;
}


ALSMan::ALSMan(ALSOpt & opt): isSign(opt.isSign), enableFastErrEst(opt.enableFastErrEst), sourceSeed(opt.sourceSeed), seed(0), lacType(opt.lacType), distrType(opt.distrType), metrType(opt.metrType), nFrame(opt.nFrame), nFrame4ResubGen(opt.nFrame4ResubGen), maxCandResub(opt.maxCandResub), maxExactCandValidate(opt.maxExactCandValidate), nThread(opt.nThread), maxRound(opt.maxRound), maxZeroErrorFallbackRounds(opt.maxZeroErrorFallbackRounds), maxLevelDiff(INT_MAX), round(0), boundPhase(0), zeroErrorFallbackRounds(0), errUppBound(opt.errUppBound), designErrUppBound(opt.designModelPath.empty()? opt.errUppBound * opt.designMetricWeight: opt.errUppBound), errUppBounds(opt.errUppBounds), designMetricWeight(opt.designMetricWeight), modelSafetyMargin(opt.modelSafetyMargin), scalarKernelFeature(opt.scalarKernelFeature), earlyExitPolicy(opt.earlyExitPolicy), outputNum(opt.outputNum), outputWidths(opt.outputWidths), maxDelay(DBL_MAX), accNet(NetMan(opt.pNtk, true)), standCellPath(opt.standCellPath), outpPath(opt.outpPath), patternFilePath(opt.patternFilePath), transitionGMMPolicyPatternFile(opt.transitionGMMPolicyPatternFile), transitionGMMPolicyFrames(opt.transitionGMMPolicyFrames > 0? opt.transitionGMMPolicyFrames: opt.nFrame), candidateValidationPolicy(opt.candidateValidationPolicy), candidateFeatureSource(opt.candidateFeatureSource), candidateAuditDir(opt.candidateAuditDir), candidateAuditLimit(opt.candidateAuditLimit), publicationCandidateDir(opt.publicationCandidateDir), publicationCandidateLimit(opt.publicationCandidateLimit), transitionGMMConfig({opt.transitionGMMPrototypePath, opt.transitionGMMOutputPath, opt.runId, opt.transitionGMMGraphHash, opt.transitionGMMBaseStateHash, opt.transitionGMMPatternHash, opt.transitionGMMTargetRegion, opt.transitionGMMBoundaryNodes, opt.transitionGMMIncludeRawTrace, opt.transitionGMMPackedTraceDir}), signedLACScorerConfig(opt.signedLACScorerConfig), signedLACScorer(nullptr), lastStopReason(StopReason::NONE), useDesignErrorModel(!opt.designModelPath.empty()), featureIndices(opt.featureIndices), featureMetrics(opt.featureMetrics), designFeatureVector(opt.initialFeatureVector), probeRound(opt.probeRound), probeSummaryPath(opt.probeSummaryPath), targetDecisionPath(opt.targetDecisionPath), probeTimeoutSec(opt.probeTimeoutSec), probePollMs(opt.probePollMs), runId(opt.runId), probeHandled(false), targetActive(false), targetWorkLimit(-1), roundCandidatesGenerated(0), roundCandidatesScanned(0), roundExactValidations(0), cumulativeCandidatesGenerated(0), cumulativeCandidatesScanned(0), cumulativeExactValidations(0), cumulativeWorkUnits(0), roundAccountingFinalized(true) {
    if (errUppBounds.empty())
        errUppBounds.emplace_back(errUppBound);
    if (useDesignErrorModel && !opt.designErrUppBounds.empty()) {
        if (opt.designErrUppBounds.size() != errUppBounds.size()) {
            cerr << "ERROR: designErrUppBounds length must match errUppBounds length" << endl;
            exit(1);
        }
        designErrUppBounds = opt.designErrUppBounds;
    }
    else {
        designErrUppBounds.reserve(errUppBounds.size());
        for (double bound: errUppBounds)
            designErrUppBounds.emplace_back(
                useDesignErrorModel? bound: bound * designMetricWeight);
    }
    errUppBound = errUppBounds.front();
    designErrUppBound = designErrUppBounds.front();
    if (probeRound > 0) {
        if (probeSummaryPath.empty())
            probeSummaryPath = outpPath + "probe_summary.json";
        if (targetDecisionPath.empty())
            targetDecisionPath = outpPath + "target_decision.json";
        if (filesystem::absolute(probeSummaryPath).lexically_normal() ==
            filesystem::absolute(targetDecisionPath).lexically_normal()) {
            cerr << "ERROR: probe summary and target decision paths must differ" << endl;
            exit(1);
        }
        // A controller may be watching these paths before the process reaches
        // the barrier.  Remove prior-run outputs so it cannot consume a stale
        // prefix or applied-decision acknowledgement.
        std::error_code cleanupError;
        filesystem::remove(probeSummaryPath, cleanupError);
        cleanupError.clear();
        filesystem::remove(outpPath + "target_decision_applied.json", cleanupError);
    }
    if (!candidateAuditDir.empty()) {
        filesystem::create_directories(candidateAuditDir);
        const filesystem::path manifest = filesystem::path(candidateAuditDir)
            / "candidate_audit.jsonl";
        if (filesystem::exists(manifest) && filesystem::file_size(manifest) > 0) {
            cerr << "ERROR: candidate audit manifest already exists and is non-empty: "
                 << manifest << endl;
            exit(1);
        }
        if (!AtomicWriteText(manifest.string(), "")) {
            cerr << "ERROR: cannot initialize candidate audit manifest: "
                 << manifest << endl;
            exit(1);
        }
    }
    if (!publicationCandidateDir.empty()) {
        filesystem::create_directories(publicationCandidateDir);
        const filesystem::path manifest = filesystem::path(
            publicationCandidateDir) / "candidate_pool.jsonl";
        if (filesystem::exists(manifest) && filesystem::file_size(manifest) > 0) {
            cerr << "ERROR: publication candidate pool already exists: "
                 << manifest << endl;
            exit(1);
        }
        if (!AtomicWriteText(manifest.string(), "")) {
            cerr << "ERROR: cannot initialize publication candidate pool: "
                 << manifest << endl;
            exit(1);
        }
    }
    if (transitionGMMConfig.Enabled()) {
        if (!AtomicWriteText(transitionGMMConfig.outputPath, "")) {
            cerr << "ERROR: cannot initialize transition GMM output: "
                 << transitionGMMConfig.outputPath << endl;
            exit(1);
        }
        if (transitionGMMConfig.baseStateHash.empty()) {
            transitionGMMConfig.baseStateHash = StableTransitionCandidateHash(
                "initial:" + transitionGMMConfig.graphHash
            );
        }
    }
    if (signedLACScorerConfig.Enabled()) {
        designErrUppBound = signedLACScorerConfig.qMax;
        designErrUppBounds.assign(1, signedLACScorerConfig.qMax);
        signedLACScorer = make_unique<SignedLACScorer>();
        string scorerError;
        if (!signedLACScorer->Configure(signedLACScorerConfig, scorerError)) {
            cerr << "ERROR: cannot initialize signed-LAC scorer: "
                 << scorerError << endl;
            exit(1);
        }
    }
    if (accNet.GetNetType() == NET_TYPE::GATE) {
        cout << "convert gate netlist into AIG" << endl;
        accNet.Comm("st; compress2rs; logic; sop; ps;");
    }
    if (useDesignErrorModel) {
        if (!designErrorModel.Load(opt.designModelPath))
            exit(1);
        if (designFeatureVector.empty())
            designFeatureVector.assign(designErrorModel.FeatureNum(), 0.0);
        if (static_cast<int>(designFeatureVector.size()) != designErrorModel.FeatureNum()) {
            cerr << "ERROR: initial feature vector size (" << designFeatureVector.size()
                 << ") does not match design model feature count ("
                 << designErrorModel.FeatureNum() << ")" << endl;
            exit(1);
        }
        if (featureIndices.empty()) {
            cerr << "ERROR: design model mode requires at least one feature index" << endl;
            exit(1);
        }
        if (!featureMetrics.empty() && featureMetrics.size() != featureIndices.size()) {
            cerr << "ERROR: feature metrics size (" << featureMetrics.size()
                 << ") does not match feature indices size ("
                 << featureIndices.size() << ")" << endl;
            exit(1);
        }
        for (auto idx: featureIndices) {
            if (idx < 0 || idx >= designErrorModel.FeatureNum()) {
                cerr << "ERROR: feature index out of range: " << idx << endl;
                exit(1);
            }
        }
        cout << "initial predicted design error = "
             << designErrorModel.Predict(designFeatureVector) + modelSafetyMargin << endl;
    }
    randGen.seed(sourceSeed);
}


double ALSMan::PredictDesignError(double kernelErr) const {
    if (!useDesignErrorModel)
        return designMetricWeight * kernelErr;
    if (DoubleGreat(kernelErr, 1.0) && featureMetrics.empty() &&
        (scalarKernelFeature || featureIndices.size() != 4))
        return 1e9;
    return PredictDesignErrorFromFullFeatures(
        EstimateFullDesignFeatureVector(kernelErr)
    );
}


vector<double> ALSMan::EstimateLocalDesignFeatureVector(double kernelErr) const {
    if (!useDesignErrorModel)
        return vector<double>{kernelErr};
    if (!featureMetrics.empty()) {
        const int bitWidthPerOutput = outputWidths.empty()
            ? accNet.GetPoNum() / outputNum
            : *max_element(outputWidths.begin(), outputWidths.end());
        const double norm = double(GetErrorMetricNormalization(bitWidthPerOutput, isSign));
        vector<double> estimatedFeatures;
        estimatedFeatures.reserve(featureMetrics.size());
        for (const auto& metric: featureMetrics) {
            const double compactValue = CompactFeatureMetricValue(
                metric, kernelErr, nullptr, nullptr, metrType);
            if (isfinite(compactValue))
                estimatedFeatures.emplace_back(compactValue);
            else if (metric == "NMED" && metrType == METR_TYPE::MED)
                estimatedFeatures.emplace_back(kernelErr / norm);
            else if (metric == "NMSE" && metrType == METR_TYPE::MSE)
                estimatedFeatures.emplace_back(kernelErr / (norm * norm));
            else
                estimatedFeatures.emplace_back(kernelErr);
        }
        return estimatedFeatures;
    }
    if (!scalarKernelFeature && featureIndices.size() == 4) {
        const int bitWidthPerOutput = accNet.GetPoNum() / outputNum;
        const double norm = double(GetErrorMetricNormalization(bitWidthPerOutput, isSign));
        vector<double> localFeatures(4, 0.0);
        if (metrType == METR_TYPE::MED)
            localFeatures[0] = kernelErr / norm;
        else if (metrType == METR_TYPE::MSE)
            localFeatures[1] = kernelErr / (norm * norm);
        else
            localFeatures[0] = kernelErr;
        return localFeatures;
    }
    return vector<double>{kernelErr};
}


vector<double> ALSMan::ExpandDesignFeatureVector(const vector<double>& localFeatures) const {
    if (!useDesignErrorModel)
        return localFeatures;
    auto features = designFeatureVector;
    if (scalarKernelFeature) {
        double kernelErr = 0.0;
        if (!localFeatures.empty()) {
            for (auto err: localFeatures)
                kernelErr += err;
            kernelErr /= static_cast<double>(localFeatures.size());
        }
        for (auto idx: featureIndices)
            features[idx] = kernelErr;
    }
    else if (localFeatures.size() == featureIndices.size()) {
        for (size_t i = 0; i < featureIndices.size(); ++i)
            features[featureIndices[i]] = localFeatures[i];
    }
    else {
        double kernelErr = localFeatures.empty()? 0.0: localFeatures.front();
        for (auto idx: featureIndices)
            features[idx] = kernelErr;
    }
    return features;
}


vector<double> ALSMan::EstimateFullDesignFeatureVector(double kernelErr) const {
    return ExpandDesignFeatureVector(EstimateLocalDesignFeatureVector(kernelErr));
}


double ALSMan::PredictDesignErrorFromFullFeatures(const vector<double>& features) const {
    if (!designErrorModel.InFeatureRange(features))
        return 1e9;
    return designErrorModel.Predict(features) + modelSafetyMargin;
}


double ALSMan::PredictDesignError(const vector<double>& errorVector) const {
    if (!useDesignErrorModel)
        return designMetricWeight * (errorVector.empty()? 0.0: errorVector.front());
    for (auto err: errorVector) {
        if (DoubleGreat(err, 1.0))
            return 1e9;
    }
    return PredictDesignErrorFromFullFeatures(
        ExpandDesignFeatureVector(errorVector)
    );
}


bool ALSMan::UseGradientCandidateRanking() const {
    return candidateValidationPolicy == "gradient" ||
           candidateValidationPolicy == "gradient_stratified";
}


bool ALSMan::UseStratifiedCandidateOrdering() const {
    return candidateValidationPolicy == "stratified" ||
           candidateValidationPolicy == "gradient_stratified";
}


double ALSMan::LocalEstimationUpperBound() const {
    if (!useDesignErrorModel)
        return errUppBound;

    constexpr double POLICY_FEATURE_FRACTION = 0.004;
    constexpr double POLICY_FEATURE_FLOOR = 2e-6;
    constexpr double POLICY_RAW_MED_CAP = 32.0;
    constexpr double POLICY_RAW_MSE_CAP = 4096.0;
    int bitWidthPerOutput = outputWidths.empty()
        ? accNet.GetPoNum() / outputNum
        : *max_element(outputWidths.begin(), outputWidths.end());
    const double norm = double(GetErrorMetricNormalization(bitWidthPerOutput, isSign));
    const double norm2 = norm * norm;
    double localBound = 1.0;
    bool matchedNormalizedFeature = false;

    auto policyFeatureCapForMetric = [&](size_t metricIndex) {
        if (metricIndex < featureIndices.size() &&
            designErrorModel.HasFeatureRange(featureIndices[metricIndex])) {
            double value = designErrorModel.FeatureMax(featureIndices[metricIndex]);
            if (isfinite(value) && value > 0.0)
                return min(value * 1.05,
                           max(POLICY_FEATURE_FLOOR,
                               value * POLICY_FEATURE_FRACTION));
        }
        return 1.0;
    };

    auto capPolicyRawBound = [&](double candidateBound) {
        if (!isfinite(candidateBound) || candidateBound <= 0.0)
            return candidateBound;
        if (metrType == METR_TYPE::MED)
            return min(candidateBound, POLICY_RAW_MED_CAP);
        if (metrType == METR_TYPE::MSE)
            return min(candidateBound, POLICY_RAW_MSE_CAP);
        return candidateBound;
    };

    if (!featureMetrics.empty()) {
        for (size_t i = 0; i < featureMetrics.size(); ++i) {
            const string& metric = featureMetrics[i];
            const double featureCap = policyFeatureCapForMetric(i);
            double candidateBound = 1.0;
            bool matched = true;
            if (metric == "NMED" && metrType == METR_TYPE::MED)
                candidateBound = featureCap * norm;
            else if (metric == "NMSE" && metrType == METR_TYPE::MSE)
                candidateBound = featureCap * norm2;
            else if (metric == "MAPE" && metrType == METR_TYPE::MAPE)
                candidateBound = featureCap;
            else if (metric == "KERNEL_ERROR")
                candidateBound = featureCap;
            else
                matched = false;
            candidateBound = capPolicyRawBound(candidateBound);
            if (matched && isfinite(candidateBound) && candidateBound > localBound) {
                localBound = candidateBound;
                matchedNormalizedFeature = true;
            }
        }
    }
    else if (!scalarKernelFeature && featureIndices.size() == 4) {
        if (metrType == METR_TYPE::MED) {
            localBound = norm;
            matchedNormalizedFeature = true;
        }
        else if (metrType == METR_TYPE::MSE) {
            localBound = norm2;
            matchedNormalizedFeature = true;
        }
    }

    if (!matchedNormalizedFeature) {
        if (metrType == METR_TYPE::MED)
            localBound = max(localBound, norm * POLICY_FEATURE_FLOOR);
        else if (metrType == METR_TYPE::MSE)
            localBound = max(localBound, norm2 * POLICY_FEATURE_FLOOR * POLICY_FEATURE_FLOOR);
    }
    localBound = capPolicyRawBound(localBound);
    return max(1.0, localBound);
}


VECBEEEarlyExitConfig ALSMan::BuildVECBEEEarlyExitConfig(
    const vector<double>& baseLocalFeatures,
    const vector<double>& baseFullFeatures
) const {
    (void)baseLocalFeatures;
    (void)baseFullFeatures;
    VECBEEEarlyExitConfig config;
    config.policy = earlyExitPolicy;
    return config;
}


double ALSMan::EstimateCandidateRankingDelta(
    const vector<double>& baseFullFeatures,
    const vector<double>& candidateFullFeatures,
    double baseDesignErr
) const {
    if (!useDesignErrorModel)
        return designMetricWeight * (
            candidateFullFeatures.empty()? 0.0: candidateFullFeatures.front()
        ) - baseDesignErr;
    if (!designErrorModel.InFeatureRange(candidateFullFeatures))
        return 1e9;
    if (!UseGradientCandidateRanking())
        return designErrorModel.Predict(candidateFullFeatures) +
               modelSafetyMargin - baseDesignErr;
    vector<double> gradient = designErrorModel.Gradient(baseFullFeatures);
    if (gradient.size() != candidateFullFeatures.size())
        return 1e9;
    double linearDelta = 0.0;
    for (size_t i = 0; i < gradient.size(); ++i)
        linearDelta += gradient[i] * (candidateFullFeatures[i] - baseFullFeatures[i]);
    return linearDelta;
}


vector<double> ALSMan::SelectDesignFeatureVector(double kernelErr, const vector<double>& errorVector, const ErrorMetrics& multiMetrics) const {
    if (!useDesignErrorModel)
        return vector<double>{kernelErr};
    if (useDesignErrorModel && !featureMetrics.empty()) {
        vector<double> values;
        values.reserve(featureMetrics.size());
        for (const auto& metric: featureMetrics) {
            const double compactValue = CompactFeatureMetricValue(
                metric, kernelErr, &errorVector, &multiMetrics, metrType);
            if (isfinite(compactValue)) {
                values.emplace_back(compactValue);
            }
            else if (metric.rfind("ERROR_VECTOR_", 0) == 0) {
                int idx = stoi(metric.substr(string("ERROR_VECTOR_").size()));
                values.emplace_back(idx >= 0 && idx < static_cast<int>(errorVector.size())? errorVector[idx]: kernelErr);
            }
            else {
                cerr << "ERROR: unknown feature metric: " << metric << endl;
                exit(1);
            }
        }
        return values;
    }
    if (useDesignErrorModel && !scalarKernelFeature && featureIndices.size() == 4)
        return multiMetrics.ToFeatureVector();
    if (!errorVector.empty())
        return errorVector;
    return vector<double>{kernelErr};
}


bool ALSMan::IsDesignErrorFeasible(double kernelErr) const {
    if (signedLACScorer != nullptr)
        return DoubleLessEqual(
            signedLACScorerConfig.qCurrent, signedLACScorerConfig.qMax);
    if (useDesignErrorModel && DoubleGreat(kernelErr, 1.0))
        return false;
    return DoubleLessEqual(PredictDesignError(kernelErr), designErrUppBound);
}


bool ALSMan::IsDesignErrorFeasible(const vector<double>& errorVector) const {
    if (signedLACScorer != nullptr)
        return DoubleLessEqual(
            signedLACScorerConfig.qCurrent, signedLACScorerConfig.qMax);
    if (useDesignErrorModel) {
        for (auto err: errorVector) {
            if (DoubleGreat(err, 1.0))
                return false;
        }
    }
    return DoubleLessEqual(PredictDesignError(errorVector), designErrUppBound);
}


string ALSMan::StopReasonString(StopReason reason) const {
    switch (reason) {
        case StopReason::NONE: return "";
        case StopReason::NO_FEASIBLE_REDUCING_LAC: return "no_feasible_reducing_lac";
        case StopReason::NO_LACS_GENERATED: return "no_lacs_generated";
        case StopReason::BASE_ERROR_INFEASIBLE: return "base_error_infeasible";
        case StopReason::MAX_ROUND_REACHED: return "max_round_reached";
        case StopReason::TARGET_OPERATING_ROUND_REACHED: return "target_operating_round_reached";
        case StopReason::TARGET_WORK_CAP_REACHED: return "target_work_cap_reached";
    }
    return "unknown";
}


void ALSMan::BeginRoundAccounting() {
    roundCandidatesGenerated = 0;
    roundCandidatesScanned = 0;
    roundExactValidations = 0;
    roundAccountingFinalized = false;
    roundStartTime = ProfClock::now();
}


void ALSMan::FinalizeRoundAccounting() {
    if (roundAccountingFinalized)
        return;
    cumulativeCandidatesGenerated += roundCandidatesGenerated;
    cumulativeCandidatesScanned += roundCandidatesScanned;
    cumulativeExactValidations += roundExactValidations;
    // A work unit is one generated candidate or one exact candidate
    // validation.  Both counters remain available separately for alternative
    // cost models; this scalar is solely the online prefix work-cap currency.
    cumulativeWorkUnits += roundCandidatesGenerated + roundExactValidations;
    roundAccountingFinalized = true;
}


string ALSMan::RecordJson(const ParetoRecord& record) const {
    ostringstream out;
    out << setprecision(17);
    out << "{\"schema\":\"ppt_als_trajectory_point_v1\",\"round\":" << record.round
        << ",\"phase\":" << record.phase
        << ",\"kernel_error\":" << JsonDouble(record.kernelErr)
        << ",\"error_vector\":[";
    for (size_t i = 0; i < record.errorVector.size(); ++i) {
        if (i)
            out << ",";
        out << JsonDouble(record.errorVector[i]);
    }
    out << "]"
        << ",\"error_profile\":{"
        << "\"MAPE\":";
    const double kernelMape = record.errorVector.empty()? 0.0:
        accumulate(record.errorVector.begin(), record.errorVector.end(), 0.0) /
            static_cast<double>(record.errorVector.size());
    out << JsonDouble(kernelMape)
        << ",\"NMED\":" << JsonDouble(record.multiMetrics.nmed)
        << ",\"NMSE\":" << JsonDouble(record.multiMetrics.nmse)
        << ",\"ER\":" << JsonDouble(record.multiMetrics.er)
        << ",\"ME\":" << JsonDouble(record.multiMetrics.me)
        << "}"
        << ",\"design_error\":" << JsonDouble(record.designErr)
        << ",\"design_error_bound\":" << JsonDouble(record.designErrBound)
        << ",\"area\":" << JsonDouble(record.area)
        << ",\"delay\":" << JsonDouble(record.delay)
        << ",\"aig_netlist\":\"" << JsonEscape(record.netlistPath) << "\""
        << ",\"lac\":\"" << JsonEscape(record.lac) << "\""
        << ",\"candidates_generated\":" << record.candidatesGenerated
        << ",\"candidates_scanned\":" << record.candidatesScanned
        << ",\"exact_validations\":" << record.exactValidations
        << ",\"work_units\":" << record.workUnits
        << ",\"cumulative_candidates_generated\":" << record.cumulativeCandidatesGenerated
        << ",\"cumulative_candidates_scanned\":" << record.cumulativeCandidatesScanned
        << ",\"cumulative_exact_validations\":" << record.cumulativeExactValidations
        << ",\"cumulative_work_units\":" << record.cumulativeWorkUnits
        << ",\"round_runtime_sec\":" << JsonDouble(record.roundRuntimeSec)
        << ",\"cumulative_runtime_sec\":" << JsonDouble(record.cumulativeRuntimeSec)
        << ",\"stop_reason\":\"" << JsonEscape(record.stopReason) << "\"}"
        ;
    return out.str();
}


void ALSMan::WriteTrajectoryJsonl() const {
    ostringstream contents;
    for (const auto& record: trajectoryRecords)
        contents << RecordJson(record) << "\n";
    const string path = outpPath + "trajectory.jsonl";
    if (!AtomicWriteText(path, contents.str())) {
        cerr << "ERROR: cannot atomically write trajectory JSONL: " << path << endl;
        exit(1);
    }
}


void ALSMan::MarkTrajectoryStop(StopReason reason) {
    if (trajectoryRecords.empty())
        return;
    FinalizeRoundAccounting();
    ParetoRecord* record = nullptr;
    if (trajectoryRecords.back().round == round) {
        record = &trajectoryRecords.back();
    }
    else {
        trajectoryRecords.emplace_back(trajectoryRecords.back());
        record = &trajectoryRecords.back();
        record->round = round;
        record->phase = boundPhase;
        record->lac.clear();
        record->candidatesGenerated = roundCandidatesGenerated;
        record->candidatesScanned = roundCandidatesScanned;
        record->exactValidations = roundExactValidations;
        record->workUnits = roundCandidatesGenerated + roundExactValidations;
    }
    record->cumulativeCandidatesGenerated = cumulativeCandidatesGenerated;
    record->cumulativeCandidatesScanned = cumulativeCandidatesScanned;
    record->cumulativeExactValidations = cumulativeExactValidations;
    record->cumulativeWorkUnits = cumulativeWorkUnits;
    record->roundRuntimeSec = round == 0? 0.0: ProfSeconds(roundStartTime);
    record->cumulativeRuntimeSec = ProfSeconds(runStartTime);
    record->stopReason = StopReasonString(reason);
    WriteTrajectoryCsv();
}


void ALSMan::WriteProbeSummary(const string& status) const {
    ostringstream out;
    out << setprecision(17)
        << "{\n"
        << "  \"schema\": \"ppt_als_probe_summary_v1\",\n"
        << "  \"schema_version\": 1,\n"
        << "  \"run_id\": \"" << JsonEscape(runId) << "\",\n"
        << "  \"status\": \"" << JsonEscape(status) << "\",\n"
        << "  \"process_id\": " << static_cast<long long>(getpid()) << ",\n"
        << "  \"target_decision_path\": \"" << JsonEscape(targetDecisionPath) << "\",\n"
        << "  \"probe_round\": " << probeRound << ",\n"
        << "  \"current_round\": " << round << ",\n"
        << "  \"source_seed\": " << sourceSeed << ",\n"
        << "  \"rng_state_preserved\": true,\n"
        << "  \"active_bound_phase\": " << boundPhase << ",\n"
        << "  \"active_design_error_bound\": " << JsonDouble(designErrUppBound) << ",\n"
        << "  \"original_design_error_bounds\": [";
    for (size_t i = 0; i < designErrUppBounds.size(); ++i) {
        if (i)
            out << ",";
        out << JsonDouble(designErrUppBounds[i]);
    }
    out << "],\n"
        << "  \"work_definition\": \"candidates_generated + exact_validations\",\n"
        << "  \"cumulative_work\": {\n"
        << "    \"candidates_generated\": " << cumulativeCandidatesGenerated << ",\n"
        << "    \"candidates_scanned\": " << cumulativeCandidatesScanned << ",\n"
        << "    \"exact_validations\": " << cumulativeExactValidations << ",\n"
        << "    \"work_units\": " << cumulativeWorkUnits << ",\n"
        << "    \"runtime_sec\": " << JsonDouble(ProfSeconds(runStartTime)) << "\n"
        << "  },\n"
        << "  \"trajectory_prefix\": [\n";
    for (size_t i = 0; i < trajectoryRecords.size(); ++i) {
        out << "    " << RecordJson(trajectoryRecords[i]);
        if (i + 1 != trajectoryRecords.size())
            out << ",";
        out << "\n";
    }
    out << "  ]\n}\n";
    if (!AtomicWriteText(probeSummaryPath, out.str())) {
        cerr << "ERROR: cannot atomically write probe summary: " << probeSummaryPath << endl;
        exit(1);
    }
}


bool ALSMan::WaitForTargetDecision() {
    cout << "[probe] waiting for target decision at " << targetDecisionPath << endl;
    const auto waitStart = ProfClock::now();
    while (true) {
        auto json = ReadCompleteJson(targetDecisionPath);
        if (json) {
            if (!runId.empty()) {
                auto decisionRunId = ReadJsonString(*json, "run_id");
                if (!decisionRunId || *decisionRunId != runId) {
                    cout << "[probe] ignore target decision for run_id="
                         << (decisionRunId? *decisionRunId: string("<missing>"))
                         << ", expected " << runId << endl;
                    json.reset();
                }
            }
            if (json) {
                TargetDecision decision;
                decision.rawJson = *json;
                const auto explicitAbstain = ReadJsonBool(*json, "abstain");
                const auto continueFullAls = ReadJsonBool(*json, "continue_full_als");
                if (explicitAbstain)
                    decision.abstain = *explicitAbstain;
                if (auto action = ReadJsonString(*json, "action")) {
                    if (*action == "full" || *action == "abstain")
                        decision.abstain = true;
                    else if (*action == "target")
                        decision.abstain = false;
                }
                optional<long long> operatingRound = ReadJsonInteger(*json, "expected_operating_round");
                if (!operatingRound)
                    operatingRound = ReadJsonInteger(*json, "expectedOperatingRound");
                if (operatingRound) {
                    if (*operatingRound > numeric_limits<int>::max() ||
                        *operatingRound < numeric_limits<int>::min())
                        decision.expectedOperatingRound = -2;
                    else
                        decision.expectedOperatingRound = static_cast<int>(*operatingRound);
                }
                if (auto value = ReadJsonInteger(*json, "additional_work_cap"))
                    decision.additionalWorkCap = *value;
                else if (auto value = ReadJsonInteger(*json, "additionalWorkCap"))
                    decision.additionalWorkCap = *value;
                decision.expectedErrorProfile = ReadJsonNumberArray(*json, "expected_error_profile");
                if (decision.expectedErrorProfile.empty())
                    decision.expectedErrorProfile = ReadJsonNumberArray(*json, "expectedErrorProfile");
                if (auto value = ReadJsonString(*json, "risk_level"))
                    decision.riskLevel = *value;
                if (auto value = ReadJsonNumber(*json, "ood_score")) {
                    decision.oodScore = *value;
                    decision.hasOodScore = true;
                }

                const bool hasTarget = decision.expectedOperatingRound >= 0 ||
                    decision.additionalWorkCap >= 0 || !decision.expectedErrorProfile.empty();
                if (!explicitAbstain && hasTarget)
                    decision.abstain = false;
                // An explicit risk-aware abstention always wins over any
                // accidentally retained target/action fields.
                if (explicitAbstain && *explicitAbstain)
                    decision.abstain = true;
                if (continueFullAls && *continueFullAls)
                    decision.abstain = true;
                if (!decision.abstain && decision.expectedOperatingRound < 0 &&
                    decision.additionalWorkCap < 0) {
                    cerr << "WARNING: target decision has no operating round or work cap; "
                         << "safely continuing full ALS" << endl;
                    decision.abstain = true;
                }
                if (decision.expectedOperatingRound < -1 || decision.additionalWorkCap < -1) {
                    cerr << "WARNING: invalid negative target field; safely continuing full ALS" << endl;
                    decision.abstain = true;
                }
                if (!decision.abstain && decision.expectedErrorProfile.size() != 5) {
                    cerr << "WARNING: target decision lacks the canonical five-metric error profile; "
                         << "safely continuing full ALS" << endl;
                    decision.abstain = true;
                }
                if (!decision.abstain) {
                    // [MAPE,NMED,NMSE,ER] are non-negative; ME is signed.
                    for (size_t i = 0; i < 4; ++i) {
                        if (decision.expectedErrorProfile[i] < 0.0) {
                            cerr << "WARNING: target decision has a negative non-ME error metric; "
                                 << "safely continuing full ALS" << endl;
                            decision.abstain = true;
                            break;
                        }
                    }
                }
                targetDecision = decision;
                const string appliedPath = outpPath + "target_decision_applied.json";
                if (!AtomicWriteText(appliedPath, decision.rawJson + "\n")) {
                    cerr << "ERROR: cannot write applied target decision: " << appliedPath << endl;
                    exit(1);
                }
                return true;
            }
        }

        if (probeTimeoutSec > 0.0 && ProfSeconds(waitStart) >= probeTimeoutSec) {
            cerr << "WARNING: target decision timeout; safely continuing full ALS" << endl;
            targetDecision = TargetDecision{};
            targetDecision.riskLevel = "decision_timeout";
            const string fallback = "{\"abstain\":true,\"reason\":\"decision_timeout\"}\n";
            AtomicWriteText(outpPath + "target_decision_applied.json", fallback);
            return false;
        }
        this_thread::sleep_for(chrono::milliseconds(probePollMs));
    }
}


bool ALSMan::ShouldStopForTarget(StopReason& reason) const {
    if (!targetActive)
        return false;
    if (targetDecision.expectedOperatingRound >= 0 &&
        round >= targetDecision.expectedOperatingRound) {
        reason = StopReason::TARGET_OPERATING_ROUND_REACHED;
        return true;
    }
    if (targetWorkLimit >= 0 && cumulativeWorkUnits >= targetWorkLimit) {
        reason = StopReason::TARGET_WORK_CAP_REACHED;
        return true;
    }
    return false;
}


bool ALSMan::HandleProbeBarrier() {
    if (probeHandled || probeRound <= 0 || round < probeRound)
        return false;
    probeHandled = true;
    WriteProbeSummary("awaiting_target_decision");
    WaitForTargetDecision();
    if (targetDecision.abstain) {
        cout << "[probe] predictor abstained; continue the same process/RNG state to full ALS" << endl;
        targetActive = false;
        return false;
    }
    targetActive = true;
    if (targetDecision.additionalWorkCap >= 0) {
        if (targetDecision.additionalWorkCap > numeric_limits<long long>::max() - cumulativeWorkUnits)
            targetWorkLimit = numeric_limits<long long>::max();
        else
            targetWorkLimit = cumulativeWorkUnits + targetDecision.additionalWorkCap;
    }
    cout << "[probe] targeted continuation: operating_round="
         << targetDecision.expectedOperatingRound
         << " additional_work_cap=" << targetDecision.additionalWorkCap
         << " original_error_bound_unchanged=" << designErrUppBound << endl;
    StopReason reason = StopReason::NONE;
    if (ShouldStopForTarget(reason)) {
        lastStopReason = reason;
        MarkTrajectoryStop(reason);
        return true;
    }
    return false;
}


void ALSMan::Run() {
    assert(lacType == LAC_TYPE::RESUB);
    runStartTime = ProfClock::now();
    roundStartTime = runStartTime;

    // initialize
    cout << endl << "******************** init status ********************" << endl;
    assert(maxDelay == DBL_MAX);
    maxDelay = Eval(accNet, 0.0, true, true);
    cout << "max delay = " << maxDelay << endl;
    accNet.Comm("st; logic; sop; ps;");
    auto currNet = accNet;
    cout << "*****************************************************" << endl << endl;

    int phaseStartRound = 1;
    double err = 0.0;
    while (true) {
        if (maxRound > 0 && round >= maxRound) {
            cout << "stop single selection: max #ALS rounds reached" << endl;
            lastStopReason = StopReason::MAX_ROUND_REACHED;
            MarkTrajectoryStop(lastStopReason);
            const double runtime = ProfSeconds(runStartTime);
            boundPhaseRecords.push_back({boundPhase, designErrUppBound, phaseStartRound, round,
                                         "max_round_reached", runtime});
            WriteParetoPhaseCsv(boundPhase);
            if (probeRound > 0 && !probeHandled) {
                probeHandled = true;
                WriteProbeSummary("completed_before_probe");
            }
            break;
        }
        cout << "------------------- round " << ++round << "-------------------" << endl;
        err = ApplyTheBestLAC(currNet);
        const double runtime = ProfSeconds(runStartTime);
        cout << "actual runtime = " << runtime << " sec" << endl;
        cout << "------------------------------------------------" << endl << endl;
        if (err != DBL_MAX) {
            bool targetStop = HandleProbeBarrier();
            StopReason targetReason = StopReason::NONE;
            if (!targetStop && ShouldStopForTarget(targetReason)) {
                lastStopReason = targetReason;
                MarkTrajectoryStop(targetReason);
                targetStop = true;
            }
            if (targetStop) {
                const string stopReason = StopReasonString(lastStopReason);
                boundPhaseRecords.push_back({boundPhase, designErrUppBound, phaseStartRound, round,
                                             stopReason, ProfSeconds(runStartTime)});
                WriteParetoPhaseCsv(boundPhase);
                cout << "[target] stop_reason=" << stopReason
                     << " cumulative_runtime_sec=" << ProfSeconds(runStartTime) << endl;
                break;
            }
            continue;
        }

        MarkTrajectoryStop(lastStopReason);
        const bool canRelaxBound =
            lastStopReason == StopReason::NO_FEASIBLE_REDUCING_LAC &&
            boundPhase + 1 < static_cast<int>(designErrUppBounds.size());
        bool targetStop = false;
        if (canRelaxBound) {
            // A failed selection round still consumed probe work.  Do not
            // silently cross the requested barrier merely because the state
            // is about to continue under the next bound phase.
            targetStop = HandleProbeBarrier();
            StopReason targetReason = StopReason::NONE;
            if (!targetStop && ShouldStopForTarget(targetReason)) {
                lastStopReason = targetReason;
                MarkTrajectoryStop(targetReason);
                targetStop = true;
            }
        }
        string stopReason = StopReasonString(lastStopReason);
        if (stopReason.empty())
            stopReason = "unknown";
        boundPhaseRecords.push_back({boundPhase, designErrUppBound, phaseStartRound, round, stopReason, runtime});
        WriteParetoPhaseCsv(boundPhase);
        cout << "[bound-phase] phase=" << boundPhase
             << " bound=" << designErrUppBound
             << " stop_reason=" << stopReason
             << " cumulative_runtime_sec=" << runtime << endl;

        if (targetStop) {
            cout << "[target] stop_reason=" << stopReason
                 << " cumulative_runtime_sec=" << ProfSeconds(runStartTime) << endl;
            break;
        }
        if (canRelaxBound) {
            ++boundPhase;
            errUppBound = errUppBounds[boundPhase];
            designErrUppBound = designErrUppBounds[boundPhase];
            phaseStartRound = round + 1;
            zeroErrorFallbackRounds = 0;
            cout << "continue ALS with relaxed bound phase " << boundPhase
                 << ": design error upper bound = " << designErrUppBound << endl;
            continue;
        }
        if (probeRound > 0 && !probeHandled) {
            // Natural termination before an accepted probe barrier is a fixed
            // endpoint, not a controller failure.  Publish the complete
            // prefix without waiting for a decision; sibling kernels may
            // still reach their normal barriers and continue in place.
            probeHandled = true;
            WriteProbeSummary("completed_before_probe");
        }
        break;
    }
    WriteParetoCsv();
    WriteTrajectoryCsv();
    WriteBoundPhasesCsv();
}


void ALSMan::WriteCandidateAuditRecords(
    const vector<CandidateAuditRecord>& records,
    const string& selectedLac
) const {
    if (candidateAuditDir.empty() || records.empty())
        return;
    const filesystem::path manifest = filesystem::path(candidateAuditDir)
        / "candidate_audit.jsonl";
    ofstream out(manifest, ios::binary | ios::app);
    if (!out.good()) {
        cerr << "ERROR: cannot append candidate audit manifest: "
             << manifest << endl;
        exit(1);
    }
    for (const auto& record: records) {
        out << "{\"schema\":\"hals_resubals_candidate_audit_v1\""
            << ",\"round\":" << record.round
            << ",\"phase\":" << record.phase
            << ",\"source_seed\":" << sourceSeed
            << ",\"round_seed\":" << record.truthSeed
            << ",\"search_seed\":" << record.searchSeed
            << ",\"policy_seed\":" << record.policySeed
            << ",\"truth_seed\":" << record.truthSeed
            << ",\"search_frames\":" << record.searchFrames
            << ",\"policy_frames\":" << record.policyFrames
            << ",\"truth_frames\":" << record.truthFrames
            << ",\"candidate_rank\":" << record.candidateRank
            << ",\"validation_index\":" << record.validationIndex
            << ",\"size_gain\":" << record.sizeGain
            << ",\"metric_type\":\"" << JsonEscape(record.metricType) << "\""
            << ",\"candidate_feature_source\":\""
            << JsonEscape(record.candidateFeatureSource) << "\""
            << ",\"base_kernel_error\":"
            << JsonDouble(record.baseKernelErr)
            << ",\"base_design_error\":"
            << JsonDouble(record.baseDesignErr)
            << ",\"estimated_kernel_error\":"
            << JsonDouble(record.estimatedKernelErr)
            << ",\"measured_kernel_error\":"
            << JsonDouble(record.measuredKernelErr)
            << ",\"base_error_vector\":"
            << JsonDoubleVector(record.baseErrorVector)
            << ",\"error_vector\":" << JsonDoubleVector(record.errorVector)
            << ",\"base_local_metrics\":{"
            << "\"NMED\":" << JsonDouble(record.baseMultiMetrics.nmed)
            << ",\"NMSE\":" << JsonDouble(record.baseMultiMetrics.nmse)
            << ",\"ER\":" << JsonDouble(record.baseMultiMetrics.er)
            << ",\"ME\":" << JsonDouble(record.baseMultiMetrics.me) << "}"
            << ",\"local_metrics\":{"
            << "\"NMED\":" << JsonDouble(record.multiMetrics.nmed)
            << ",\"NMSE\":" << JsonDouble(record.multiMetrics.nmse)
            << ",\"ER\":" << JsonDouble(record.multiMetrics.er)
            << ",\"ME\":" << JsonDouble(record.multiMetrics.me) << "}"
            << ",\"local_features\":" << JsonDoubleVector(record.localFeatures)
            << ",\"full_features\":" << JsonDoubleVector(record.fullFeatures)
            << ",\"predicted_design_error\":"
            << JsonDouble(record.predictedDesignErr)
            << ",\"design_error_bound\":" << JsonDouble(designErrUppBound)
            << ",\"feature_in_range\":"
            << (record.featureInRange? "true": "false")
            << ",\"feasible\":" << (record.feasible? "true": "false")
            << ",\"selected\":"
            << (!selectedLac.empty() && record.lac == selectedLac? "true": "false")
            << ",\"candidate_hash\":\""
            << JsonEscape(record.candidateHash) << "\""
            << ",\"base_state_hash\":\""
            << JsonEscape(record.baseStateHash) << "\""
            << ",\"policy_pattern_hash\":\""
            << JsonEscape(record.policyPatternHash) << "\""
            << ",\"lac\":\"" << JsonEscape(record.lac) << "\""
            << ",\"base_netlist\":\""
            << JsonEscape(record.baseNetlistPath) << "\""
            << ",\"candidate_netlist\":\""
            << JsonEscape(record.candidateNetlistPath) << "\"}\n";
    }
    out.flush();
    if (!out.good()) {
        cerr << "ERROR: failed while writing candidate audit manifest: "
             << manifest << endl;
        exit(1);
    }
}


void ALSMan::WritePublicationCandidatePool(
    NetMan& baseNet, LACMan& lacMan, double baseError,
    unsigned searchSeed, unsigned policySeed, unsigned truthSeed
) {
    if (publicationCandidateDir.empty())
        return;
    struct Candidate {
        LACPtr lac;
        string hash;
        int gain;
        string representation;
    };
    vector<Candidate> candidates;
    candidates.reserve(lacMan.GetLacNum());
    for (int lacId = 0; lacId < lacMan.GetLacNum(); ++lacId) {
        auto lac = lacMan.GetLac(lacId);
        auto resub = dynamic_pointer_cast<ResubLAC>(lac);
        if (!resub || resub->GetSizeGain() <= 0)
            continue;
        const string representation = resub->GetReprStr();
        candidates.push_back({lac,
            StableTransitionCandidateHash(representation),
            resub->GetSizeGain(), representation});
    }
    sort(candidates.begin(), candidates.end(), [](const Candidate& lhs,
                                                  const Candidate& rhs) {
        return lhs.hash < rhs.hash;
    });
    candidates.erase(unique(candidates.begin(), candidates.end(),
        [](const Candidate& lhs, const Candidate& rhs) {
            return lhs.hash == rhs.hash;
        }), candidates.end());
    if (static_cast<int>(candidates.size()) > publicationCandidateLimit)
        candidates.resize(publicationCandidateLimit);

    ostringstream roundName;
    roundName << "round_" << setw(4) << setfill('0') << round;
    const filesystem::path roundDirectory = filesystem::path(
        publicationCandidateDir) / roundName.str();
    filesystem::create_directories(roundDirectory);
    const filesystem::path basePath = roundDirectory / "base.blif";
    baseNet.WriteBlif(basePath.string());
    const string baseStateHash = StableTransitionCandidateHash(
        ReadTextFileStrict(basePath));
    const filesystem::path manifest = filesystem::path(
        publicationCandidateDir) / "candidate_pool.jsonl";
    ofstream out(manifest, ios::binary | ios::app);
    if (!out.good()) {
        cerr << "ERROR: cannot append publication candidate pool: "
             << manifest << endl;
        exit(1);
    }
    const string savedLastAppliedLac = lastAppliedLac;
    for (size_t rank = 0; rank < candidates.size(); ++rank) {
        const auto& candidate = candidates[rank];
        auto candidateNet = baseNet;
        ApplyLacPro(candidateNet, candidate.lac, baseError);
        const filesystem::path candidatePath = roundDirectory /
            ("candidate_" + candidate.hash.substr(7, 64) + ".blif");
        candidateNet.WriteBlif(candidatePath.string());
        const string blifHash = StableTransitionCandidateHash(
            ReadTextFileStrict(candidatePath));
        out << "{\"schema\":\"hals_resubals_candidate_pool_v1\""
            << ",\"run_id\":\"" << JsonEscape(runId) << "\""
            << ",\"round\":" << round
            << ",\"phase\":" << boundPhase
            << ",\"source_seed\":" << sourceSeed
            << ",\"search_seed\":" << searchSeed
            << ",\"policy_seed\":" << policySeed
            << ",\"truth_seed\":" << truthSeed
            << ",\"generated_candidates\":" << lacMan.GetLacNum()
            << ",\"hash_rank\":" << rank
            << ",\"size_gain\":" << candidate.gain
            << ",\"candidate_sha256\":\""
            << JsonEscape(candidate.hash) << "\""
            << ",\"base_state_sha256\":\""
            << JsonEscape(baseStateHash) << "\""
            << ",\"blif_sha256\":\"" << JsonEscape(blifHash) << "\""
            << ",\"lac\":\"" << JsonEscape(candidate.representation) << "\""
            << ",\"base_netlist\":\""
            << JsonEscape(filesystem::absolute(basePath)
                    .lexically_normal().string()) << "\""
            << ",\"candidate_netlist\":\""
            << JsonEscape(filesystem::absolute(candidatePath)
                    .lexically_normal().string()) << "\"}\n";
    }
    lastAppliedLac = savedLastAppliedLac;
    out.flush();
    if (!out.good()) {
        cerr << "ERROR: failed while writing publication candidate pool: "
             << manifest << endl;
        exit(1);
    }
    cout << "[publication-candidate-pool] generated=" << lacMan.GetLacNum()
         << " exported=" << candidates.size()
         << " order=candidate_sha256" << endl;
}


double ALSMan::ApplyTheBestLAC(NetMan & net) {
    lastStopReason = StopReason::NONE;
    BeginRoundAccounting();
    auto profTotalBeg = ProfClock::now();
    const clock_t profTotalCpuBeg = std::clock();
    double profBaseError = 0.0;
    double profLacGen = 0.0;
    double profFastEst = 0.0;
    double profSort = 0.0;
    double profSignedPanel = 0.0;
    double profSignedController = 0.0;
    double profCandidateLoop = 0.0;
    double profCandidateApply = 0.0;
    double profCandidateCombinedMetrics = 0.0;
    double profCandidateModel = 0.0;
    double profSimplify = 0.0;
    double profEval = 0.0;
    double profBaseErrorCpu = 0.0;
    double profLacGenCpu = 0.0;
    double profFastEstCpu = 0.0;
    double profSortCpu = 0.0;
    double profSignedPanelCpu = 0.0;
    double profSignedControllerCpu = 0.0;
    double profCandidateLoopCpu = 0.0;
    double profCandidateApplyCpu = 0.0;
    double profCandidateCombinedMetricsCpu = 0.0;
    double profCandidateModelCpu = 0.0;
    double profSimplifyCpu = 0.0;
    double profEvalCpu = 0.0;
    VECBEEFeatureProfile profVecbeeFeature;
    auto printRoundPhaseProfile = [&]() {
        const double profTotal = ProfSeconds(profTotalBeg);
        const double profTotalCpu = ProfCpuSeconds(profTotalCpuBeg);
        const double profCandidateOther = profCandidateLoop -
            profCandidateApply - profCandidateCombinedMetrics - profCandidateModel;
        const double profCandidateOtherCpu = profCandidateLoopCpu -
            profCandidateApplyCpu - profCandidateCombinedMetricsCpu -
            profCandidateModelCpu;
        cout << "[profile] round " << round << " phase_seconds"
             << " base_error=" << profBaseError
             << " lac_generation=" << profLacGen
             << " fast_estimation=" << profFastEst
             << " sort=" << profSort
             << " signed_panel=" << profSignedPanel
             << " signed_controller=" << profSignedController
             << " candidate_loop=" << profCandidateLoop
             << " candidate_apply=" << profCandidateApply
             << " candidate_combined_metrics=" << profCandidateCombinedMetrics
             << " candidate_model=" << profCandidateModel
             << " candidate_other=" << profCandidateOther
             << " simplify=" << profSimplify
             << " eval=" << profEval
             << " total=" << profTotal
             << endl;
        cout << "[profile] round " << round << " phase_cpu_seconds"
             << " base_error=" << profBaseErrorCpu
             << " lac_generation=" << profLacGenCpu
             << " fast_estimation=" << profFastEstCpu
             << " sort=" << profSortCpu
             << " signed_panel=" << profSignedPanelCpu
             << " signed_controller=" << profSignedControllerCpu
             << " candidate_loop=" << profCandidateLoopCpu
             << " candidate_apply=" << profCandidateApplyCpu
             << " candidate_combined_metrics=" << profCandidateCombinedMetricsCpu
             << " candidate_model=" << profCandidateModelCpu
             << " candidate_other=" << profCandidateOtherCpu
             << " simplify=" << profSimplifyCpu
             << " eval=" << profEvalCpu
             << " total=" << profTotalCpu
             << endl;
        if (profVecbeeFeature.candidatesProcessed > 0) {
            const long long totalFeatureUs =
                profVecbeeFeature.newValueTimeUs +
                profVecbeeFeature.traceBuildTimeUs +
                profVecbeeFeature.scalarMetricTimeUs +
                profVecbeeFeature.gmmSketchTimeUs +
                profVecbeeFeature.recordExportTimeUs;
            cout << "[profile] round " << round << " vecbee_feature_us"
                 << " candidates=" << profVecbeeFeature.candidatesProcessed
                 << " patterns_visited=" << profVecbeeFeature.patternsVisited
                 << " patterns_possible=" << profVecbeeFeature.patternsPossible
                 << " early_exit_candidates=" << profVecbeeFeature.earlyExitCandidates
                 << " legacy_early_exit_candidates=" << profVecbeeFeature.legacyEarlyExitCandidates
                 << " early_exit_checks=" << profVecbeeFeature.earlyExitChecks
                 << " new_value=" << profVecbeeFeature.newValueTimeUs
                 << " trace_build=" << profVecbeeFeature.traceBuildTimeUs
                 << " scalar_metric=" << profVecbeeFeature.scalarMetricTimeUs
                 << " gmm_sketch=" << profVecbeeFeature.gmmSketchTimeUs
                 << " record_export=" << profVecbeeFeature.recordExportTimeUs
                 << " json_write=" << profVecbeeFeature.jsonWriteTimeUs
                 << " total=" << totalFeatureUs
                 << endl;
        }
    };

    // backup network
    auto backNet = net;

    // use new seed
    auto profBaseBeg = ProfClock::now();
    const clock_t profBaseCpuBeg = std::clock();
    seed = NewSeed();
    const unsigned truthSeed = seed;
    unsigned searchSeed = NewSeed();
    while (searchSeed == truthSeed)
        searchSeed = NewSeed();
    unsigned policySeed = NewSeed();
    while (policySeed == truthSeed || policySeed == searchSeed)
        policySeed = NewSeed();
    Simulator accSmlt(accNet, seed, nFrame);
    InitSimulatorForStatErr(accSmlt, distrType, patternFilePath);
    auto backCombinedMetrics = CalcCombinedErrorMetrics(accSmlt, net, isSign, seed, nFrame, metrType, distrType, outputNum, patternFilePath, outputWidths);
    auto backErr = backCombinedMetrics.metricErr;
    vector<double> backErrorVector;
    backErrorVector = backCombinedMetrics.mapeVector;
    ErrorMetrics backMultiMetrics = backCombinedMetrics.multiMetrics;
    vector<double> backFeatureVector = SelectDesignFeatureVector(backErr, backErrorVector, backMultiMetrics);
    cout << "base " << metrType << " = " << backErr << endl;
    const bool backSampleExact = IsSampleExactMetrics(backCombinedMetrics);
    double backDesignErr = backSampleExact
        ? 0.0
        : PredictDesignError(backFeatureVector);
    if (signedLACScorer != nullptr)
        backDesignErr = signedLACScorerConfig.qCurrent;
    vector<double> backFullFeatureVector = useDesignErrorModel
        ? ExpandDesignFeatureVector(backFeatureVector)
        : backFeatureVector;
    cout << "base predicted design error = " << backDesignErr << endl;
    const bool baseFeasible = signedLACScorer != nullptr
        ? !DoubleGreat(signedLACScorerConfig.qCurrent,
                      signedLACScorerConfig.qMax)
        : (backSampleExact || IsDesignErrorFeasible(backFeatureVector));
    if (!baseFeasible) {
        FinalizeRoundAccounting();
        lastStopReason = StopReason::BASE_ERROR_INFEASIBLE;
        lastAppliedLac.clear();
        // This is the unchanged current state, so use the same canonical ABC
        // mapping as every accepted trajectory point.  The optional Yosys
        // path can report a zero-area result for a non-constant BLIF and would
        // make two byte-identical states carry different area labels.
        Eval(backNet, backErr);
        cout << "WARNING: exceed design error bound due to unstable " << metrType << " measurement" << endl;
        return DBL_MAX;
    }
    profBaseError = ProfSeconds(profBaseBeg);
    profBaseErrorCpu = ProfCpuSeconds(profBaseCpuBeg);

    string candidateAuditBaseNetlist;
    vector<CandidateAuditRecord> candidateAuditRecords;
    if (!candidateAuditDir.empty()) {
        ostringstream name;
        name << "round_" << setw(4) << setfill('0') << round << "_base.blif";
        const filesystem::path path = filesystem::path(candidateAuditDir)
            / name.str();
        backNet.WriteBlif(path.string());
        candidateAuditBaseNetlist = filesystem::absolute(path)
            .lexically_normal().string();
    }
    
    // create constant nodes
    net.CreateConst(true);

    // get target nodes
    auto nodes = net.TopoSortWithIds();

    // generate LACs
    auto profLacGenBeg = ProfClock::now();
    const clock_t profLacGenCpuBeg = std::clock();
    LACMan lacMan(lacType, nThread);
    lacMan.Gen012ResubLACsPro(
        net,
        nodes,
        searchSeed,
        maxLevelDiff,
        nFrame4ResubGen,
        maxCandResub
    );
    cout << "#lacs = " << lacMan.GetLacNum() << endl;
    roundCandidatesGenerated = lacMan.GetLacNum();
    profLacGen = ProfSeconds(profLacGenBeg);
    profLacGenCpu = ProfCpuSeconds(profLacGenCpuBeg);

    if (!publicationCandidateDir.empty())
        WritePublicationCandidatePool(
            backNet, lacMan, backErr, searchSeed, policySeed, truthSeed);

    // error estimation
    auto profFastEstBeg = ProfClock::now();
    const clock_t profFastEstCpuBeg = std::clock();
    BigInt errScale = BigInt(transitionGMMPolicyFrames) * BigInt(outputNum);
    if (metrType == METR_TYPE::MAPE)
        errScale *= BigInt(MAPE_ERR_SCALE);
    const double localErrUppBound = LocalEstimationUpperBound();
    const BigInt uppBound = (BigInt)(BigFlt(errScale) * BigFlt(localErrUppBound)) + 8192;
    cout << "VECBEE local policy upper bound = " << localErrUppBound << endl;
    VECBEEEarlyExitConfig vecbeeEarlyExitConfig =
        BuildVECBEEEarlyExitConfig(backFeatureVector, backFullFeatureVector);
    cout << "VECBEE early-exit policy = "
         << vecbeeEarlyExitConfig.policy << endl;
    BigInt backErrInt = (BigInt)(BigFlt(errScale) * BigFlt(backErr));
    if (lacMan.GetLacNum() == 0) {
        cout << "WARNING: no LACs generated, stopping ALS" << endl;
        net = backNet;
        lastAppliedLac.clear();
        FinalizeRoundAccounting();
        lastStopReason = StopReason::NO_LACS_GENERATED;
        // Preserve one mapping protocol across accepted and terminal states;
        // see the BASE_ERROR_INFEASIBLE path above.
        Eval(backNet, backErr);
        return DBL_MAX;
    }
    TransitionGMMExportConfig roundTransitionConfig = transitionGMMConfig;
    if (roundTransitionConfig.Enabled()) {
        ostringstream patternIdentity;
        patternIdentity << roundTransitionConfig.patternHash
                        << ":seed=" << policySeed
                        << ":frames=" << transitionGMMPolicyFrames;
        roundTransitionConfig.patternHash = StableTransitionCandidateHash(
            patternIdentity.str()
        );
    }
    vector<vector<TransitionGMMRecord>> roundTransitionRecords;
    vector<CombinedErrorMetrics> roundVecbeeCombinedMetrics;
    unordered_map<string, int> originalLacIdByHash;
    const bool useVecbeeCandidateFeatures =
        candidateFeatureSource == "vecbee" ||
        candidateFeatureSource == "vecbee_shadow";
    const bool shadowVecbeeCandidateFeatures =
        candidateFeatureSource == "vecbee_shadow";
    const bool rankWithVecbeeCandidateFeatures =
        candidateFeatureSource == "vecbee" ||
        candidateFeatureSource == "vecbee_shadow";
    if (useVecbeeCandidateFeatures) {
        originalLacIdByHash.reserve(lacMan.GetLacNum());
        for (int lacId = 0; lacId < lacMan.GetLacNum(); ++lacId) {
            auto resub = dynamic_pointer_cast<ResubLAC>(lacMan.GetLac(lacId));
            if (resub)
                originalLacIdByHash[StableTransitionCandidateHash(
                    resub->GetReprStr())] = lacId;
        }
        const bool policyTruthPatternsDiffer =
            distrType != DISTR_TYPE::SELF ||
            (!transitionGMMPolicyPatternFile.empty() &&
             !SameNormalizedPath(transitionGMMPolicyPatternFile, patternFilePath));
        if (policyTruthPatternsDiffer) {
            cout << "WARNING: candidateFeatureSource=" << candidateFeatureSource
                 << " uses VECBEE policy patterns; exact shadow equality is "
                 << "guaranteed only when policy and truth patterns match"
                 << endl;
        }
    }
    // Complete gain-ordered validation needs no error estimate for the whole
    // pool: it simulates every candidate capable of improving its incumbent.
    // Skip the redundant VECBEE pass only for plain simulation with no export.
    const bool lazyExactRanking = candidateValidationPolicy == "complete_size_gain"
        && !useVecbeeCandidateFeatures && !roundTransitionConfig.Enabled()
        && signedLACScorer == nullptr;
    if (!lazyExactRanking) {// this block is for memory management
    VECBEEMan vecbeeMan(
        isSign,
        outputNum,
        outputWidths,
        policySeed,
        transitionGMMPolicyFrames,
        metrType,
        lacType,
        distrType,
        transitionGMMPolicyPatternFile.empty()
            ? patternFilePath
            : transitionGMMPolicyPatternFile,
        nThread,
        roundTransitionConfig,
        vecbeeEarlyExitConfig
    );
    vecbeeMan.BatchErrEstPro(
        accNet,
        net,
        lacMan,
        uppBound,
        enableFastErrEst,
        backErrInt,
        useVecbeeCandidateFeatures
    );
    profVecbeeFeature = vecbeeMan.GetFeatureProfile();
    if (signedLACScorer != nullptr)
        roundTransitionRecords = vecbeeMan.GetTransitionGMMRecords();
    if (useVecbeeCandidateFeatures)
        roundVecbeeCombinedMetrics = vecbeeMan.GetLACCombinedMetrics();
    }
    if (useVecbeeCandidateFeatures &&
        roundVecbeeCombinedMetrics.size() !=
            static_cast<size_t>(lacMan.GetLacNum())) {
        cerr << "ERROR: VECBEE-derived candidate feature table is stale" << endl;
        exit(1);
    }
    for (int lacId = 0; lacId < lacMan.GetLacNum(); ++lacId) {
        auto pLac = lacMan.GetLac(lacId);
        if (lazyExactRanking) {
            pLac->SetErrPro(0);
            pLac->SetDeltaDesignErr(BigFlt(0));
            continue;
        }
        BigFlt candidateErr = BigFlt(pLac->GetErrPro()) / BigFlt(errScale);
        double rankingDelta = 0.0;
        if (useDesignErrorModel) {
            vector<double> candidateFullFeatures;
            if (rankWithVecbeeCandidateFeatures) {
                const auto& candidateMetrics =
                    roundVecbeeCombinedMetrics.at(lacId);
                if (IsSampleExactMetrics(candidateMetrics)) {
                    pLac->SetDeltaDesignErr(BigFlt(-backDesignErr));
                    continue;
                }
                vector<double> localFeatures = SelectDesignFeatureVector(
                    candidateMetrics.metricErr,
                    candidateMetrics.mapeVector,
                    candidateMetrics.multiMetrics
                );
                candidateFullFeatures = ExpandDesignFeatureVector(
                    localFeatures);
            }
            else
                candidateFullFeatures =
                    EstimateFullDesignFeatureVector(double(candidateErr));
            rankingDelta = EstimateCandidateRankingDelta(
                backFullFeatureVector,
                candidateFullFeatures,
                backDesignErr
            );
        }
        else {
            double candidateDesignErr = PredictDesignError(double(candidateErr));
            rankingDelta = candidateDesignErr - backDesignErr;
        }
        pLac->SetDeltaDesignErr(BigFlt(rankingDelta));
    }
    if (signedLACScorer != nullptr) {
        if (roundTransitionRecords.size() !=
            static_cast<size_t>(lacMan.GetLacNum())) {
            cerr << "ERROR: signed-LAC transition panel size is stale" << endl;
            exit(1);
        }
        for (int lacId = 0; lacId < lacMan.GetLacNum(); ++lacId) {
            auto resub = dynamic_pointer_cast<ResubLAC>(lacMan.GetLac(lacId));
            if (!resub) {
                cerr << "ERROR: signed-LAC panel contains a non-resub candidate"
                     << endl;
                exit(1);
            }
            const string expectedCandidateHash = StableTransitionCandidateHash(
                resub->GetReprStr());
            for (const auto& record: roundTransitionRecords[lacId]) {
                if (record.runId != runId ||
                    record.baseStateHash != roundTransitionConfig.baseStateHash ||
                    record.patternHash != roundTransitionConfig.patternHash ||
                    record.candidateHash != expectedCandidateHash) {
                    cerr << "ERROR: signed-LAC run/base/pattern/candidate "
                         << "provenance mismatch" << endl;
                    exit(1);
                }
            }
        }
        vector<double> predictions;
        string scorerError;
        if (!signedLACScorer->Score(
                roundTransitionRecords, lacMan.GetLacNum(), predictions,
                scorerError)) {
            cerr << "ERROR: signed-LAC scoring failed closed: "
                 << scorerError << endl;
            exit(1);
        }
        for (int lacId = 0; lacId < lacMan.GetLacNum(); ++lacId)
            lacMan.GetLac(lacId)->SetDeltaDesignErr(
                BigFlt(predictions[lacId]));
    }
    if (lazyExactRanking)
        cout << "[complete-validation] lazy exact simulation; no batch error estimation; "
             << "equal-gain/error ties follow stable generation order" << endl;
    profFastEst = ProfSeconds(profFastEstBeg);
    profFastEstCpu = ProfCpuSeconds(profFastEstCpuBeg);

    assert(lacType == LAC_TYPE::RESUB);
    auto profSortBeg = ProfClock::now();
    const clock_t profSortCpuBeg = std::clock();
    vector<LACPtr> candidates;
    candidates.reserve(lacMan.GetLacNum());
    for (int lacId = 0; lacId < lacMan.GetLacNum(); ++lacId)
        candidates.emplace_back(lacMan.GetLac(lacId));
    const bool gradientRanking = UseGradientCandidateRanking();
    const bool mlpGuardedRanking = candidateValidationPolicy == "mlp_guarded";
    const bool signedLACRanking = signedLACScorer != nullptr;
    const double rankingQCurrent = signedLACRanking
        ? signedLACScorerConfig.qCurrent : backDesignErr;
    const double rankingQMax = signedLACRanking
        ? signedLACScorerConfig.qMax : designErrUppBound;
    if (signedLACRanking || mlpGuardedRanking) {
        struct GuardedRankEntry {
            LACPtr candidate;
            int sizeGain;
            double delta;
            bool infeasible;
            bool saturated;
            bool replayable;
            string stableId;
        };
        vector<GuardedRankEntry> rankEntries;
        rankEntries.reserve(candidates.size());
        for (const auto& candidate: candidates) {
            auto resub = dynamic_pointer_cast<ResubLAC>(candidate);
            const int sizeGain = resub? resub->GetSizeGain(): INT_MIN;
            bool replayable = false;
            if (resub && sizeGain > 0) {
                int targId = resub->GetTargId();
                replayable = targId >= 0 && targId < backNet.GetIdMaxPlus1() &&
                    backNet.IsObj(targId) && backNet.IsNode(targId) &&
                    backNet.GetFanoutNum(targId) > 0;
                if (replayable) {
                    int targLev = backNet.GetObjLev(targId);
                    for (auto divId: resub->GetDivIds()) {
                        if (divId == targId || divId < 0 ||
                            divId >= backNet.GetIdMaxPlus1() ||
                            !backNet.IsObj(divId) ||
                            (backNet.IsNode(divId) &&
                             backNet.GetObjLev(divId) >= targLev)) {
                            replayable = false;
                            break;
                        }
                    }
                }
            }
            const double delta = double(candidate->GetDeltaDesignErr());
            const double estimatedLocalErr = double(BigFlt(candidate->GetErrPro()) /
                BigFlt(errScale));
            const bool saturated = useDesignErrorModel &&
                isfinite(localErrUppBound) && localErrUppBound > 0.0 &&
                estimatedLocalErr >= 0.95 * localErrUppBound;
            rankEntries.push_back({
                candidate,
                sizeGain,
                delta,
                rankingQCurrent + delta > rankingQMax,
                saturated,
                replayable,
                resub? StableTransitionCandidateHash(resub->GetReprStr()): ""
            });
        }
        if (mlpGuardedRanking && !signedLACRanking) {
            vector<int> lowErrorOrder(rankEntries.size());
            vector<int> sizeOrder(rankEntries.size());
            vector<int> scoreOrder(rankEntries.size());
            iota(lowErrorOrder.begin(), lowErrorOrder.end(), 0);
            iota(sizeOrder.begin(), sizeOrder.end(), 0);
            iota(scoreOrder.begin(), scoreOrder.end(), 0);

            auto active = [&](int id) {
                return rankEntries[id].replayable;
            };
            auto stableTie = [&](int lhs, int rhs) {
                return rankEntries[lhs].stableId < rankEntries[rhs].stableId;
            };
            auto feasibleActiveFirst = [&](int lhs, int rhs) -> optional<bool> {
                if (active(lhs) != active(rhs))
                    return optional<bool>(active(lhs));
                if (rankEntries[lhs].infeasible != rankEntries[rhs].infeasible)
                    return optional<bool>(!rankEntries[lhs].infeasible);
                if (rankEntries[lhs].saturated != rankEntries[rhs].saturated)
                    return optional<bool>(!rankEntries[lhs].saturated);
                return optional<bool>();
            };

            sort(lowErrorOrder.begin(), lowErrorOrder.end(), [&](int lhs, int rhs) {
                if (auto first = feasibleActiveFirst(lhs, rhs))
                    return *first;
                if (rankEntries[lhs].delta != rankEntries[rhs].delta)
                    return rankEntries[lhs].delta < rankEntries[rhs].delta;
                if (rankEntries[lhs].sizeGain != rankEntries[rhs].sizeGain)
                    return rankEntries[lhs].sizeGain > rankEntries[rhs].sizeGain;
                return stableTie(lhs, rhs);
            });
            sort(sizeOrder.begin(), sizeOrder.end(), [&](int lhs, int rhs) {
                if (auto first = feasibleActiveFirst(lhs, rhs))
                    return *first;
                if (rankEntries[lhs].sizeGain != rankEntries[rhs].sizeGain)
                    return rankEntries[lhs].sizeGain > rankEntries[rhs].sizeGain;
                if (rankEntries[lhs].delta != rankEntries[rhs].delta)
                    return rankEntries[lhs].delta < rankEntries[rhs].delta;
                return stableTie(lhs, rhs);
            });
            sort(scoreOrder.begin(), scoreOrder.end(), [&](int lhs, int rhs) {
                if (auto first = feasibleActiveFirst(lhs, rhs))
                    return *first;
                const double lhsScore = rankEntries[lhs].delta /
                    static_cast<double>(max(1, rankEntries[lhs].sizeGain));
                const double rhsScore = rankEntries[rhs].delta /
                    static_cast<double>(max(1, rankEntries[rhs].sizeGain));
                if (lhsScore != rhsScore)
                    return lhsScore < rhsScore;
                if (rankEntries[lhs].sizeGain != rankEntries[rhs].sizeGain)
                    return rankEntries[lhs].sizeGain > rankEntries[rhs].sizeGain;
                return stableTie(lhs, rhs);
            });

            vector<int> paretoOrder;
            int bestGain = INT_MIN;
            for (int id: lowErrorOrder) {
                if (!active(id) || rankEntries[id].infeasible)
                    continue;
                if (rankEntries[id].sizeGain > bestGain) {
                    paretoOrder.emplace_back(id);
                    bestGain = rankEntries[id].sizeGain;
                }
            }

            vector<char> selected(rankEntries.size(), false);
            vector<int> preferredIds;
            preferredIds.reserve(rankEntries.size());
            auto addId = [&](int id) {
                if (id < 0 || id >= static_cast<int>(rankEntries.size()) ||
                    selected[id])
                    return false;
                selected[id] = true;
                preferredIds.emplace_back(id);
                return true;
            };
            auto addFrom = [&](const vector<int>& order, int quota) {
                int added = 0;
                for (int id: order) {
                    if (addId(id) && ++added >= quota)
                        break;
                }
            };

            const int panel = max(4, max(1, maxExactCandValidate));
            const int lowQuota = max(1, panel / 2);
            const int gainQuota = max(1, panel / 4);
            const int paretoQuota = max(1, panel / 4);
            addFrom(lowErrorOrder, lowQuota);
            addFrom(sizeOrder, gainQuota);
            addFrom(paretoOrder, paretoQuota);
            addFrom(scoreOrder, panel);
            addFrom(lowErrorOrder, static_cast<int>(rankEntries.size()));
            addFrom(paretoOrder, static_cast<int>(rankEntries.size()));
            addFrom(sizeOrder, static_cast<int>(rankEntries.size()));
            addFrom(scoreOrder, static_cast<int>(rankEntries.size()));
            for (int id = 0; id < static_cast<int>(rankEntries.size()); ++id)
                addId(id);

            cout << "mlp_guarded mixed ranking panel: low_error="
                 << lowQuota << ", size_gain=" << gainQuota
                 << ", pareto=" << paretoQuota
                 << ", exact_validation_limit=" << maxExactCandValidate << endl;

            candidates.clear();
            candidates.reserve(preferredIds.size());
            for (int id: preferredIds)
                candidates.emplace_back(rankEntries[id].candidate);
        }
        else {
            sort(rankEntries.begin(), rankEntries.end(),
                [](const GuardedRankEntry& lhs, const GuardedRankEntry& rhs) {
                    if (lhs.infeasible != rhs.infeasible)
                        return !lhs.infeasible;
                    if (lhs.sizeGain != rhs.sizeGain)
                        return lhs.sizeGain > rhs.sizeGain;
                    if (lhs.delta != rhs.delta)
                        return lhs.delta < rhs.delta;
                    return lhs.stableId < rhs.stableId;
                });
            candidates.clear();
            candidates.reserve(rankEntries.size());
            for (const auto& entry: rankEntries)
                candidates.emplace_back(entry.candidate);
        }
    }
    else {
        auto compareCandidates = [gradientRanking](
                const LACPtr& lhs, const LACPtr& rhs) {
            auto lhsResub = dynamic_pointer_cast<ResubLAC>(lhs);
            auto rhsResub = dynamic_pointer_cast<ResubLAC>(rhs);
            int lhsGain = lhsResub? lhsResub->GetSizeGain(): INT_MIN;
            int rhsGain = rhsResub? rhsResub->GetSizeGain(): INT_MIN;
            if (gradientRanking) {
                if (lhs->GetDeltaDesignErr() != rhs->GetDeltaDesignErr())
                    return lhs->GetDeltaDesignErr() < rhs->GetDeltaDesignErr();
                return lhsGain > rhsGain;
            }
            if (lhsGain != rhsGain)
                return lhsGain > rhsGain;
            return lhs->GetDeltaDesignErr() < rhs->GetDeltaDesignErr();
        };
        if (lazyExactRanking)
            stable_sort(candidates.begin(), candidates.end(), compareCandidates);
        else
            sort(candidates.begin(), candidates.end(), compareCandidates);
    }
    if (!signedLACRanking && !mlpGuardedRanking &&
        UseStratifiedCandidateOrdering() && !candidates.empty()) {
        vector<int> preferredIds;
        vector<bool> selected(candidates.size(), false);
        auto addPreferred = [&](int id) {
            if (id < 0 || id >= static_cast<int>(candidates.size()) || selected[id])
                return false;
            selected[id] = true;
            preferredIds.emplace_back(id);
            return true;
        };
        for (int id = 0; id < min<int>(8, candidates.size()); ++id)
            addPreferred(id);

        if (gradientRanking) {
            vector<int> sizeOrder(candidates.size());
            iota(sizeOrder.begin(), sizeOrder.end(), 0);
            sort(sizeOrder.begin(), sizeOrder.end(), [&](int lhs, int rhs) {
                auto lhsResub = dynamic_pointer_cast<ResubLAC>(candidates[lhs]);
                auto rhsResub = dynamic_pointer_cast<ResubLAC>(candidates[rhs]);
                int lhsGain = lhsResub? lhsResub->GetSizeGain(): INT_MIN;
                int rhsGain = rhsResub? rhsResub->GetSizeGain(): INT_MIN;
                if (lhsGain != rhsGain)
                    return lhsGain > rhsGain;
                return candidates[lhs]->GetDeltaDesignErr() <
                       candidates[rhs]->GetDeltaDesignErr();
            });
            int sizeAdded = 0;
            for (int id: sizeOrder) {
                if (addPreferred(id) && ++sizeAdded == 4)
                    break;
            }
        }

        vector<int> errorOrder(candidates.size());
        iota(errorOrder.begin(), errorOrder.end(), 0);
        sort(errorOrder.begin(), errorOrder.end(), [&](int lhs, int rhs) {
            if (candidates[lhs]->GetDeltaDesignErr() != candidates[rhs]->GetDeltaDesignErr())
                return candidates[lhs]->GetDeltaDesignErr() < candidates[rhs]->GetDeltaDesignErr();
            auto lhsResub = dynamic_pointer_cast<ResubLAC>(candidates[lhs]);
            auto rhsResub = dynamic_pointer_cast<ResubLAC>(candidates[rhs]);
            int lhsGain = lhsResub? lhsResub->GetSizeGain(): INT_MIN;
            int rhsGain = rhsResub? rhsResub->GetSizeGain(): INT_MIN;
            return lhsGain > rhsGain;
        });
        int lowErrorAdded = 0;
        for (int id: errorOrder) {
            if (addPreferred(id) && ++lowErrorAdded == 4)
                break;
        }
        for (int quantile = 1; quantile <= 4; ++quantile) {
            int id = static_cast<int>((static_cast<long long>(quantile) * (candidates.size() - 1)) / 5);
            addPreferred(id);
        }
        for (int id = 0; id < static_cast<int>(candidates.size()); ++id)
            addPreferred(id);

        vector<LACPtr> ordered;
        ordered.reserve(candidates.size());
        for (int id: preferredIds)
            ordered.emplace_back(candidates[id]);
        candidates.swap(ordered);
    }
    profSort = ProfSeconds(profSortBeg);
    profSortCpu = ProfCpuSeconds(profSortCpuBeg);

    bool hasSignedExactDecision = false;
    string signedAcceptedCandidateHash;
    double signedAcceptedDeltaQ = 0.0;
    if (signedLACRanking) {
        const auto panelBegin = ProfClock::now();
        const clock_t panelCpuBegin = std::clock();
        const filesystem::path controllerRoot(
            signedLACScorerConfig.controllerDirectory);
        ostringstream roundName;
        roundName << "round_" << setw(4) << setfill('0') << round;
        const filesystem::path roundDirectory = controllerRoot / roundName.str();
        filesystem::create_directories(roundDirectory);
        const filesystem::path basePath = roundDirectory / "base.blif";
        backNet.WriteBlif(basePath.string());

        struct PanelCandidate {
            LACPtr lac;
            string hash;
            int gain;
            double predictedDelta;
            string netlist;
        };
        vector<PanelCandidate> panelCandidates;
        panelCandidates.reserve(min<int>(
            signedLACScorerConfig.topB, candidates.size()));
        const string savedLastAppliedLac = lastAppliedLac;
        for (const auto& candidate: candidates) {
            if (static_cast<int>(panelCandidates.size()) >=
                signedLACScorerConfig.topB)
                break;
            auto resub = dynamic_pointer_cast<ResubLAC>(candidate);
            if (!resub || resub->GetSizeGain() <= 0)
                continue;
            const string candidateHash = StableTransitionCandidateHash(
                resub->GetReprStr());
            auto candidateNet = backNet;
            ApplyLacPro(candidateNet, candidate, backErr);
            const filesystem::path candidatePath = roundDirectory /
                ("candidate_" + candidateHash.substr(7, 16) + ".blif");
            candidateNet.WriteBlif(candidatePath.string());
            panelCandidates.push_back({
                candidate, candidateHash, resub->GetSizeGain(),
                static_cast<double>(candidate->GetDeltaDesignErr()),
                filesystem::absolute(candidatePath).lexically_normal().string(),
            });
        }
        lastAppliedLac = savedLastAppliedLac;
        if (panelCandidates.empty()) {
            cerr << "ERROR: signed-LAC ranking produced no replayable candidate"
                 << endl;
            exit(1);
        }
        const filesystem::path panelPath = roundDirectory / "panel.json";
        const filesystem::path decisionPath = roundDirectory / "decision.json";
        ostringstream panel;
        panel << setprecision(17)
              << "{\"schema\":\"hals_signed_lac_candidate_panel_v1\""
              << ",\"run_id\":\"" << JsonEscape(runId) << "\""
              << ",\"round\":" << round
              << ",\"graph_sha256\":\""
              << JsonEscape(roundTransitionConfig.graphHash) << "\""
              << ",\"base_state_sha256\":\""
              << JsonEscape(roundTransitionConfig.baseStateHash) << "\""
              << ",\"model_sha256\":\""
              << JsonEscape(signedLACScorerConfig.modelHash) << "\""
              << ",\"prototype_sha256\":\""
              << JsonEscape(signedLACScorerConfig.prototypeHash) << "\""
              << ",\"pattern_sha256\":\""
              << JsonEscape(roundTransitionConfig.patternHash) << "\""
              << ",\"wrapper_sha256\":\""
              << JsonEscape(signedLACScorerConfig.wrapperHash) << "\""
              << ",\"ir_sha256\":\""
              << JsonEscape(signedLACScorerConfig.irHash) << "\""
              << ",\"truth_pattern_sha256\":\""
              << JsonEscape(signedLACScorerConfig.truthPatternHash) << "\""
              << ",\"execution_backend\":\""
              << JsonEscape(signedLACScorerConfig.executionBackend) << "\""
              << ",\"metric\":\""
              << JsonEscape(signedLACScorerConfig.metric) << "\""
              << ",\"q_current\":" << JsonDouble(signedLACScorerConfig.qCurrent)
              << ",\"q_max\":" << JsonDouble(signedLACScorerConfig.qMax)
              << ",\"metric_bounds\":{\"MAPE\":"
              << JsonDouble(signedLACScorerConfig.mapeBound)
              << ",\"NMED\":" << JsonDouble(signedLACScorerConfig.nmedBound)
              << ",\"NMSE\":" << JsonDouble(signedLACScorerConfig.nmseBound)
              << ",\"MSE\":" << JsonDouble(signedLACScorerConfig.mseBound)
              << "}"
              << ",\"top_b\":" << panelCandidates.size()
              << ",\"base_netlist\":\""
              << JsonEscape(filesystem::absolute(basePath)
                    .lexically_normal().string()) << "\""
              << ",\"candidates\":[";
        for (size_t index = 0; index < panelCandidates.size(); ++index) {
            if (index)
                panel << ",";
            const auto& candidate = panelCandidates[index];
            panel << "{\"candidate_sha256\":\""
                  << JsonEscape(candidate.hash) << "\""
                  << ",\"predicted_delta_q\":"
                  << JsonDouble(candidate.predictedDelta)
                  << ",\"hardware_gain\":" << candidate.gain
                  << ",\"candidate_netlist\":\""
                  << JsonEscape(candidate.netlist) << "\"}";
        }
        panel << "]}\n";
        const string panelContents = panel.str();
        if (!AtomicWriteText(panelPath.string(), panelContents)) {
            cerr << "ERROR: cannot write signed-LAC candidate panel: "
                 << panelPath << endl;
            exit(1);
        }
        profSignedPanel = ProfSeconds(panelBegin);
        profSignedPanelCpu = ProfCpuSeconds(panelCpuBegin);
        const string panelHash = StableTransitionCandidateHash(panelContents);
        cout << "[signed-LAC] waiting for exact decision " << decisionPath
             << " panel=" << panelHash << endl;
        const auto waitBegin = ProfClock::now();
        const clock_t waitCpuBegin = std::clock();
        while (true) {
            auto decision = ReadCompleteJson(decisionPath.string());
            bool matches = false;
            if (decision) {
                auto schema = ReadJsonString(*decision, "schema");
                auto decisionRun = ReadJsonString(*decision, "run_id");
                auto decisionRound = ReadJsonInteger(*decision, "round");
                auto graph = ReadJsonString(*decision, "graph_sha256");
                auto base = ReadJsonString(*decision, "base_state_sha256");
                auto boundPanel = ReadJsonString(*decision, "panel_sha256");
                auto proof = ReadJsonString(*decision, "proof");
                matches = schema && *schema == "hals_signed_lac_exact_decision_v1"
                    && decisionRun && *decisionRun == runId
                    && decisionRound && *decisionRound == round
                    && graph && *graph == roundTransitionConfig.graphHash
                    && base && *base == roundTransitionConfig.baseStateHash
                    && boundPanel && *boundPanel == panelHash
                    && proof && *proof == "exact_whole_design_top_b";
                if (matches) {
                    auto accepted = ReadJsonString(
                        *decision, "accepted_candidate_sha256");
                    if (accepted) {
                        auto found = find_if(
                            panelCandidates.begin(), panelCandidates.end(),
                            [&](const PanelCandidate& value) {
                                return value.hash == *accepted;
                            });
                        auto delta = ReadJsonNumber(*decision, "accepted_delta_q");
                        if (found == panelCandidates.end() || !delta ||
                            !isfinite(*delta) || DoubleGreat(
                                signedLACScorerConfig.qCurrent + *delta,
                                signedLACScorerConfig.qMax)) {
                            cerr << "ERROR: exact controller accepted an absent, "
                                 << "nonfinite, or infeasible candidate" << endl;
                            exit(1);
                        }
                        signedAcceptedCandidateHash = *accepted;
                        signedAcceptedDeltaQ = *delta;
                        candidates.assign(1, found->lac);
                    }
                    else {
                        candidates.clear();
                    }
                    hasSignedExactDecision = true;
                    profSignedController = ProfSeconds(waitBegin);
                    profSignedControllerCpu = ProfCpuSeconds(waitCpuBegin);
                    break;
                }
            }
            if (ProfSeconds(waitBegin) >=
                signedLACScorerConfig.decisionTimeoutSeconds) {
                cerr << "ERROR: signed-LAC exact controller timed out or only "
                     << "provided stale decisions" << endl;
                exit(1);
            }
            this_thread::sleep_for(chrono::milliseconds(
                signedLACScorerConfig.decisionPollMilliseconds));
        }
    }

    // Complete the existing lexicographic (size gain, then error) choice.
    // A lower-gain tail cannot improve a feasible incumbent, but every gain
    // tie must still be evaluated. No feasible incumbent means no pruning.
    const bool completeSizeGain = candidateValidationPolicy == "complete_size_gain";
    const int VALIDATION_LIMIT = completeSizeGain
        ? max<int>(1, candidates.size()) : max(1, maxExactCandValidate);
    const bool mlpGuardedPolicy = candidateValidationPolicy == "mlp_guarded";
    const int FALLBACK_VALIDATION_LIMIT = mlpGuardedPolicy
        ? max(VALIDATION_LIMIT, min(32, max(8, VALIDATION_LIMIT * 4)))
        : VALIDATION_LIMIT;
    const int SCAN_LIMIT = max(4096, FALLBACK_VALIDATION_LIMIT);
    const double ZERO_DESIGN_ERR_EPS = 1e-15;
    bool foundPositiveLac = false;
    bool foundFallbackLac = false;
    double err = backErr;
    vector<double> errorVector;
    NetMan selectedNet = backNet;
    NetMan fallbackNet = backNet;
    string selectedLac;
    string fallbackLac;
    double selectedDesignErr = DBL_MAX;
    double fallbackDesignErr = DBL_MAX;
    double fallbackErr = backErr;
    vector<double> fallbackErrorVector;
    ErrorMetrics selectedMultiMetrics;
    ErrorMetrics fallbackMultiMetrics;
    int selectedSizeGain = INT_MIN;
    int fallbackSizeGain = INT_MIN;
    int validatedCandNum = 0;
    int scannedCandNum = 0;
    int skippedZeroEstCandNum = 0;
    int checkedCandNum = min<int>(SCAN_LIMIT, candidates.size());
    int rejectedOutOfRangeCandNum = 0;
    int rejectedInfeasibleCandNum = 0;
    int vecbeeFeatureComparedCandNum = 0;
    int vecbeeFeatureMismatchCandNum = 0;
    double vecbeeFeatureMaxAbsDiff = 0.0;
    string vecbeeFeatureMaxDiffCandidateHash;
    bool announcedFallbackValidation = false;
    bool allowZeroFallback = zeroErrorFallbackRounds < maxZeroErrorFallbackRounds;
    auto isBackNetObj = [&backNet](int id) {
        return id >= 0 && id < backNet.GetIdMaxPlus1() && backNet.IsObj(id);
    };
    auto profCandidateLoopBeg = ProfClock::now();
    const clock_t profCandidateLoopCpuBeg = std::clock();
    for (int candId = 0; candId < checkedCandNum; ++candId) {
        ++scannedCandNum;
        auto pCandLac = candidates[candId];
        auto pCandResub = dynamic_pointer_cast<ResubLAC>(pCandLac);
        if (!pCandResub) {
            cout << "skip invalid non-resub LAC #" << candId << endl;
            continue;
        }
        if (completeSizeGain && foundPositiveLac &&
            pCandResub->GetSizeGain() < selectedSizeGain) {
            --scannedCandNum;
            cout << "[complete-validation] remaining lower-gain candidates "
                 << "cannot improve feasible incumbent; selected_gain="
                 << selectedSizeGain << endl;
            break;
        }
        if (pCandResub->GetSizeGain() <= 0) {
            cout << "skip non-reducing LAC #" << candId
                 << ": size gain = " << pCandResub->GetSizeGain() << endl;
            continue;
        }
        int targId = pCandResub->GetTargId();
        if (!isBackNetObj(targId) || !backNet.IsNode(targId) || backNet.GetFanoutNum(targId) == 0) {
            cout << "skip invalid LAC #" << candId << ": bad target " << targId << endl;
            continue;
        }
        bool validDivs = true;
        int targLev = backNet.GetObjLev(targId);
        for (auto divId: pCandResub->GetDivIds()) {
            if (divId == targId || !isBackNetObj(divId)) {
                cout << "skip invalid LAC #" << candId << ": bad divisor " << divId << endl;
                validDivs = false;
                break;
            }
            if (backNet.IsNode(divId) && backNet.GetObjLev(divId) >= targLev) {
                cout << "skip invalid LAC #" << candId << ": divisor " << divId
                     << " is not topologically before target" << endl;
                validDivs = false;
                break;
            }
        }
        if (!validDivs)
            continue;
        const int activeValidationLimit =
            (mlpGuardedPolicy && !foundPositiveLac)
                ? FALLBACK_VALIDATION_LIMIT
                : VALIDATION_LIMIT;
        if (validatedCandNum >= activeValidationLimit)
            break;
        if (mlpGuardedPolicy && !foundPositiveLac &&
            validatedCandNum >= VALIDATION_LIMIT &&
            !announcedFallbackValidation) {
            cout << "mlp_guarded fallback validation: top "
                 << VALIDATION_LIMIT << " exact candidates produced no "
                 << "feasible LAC; continue up to "
                 << FALLBACK_VALIDATION_LIMIT << " validations" << endl;
            announcedFallbackValidation = true;
        }
        ++validatedCandNum;
        const string candidateHash = StableTransitionCandidateHash(
            pCandResub->GetReprStr());
        cout << "try LAC #" << candId
             << ": size gain = " << pCandResub->GetSizeGain()
             << ", estimated delta design error = " << pCandLac->GetDeltaDesignErr() << endl;

        auto profApplyBeg = ProfClock::now();
        const clock_t profApplyCpuBeg = std::clock();
        auto candidateNet = backNet;
        ApplyLacPro(candidateNet, pCandLac, backErr);
        string candidateLac = lastAppliedLac;
        profCandidateApply += ProfSeconds(profApplyBeg);
        profCandidateApplyCpu += ProfCpuSeconds(profApplyCpuBeg);

        auto profCombinedBeg = ProfClock::now();
        const clock_t profCombinedCpuBeg = std::clock();
        CombinedErrorMetrics candidateCombinedMetrics;
        if (useVecbeeCandidateFeatures) {
            auto lacIdIter = originalLacIdByHash.find(candidateHash);
            if (lacIdIter == originalLacIdByHash.end()) {
                cerr << "ERROR: cannot map candidate hash back to VECBEE "
                     << "feature table: " << candidateHash << endl;
                exit(1);
            }
            candidateCombinedMetrics =
                roundVecbeeCombinedMetrics.at(lacIdIter->second);
        }
        if (!useVecbeeCandidateFeatures || shadowVecbeeCandidateFeatures) {
            auto simulatedCombinedMetrics = CalcCombinedErrorMetrics(
                accSmlt,
                candidateNet,
                isSign,
                seed,
                nFrame,
                metrType,
                distrType,
                outputNum,
                patternFilePath,
                outputWidths
            );
            if (shadowVecbeeCandidateFeatures) {
                ++vecbeeFeatureComparedCandNum;
                const double diff = CombinedMetricMaxAbsDiff(
                    candidateCombinedMetrics,
                    simulatedCombinedMetrics
                );
                if (diff > vecbeeFeatureMaxAbsDiff) {
                    vecbeeFeatureMaxAbsDiff = diff;
                    vecbeeFeatureMaxDiffCandidateHash = candidateHash;
                }
                if (diff > 1e-9) {
                    ++vecbeeFeatureMismatchCandNum;
                    if (vecbeeFeatureMismatchCandNum <= 8) {
                        cout << "[vecbee-feature-check] mismatch"
                             << " candidate=" << candidateHash
                             << " max_abs_diff=" << setprecision(12) << diff
                             << " vecbee_metric="
                             << candidateCombinedMetrics.metricErr
                             << " simulated_metric="
                             << simulatedCombinedMetrics.metricErr
                             << endl;
                    }
                }
                candidateCombinedMetrics = simulatedCombinedMetrics;
            }
            else
                candidateCombinedMetrics = simulatedCombinedMetrics;
        }
        double candidateErr = candidateCombinedMetrics.metricErr;
        profCandidateCombinedMetrics += ProfSeconds(profCombinedBeg);
        profCandidateCombinedMetricsCpu += ProfCpuSeconds(profCombinedCpuBeg);
        cout << "candidate " << metrType << " = " << candidateErr << endl;
        vector<double> candidateErrorVector;
        candidateErrorVector = candidateCombinedMetrics.mapeVector;
        ErrorMetrics candidateMultiMetrics = candidateCombinedMetrics.multiMetrics;
        cout << "candidate local metrics: NMED=" << candidateMultiMetrics.nmed
             << " NMSE=" << candidateMultiMetrics.nmse
             << " ER=" << candidateMultiMetrics.er
             << " ME=" << candidateMultiMetrics.me << endl;
        auto profModelBeg = ProfClock::now();
        const clock_t profModelCpuBeg = std::clock();
        vector<double> candidateFeatureVector = SelectDesignFeatureVector(candidateErr, candidateErrorVector, candidateMultiMetrics);
        double candidateDesignErr = IsSampleExactMetrics(candidateCombinedMetrics)
            ? 0.0
            : PredictDesignError(candidateFeatureVector);
        if (hasSignedExactDecision)
            candidateDesignErr = signedLACScorerConfig.qCurrent
                + signedAcceptedDeltaQ;
        profCandidateModel += ProfSeconds(profModelBeg);
        profCandidateModelCpu += ProfCpuSeconds(profModelCpuBeg);
        cout << "candidate predicted design error = " << candidateDesignErr << endl;

        double estErr = double(BigFlt(pCandLac->GetErrPro()) / BigFlt(errScale));
        if (!lazyExactRanking && !enableFastErrEst && !DoubleEqual(estErr, candidateErr, 1e-4)) {
            cout << "================== WARNING: wrong error estimation ================" << endl;
            cout << setprecision(10) << estErr << "\t" << candidateErr << endl;
            cout << "use exact simulated candidate error for selection" << endl;
        }

        vector<double> candidateFullFeatures = useDesignErrorModel
            ? ExpandDesignFeatureVector(candidateFeatureVector)
            : candidateFeatureVector;
        bool candidateFeatureInRange = !useDesignErrorModel ||
            designErrorModel.InFeatureRange(candidateFullFeatures);
        bool candidateLocalFeatureWithinUnit = true;
        if (useDesignErrorModel) {
            for (auto value: candidateFeatureVector) {
                if (DoubleGreat(value, 1.0)) {
                    candidateLocalFeatureWithinUnit = false;
                    break;
                }
            }
        }
        bool feasible = hasSignedExactDecision
            ? !DoubleGreat(candidateDesignErr, signedLACScorerConfig.qMax)
            : candidateFeatureInRange && candidateLocalFeatureWithinUnit &&
              DoubleLessEqual(candidateDesignErr, designErrUppBound);
        if (!candidateAuditDir.empty() &&
            static_cast<int>(candidateAuditRecords.size()) < candidateAuditLimit) {
            ostringstream name;
            name << "round_" << setw(4) << setfill('0') << round
                 << "_candidate_" << setw(4) << setfill('0')
                 << candidateAuditRecords.size() << ".blif";
            const filesystem::path path = filesystem::path(candidateAuditDir)
                / name.str();
            candidateNet.WriteBlif(path.string());
            CandidateAuditRecord record;
            record.round = round;
            record.phase = boundPhase;
            record.candidateRank = candId;
            record.validationIndex = validatedCandNum - 1;
            record.sizeGain = pCandResub->GetSizeGain();
            record.searchSeed = searchSeed;
            record.policySeed = policySeed;
            record.truthSeed = truthSeed;
            record.searchFrames = nFrame4ResubGen;
            record.policyFrames = transitionGMMPolicyFrames;
            record.truthFrames = nFrame;
            record.baseKernelErr = backErr;
            record.baseDesignErr = backDesignErr;
            record.estimatedKernelErr = estErr;
            record.measuredKernelErr = candidateErr;
            record.baseErrorVector = backErrorVector;
            record.errorVector = candidateErrorVector;
            record.baseMultiMetrics = backMultiMetrics;
            record.multiMetrics = candidateMultiMetrics;
            record.localFeatures = candidateFeatureVector;
            record.fullFeatures = candidateFullFeatures;
            record.predictedDesignErr = candidateDesignErr;
            record.featureInRange = candidateFeatureInRange;
            record.feasible = feasible;
            {
                ostringstream metricName;
                metricName << metrType;
                record.metricType = metricName.str();
            }
            record.candidateFeatureSource = candidateFeatureSource;
            record.candidateHash = candidateHash;
            record.baseStateHash = roundTransitionConfig.baseStateHash;
            record.policyPatternHash = roundTransitionConfig.patternHash;
            record.lac = candidateLac;
            record.baseNetlistPath = candidateAuditBaseNetlist;
            record.candidateNetlistPath = filesystem::absolute(path)
                .lexically_normal().string();
            candidateAuditRecords.emplace_back(std::move(record));
        }
        if (!feasible) {
            if (!candidateFeatureInRange)
                ++rejectedOutOfRangeCandNum;
            else
                ++rejectedInfeasibleCandNum;
            if (!candidateFeatureInRange)
                cout << "candidate is outside design model feature range" << endl;
            cout << "candidate violates design error bound" << endl;
            cout << "reject this LAC before synthesis/mapping" << endl;
            continue;
        }

        double candidateDeltaDesignErr = hasSignedExactDecision
            ? signedAcceptedDeltaQ
            : candidateDesignErr - backDesignErr;
        bool nonIncreasingDesignErr = !DoubleGreat(candidateDeltaDesignErr, 0.0, ZERO_DESIGN_ERR_EPS);
        if (nonIncreasingDesignErr)
            cout << "accept non-increasing-error reducing LAC as normal candidate" << endl;

        bool betterSizeGain = pCandResub->GetSizeGain() > selectedSizeGain;
        bool equalGainBetterDesignErr = pCandResub->GetSizeGain() == selectedSizeGain &&
                                        DoubleLess(candidateDesignErr, selectedDesignErr);
        if (!foundPositiveLac || betterSizeGain || equalGainBetterDesignErr) {
            selectedNet = candidateNet;
            selectedLac = candidateLac;
            err = candidateErr;
            errorVector = candidateErrorVector;
            selectedMultiMetrics = candidateMultiMetrics;
            selectedDesignErr = candidateDesignErr;
            selectedSizeGain = pCandResub->GetSizeGain();
            foundPositiveLac = true;
        }
    }
    profCandidateLoop = ProfSeconds(profCandidateLoopBeg);
    profCandidateLoopCpu = ProfCpuSeconds(profCandidateLoopCpuBeg);
    roundCandidatesScanned = scannedCandNum;
    roundExactValidations = validatedCandNum;
    FinalizeRoundAccounting();

    if (useVecbeeCandidateFeatures) {
        cout << "[vecbee-feature-source] mode=" << candidateFeatureSource
             << " validated=" << validatedCandNum
             << " extracted=" << roundVecbeeCombinedMetrics.size()
             << endl;
    }
    if (shadowVecbeeCandidateFeatures) {
        cout << "[vecbee-feature-check] compared="
             << vecbeeFeatureComparedCandNum
             << " mismatches=" << vecbeeFeatureMismatchCandNum
             << " max_abs_diff=" << setprecision(12)
             << vecbeeFeatureMaxAbsDiff
             << " max_diff_candidate="
             << (vecbeeFeatureMaxDiffCandidateHash.empty()
                    ? "none"
                    : vecbeeFeatureMaxDiffCandidateHash)
             << endl;
    }

    cout << "validated candidates = " << validatedCandNum
         << ", scanned candidates = " << scannedCandNum
         << ", generated candidates = " << candidates.size()
         << ", skipped estimated 0-error candidates = " << skippedZeroEstCandNum
         << ", rejected OOD candidates = " << rejectedOutOfRangeCandNum
         << ", rejected infeasible candidates = " << rejectedInfeasibleCandNum
         << endl;

    bool selectedZeroFallback = false;
    if (!foundPositiveLac && foundFallbackLac) {
        selectedNet = fallbackNet;
        selectedLac = fallbackLac;
        err = fallbackErr;
        errorVector = fallbackErrorVector;
        selectedMultiMetrics = fallbackMultiMetrics;
        selectedDesignErr = fallbackDesignErr;
        selectedSizeGain = fallbackSizeGain;
        selectedZeroFallback = true;
        cout << "select 0-error fallback LAC, fallback round "
             << (zeroErrorFallbackRounds + 1) << "/" << maxZeroErrorFallbackRounds << endl;
    }

    WriteCandidateAuditRecords(candidateAuditRecords, selectedLac);

    if (!foundPositiveLac && !foundFallbackLac) {
        cout << "no feasible reducing LAC found";
        if (!allowZeroFallback)
            cout << " and 0-error fallback budget is exhausted";
        cout << " among scanned " << scannedCandNum << " candidates"
             << " (exact validations " << validatedCandNum << ")" << endl;
        net = backNet;
        lastAppliedLac.clear();
        lastStopReason = StopReason::NO_FEASIBLE_REDUCING_LAC;
        printRoundPhaseProfile();
        return DBL_MAX;
    }

    if (selectedZeroFallback)
        ++zeroErrorFallbackRounds;
    else
        zeroErrorFallbackRounds = 0;

    net = selectedNet;
    lastAppliedLac = selectedLac;
    if (hasSignedExactDecision)
        signedLACScorerConfig.qCurrent = selectedDesignErr;
    cout << "current " << metrType << " = " << err << endl;
    cout << "current predicted design error = " << selectedDesignErr
         << ", delta = " << selectedDesignErr - backDesignErr << endl;
    if (metrType == METR_TYPE::MED)
        cout << "NMED = " << selectedMultiMetrics.nmed << endl;

    // Rebuild the accepted SOP before the next ALS round.  Direct ABC object
    // replacement can leave stale fanout/id bookkeeping in some large SOPs,
    // which later breaks Boolean-difference based candidate estimation.
    net.Comm("st; logic; sop;");
    if (!net.Check()) {
        cout << "WARNING: accepted LAC produced an invalid network after rebuild; stop ALS" << endl;
        net = backNet;
        lastAppliedLac.clear();
        lastStopReason = StopReason::NO_FEASIBLE_REDUCING_LAC;
        return DBL_MAX;
    }
    if (transitionGMMConfig.Enabled()) {
        transitionGMMConfig.baseStateHash = StableTransitionCandidateHash(
            transitionGMMConfig.baseStateHash + "\n" + selectedLac
        );
    }

    // simplify without errors
    auto profSimplifyBeg = ProfClock::now();
    const clock_t profSimplifyCpuBeg = std::clock();
    const ll SYNTH_ROUND = 10;
    if (round % SYNTH_ROUND == 0 || (!useDesignErrorModel && backErr > 0.5 * errUppBound))
        ExactSimpl(net);
    else {
        net.CleanUp();
        net.MergeConst();
        net.PrintStat();
    }
    profSimplify = ProfSeconds(profSimplifyBeg);
    profSimplifyCpu = ProfCpuSeconds(profSimplifyCpuBeg);

    // measure, synthesis & mapping, output
    auto profEvalBeg = ProfClock::now();
    const clock_t profEvalCpuBeg = std::clock();
    Eval(net, err);
    profEval = ProfSeconds(profEvalBeg);
    profEvalCpu = ProfCpuSeconds(profEvalCpuBeg);

    printRoundPhaseProfile();
    cout << "[profile] round " << round << " candidate_counts"
         << " generated=" << roundCandidatesGenerated
         << " scanned=" << scannedCandNum
         << " validated=" << validatedCandNum
         << " skipped_zero_est=" << skippedZeroEstCandNum
         << endl;

    // return error
    return err;
}


unsigned ALSMan::NewSeed() {
    boost::uniform_int <> unDistr(numeric_limits <int>::min(), numeric_limits <int>::max());
    unsigned _seed = static_cast <unsigned> (unDistr(randGen));
    cout << "new seed = " << _seed << endl;
    return _seed;
}


void ALSMan::ApplyLacPro(NetMan & net, std::shared_ptr <LAC> pLac, double backErr) {
    if (lacType == LAC_TYPE::RESUB) {
        assert(net.GetNetType() == NET_TYPE::SOP);
        net.GetLev();
        auto pSpecLac = dynamic_pointer_cast <ResubLAC> (pLac);
        auto targId = pSpecLac->GetTargId();
        auto faninIds = pSpecLac->GetDivIds();
        auto sop = pSpecLac->GetSop();
        lastAppliedLac = pSpecLac->GetReprStr();
        cout << "replace " << net.GetObj(targId);
        cout << "(l=" << net.GetObjLev(targId) << ") with old fanins (";
        for (ll i = 0; i < net.GetFaninNum(targId); ++i)
            cout << net.GetFaninId(targId, i) << "(l=" << net.GetObjLev(net.GetFanin(targId, i)) << "),";
        cout << ")";
        cout << " by ";
        cout << "(";
        for (const auto & faninId: faninIds)
            cout << faninId << "(l=" << net.GetObjLev(faninId) << "),";
        cout << ")";
        BigFlt errScale = BigFlt(transitionGMMPolicyFrames)
            * BigFlt(outputNum);
        if (metrType == METR_TYPE::MAPE)
            errScale *= MAPE_ERR_SCALE;
        cout << " with estimated error " << double(BigFlt(pSpecLac->GetErrPro()) / errScale) << endl;
        cout << " using function:" << endl;
        cout << sop;

        auto consts = net.CreateConst();
        if (sop == " 0\n") {
            net.Replace(targId, consts.first);
        }
        else if (sop == " 1\n") {
            net.Replace(targId, consts.second);
        }
        else if (sop == "1 1\n") {
            assert(faninIds.size() == 1);
            net.Replace(targId, faninIds[0]);
        }
        else if (sop == "0 1\n") {
            assert(faninIds.size() == 1);
            net.ReplaceByComplementedObj(targId, faninIds[0]);
        }
        else {
            auto newNodeId = net.CreateNodeAIG(faninIds, sop);
            if (newNodeId == targId) {
                cout << "skip self-replacement LAC after node creation" << endl;
                return;
            }
            net.Replace(targId, newNodeId);
        }
    }
    else
        assert(0);
}


void ALSMan::ExactSimpl(NetMan & net) {
    cout << endl << "******************** simplify ********************" << endl;
    cout << "before simplification: ";
    net.PrintStat();

    cout << "after simplification: ";
    if (net.GetNetType() == NET_TYPE::GATE) {
        net.SynthAndMap(maxDelay, false);
    }
    else {
        net.Comm("st; logic; sop; ps;");
        // net.SynthWithResyn2Comm();
    }
    net.MergeConst();
    cout << "**********************************************************" << endl << endl;
}


double ALSMan::Eval(NetMan& net, double err, bool useYosys, bool IsInitialCircuit) {
    auto profEvalBeg = ProfClock::now();
    double profWriteBlif = 0.0;
    double profWriteAig = 0.0;
    double profMapAbc = 0.0;
    double profMapYosys = 0.0;
    double profMetricsRecord = 0.0;
    assert(net.GetNetType() == NET_TYPE::SOP);

    // measure and output SOP
    ostringstream oss("");
    static size_t evalSerial = 0;
    const size_t currEvalSerial = evalSerial++;
    oss << setprecision(17) << outpPath << round << "_" << net.GetNet()->pName << "_" << metrType << "_" << err
        << "_eval_" << currEvalSerial << "_size_" << net.GetArea() << "_depth_" << net.GetDelay();
    const string sopNetlistPath = oss.str() + ".blif";
    const string aigNetlistPath = oss.str() + "_aig.blif";
    auto profWriteBlifBeg = ProfClock::now();
    net.WriteBlif(sopNetlistPath);
    profWriteBlif = ProfSeconds(profWriteBlifBeg);
    {
        auto profWriteAigBeg = ProfClock::now();
        auto aigNet = net;
        aigNet.Comm("st; logic; sop;");
        aigNet.WriteBlif(aigNetlistPath);
        profWriteAig = ProfSeconds(profWriteAigBeg);
    }

    // synthesize and technology mapping with ABC
    static double recArea = numeric_limits<double>::max();
    static double recDelay = numeric_limits<double>::max();
    double tempNetArea = DBL_MAX, tempNetDelay = DBL_MAX;
    double candidateArea = DBL_MAX, candidateDelay = DBL_MAX;
    {
        auto profMapAbcBeg = ProfClock::now();
        auto tempNet = net;
        if (IsInitialCircuit)
            tempNet.Comm("st; dch; amap;");
            // tempNet.Comm("st; map;"); // for EPFL only, because the initial circuit is delay optimized
        else
            tempNet.Compile(maxDelay);
            // tempNet.CompileNew(maxDelay); // for EPFL only, because the initial circuit is delay optimized
        // update best
        tempNetArea = tempNet.GetArea();
        tempNetDelay = tempNet.GetDelay();
        candidateArea = tempNetArea;
        candidateDelay = tempNetDelay;
        if (candidateValidationPolicy == "complete_size_gain")
            tempNet.Comm("write_blif " + aigNetlistPath + ".mapped.blif");
        if (DoubleLess(tempNetArea, recArea) ||
            (DoubleEqual(tempNetArea, recArea) && DoubleLess(tempNetDelay, recDelay)) ) {
            recArea = tempNetArea;
            recDelay = tempNetDelay;
        }
        profMapAbc = ProfSeconds(profMapAbcBeg);
    }

    // synthesize and technology mapping with Yosys
    if (!IsInitialCircuit && useYosys) { // (optional,) for obtaining better results
        auto profMapYosysBeg = ProfClock::now();
        auto tempNet= net;
        if (tempNet.CompileWithYosys(standCellPath)) {
            tempNetArea = tempNet.GetArea();
            tempNetDelay = tempNet.GetDelay();
            if (DoubleLess(tempNetArea, candidateArea) ||
                (DoubleEqual(tempNetArea, candidateArea) && DoubleLess(tempNetDelay, candidateDelay))) {
                candidateArea = tempNetArea;
                candidateDelay = tempNetDelay;
                if (candidateValidationPolicy == "complete_size_gain")
                    tempNet.Comm("write_blif " + aigNetlistPath + ".mapped.blif");
            }
            if (DoubleLess(tempNetDelay, maxDelay) || DoubleGreat(recDelay, maxDelay)) {
                if (DoubleLess(tempNetArea, recArea) ||
                (DoubleEqual(tempNetArea, recArea) && DoubleLess(tempNetDelay, recDelay)) ) {
                    recArea = tempNetArea;
                    recDelay = tempNetDelay;
                }
            }
        }
        profMapYosys = ProfSeconds(profMapYosysBeg);
    }

    // output and return
    cout << "current best: area = " << recArea << ", delay = " << recDelay << endl;
    auto profMetricsBeg = ProfClock::now();
    auto combinedMetrics = CalcCombinedErrorMetrics(accNet, net, isSign, seed, nFrame, metrType, distrType, outputNum, patternFilePath, outputWidths);
    vector<double> errorVector = combinedMetrics.mapeVector;
    ErrorMetrics multiMetrics = combinedMetrics.multiMetrics;
    vector<double> featureVector = SelectDesignFeatureVector(err, errorVector, multiMetrics);
    if (IsDesignErrorFeasible(featureVector)) {
        RecordPareto(err, errorVector, multiMetrics, candidateArea, candidateDelay, aigNetlistPath);
    }
    profMetricsRecord = ProfSeconds(profMetricsBeg);
    const double profEvalTotal = ProfSeconds(profEvalBeg);
    cout << "[profile] round " << round << " eval_seconds"
         << " write_blif=" << profWriteBlif
         << " write_aig=" << profWriteAig
         << " map_abc=" << profMapAbc
         << " map_yosys=" << profMapYosys
         << " metrics_record=" << profMetricsRecord
         << " total=" << profEvalTotal
         << " initial=" << (IsInitialCircuit? 1: 0)
         << " use_yosys=" << (useYosys? 1: 0)
         << endl;
    return recDelay;
}


static string CsvQuote(const string& value) {
    string escaped = value;
    size_t pos = 0;
    while ((pos = escaped.find('"', pos)) != string::npos) {
        escaped.insert(pos, 1, '"');
        pos += 2;
    }
    return "\"" + escaped + "\"";
}


void ALSMan::RecordPareto(double err, const vector<double>& errorVector, const ErrorMetrics& multiMetrics, double area, double delay, const string& netlistPath) {
    string lac = lastAppliedLac;
    replace(lac.begin(), lac.end(), '\n', ';');
    auto featureVector = SelectDesignFeatureVector(err, errorVector, multiMetrics);
    const double recordedDesignError = signedLACScorer != nullptr
        ? signedLACScorerConfig.qCurrent : PredictDesignError(featureVector);
    const double recordedDesignBound = signedLACScorer != nullptr
        ? signedLACScorerConfig.qMax : designErrUppBound;
    ParetoRecord record{round, boundPhase, err, errorVector, multiMetrics,
                        recordedDesignError, recordedDesignBound,
                        area, delay, netlistPath, lac};
    record.candidatesGenerated = roundCandidatesGenerated;
    record.candidatesScanned = roundCandidatesScanned;
    record.exactValidations = roundExactValidations;
    record.workUnits = roundCandidatesGenerated + roundExactValidations;
    record.cumulativeCandidatesGenerated = cumulativeCandidatesGenerated;
    record.cumulativeCandidatesScanned = cumulativeCandidatesScanned;
    record.cumulativeExactValidations = cumulativeExactValidations;
    record.cumulativeWorkUnits = cumulativeWorkUnits;
    record.roundRuntimeSec = round == 0? 0.0: ProfSeconds(roundStartTime);
    record.cumulativeRuntimeSec = ProfSeconds(runStartTime);
    record.stopReason = StopReasonString(lastStopReason);
    trajectoryRecords.emplace_back(record);
    paretoRecords = BuildLocalPareto(trajectoryRecords);
    WriteParetoCsv();
    WriteTrajectoryCsv();
}


vector<ALSMan::ParetoRecord> ALSMan::BuildLocalPareto(const vector<ParetoRecord>& records) const {
    vector<ParetoRecord> front;
    for (const auto& candidate: records) {
        // A failed/no-LAC round is retained in trajectoryRecords as an
        // unchanged terminal state for work/runtime accounting.  It must not
        // become a duplicate Pareto operating point when a later relaxed
        // bound phase continues from the same circuit.
        if (candidate.round != 0 && !candidate.stopReason.empty() && candidate.lac.empty())
            continue;
        bool dominated = false;
        if (candidate.round != 0) {
            for (const auto& other: records) {
                bool noWorse = !DoubleGreat(other.kernelErr, candidate.kernelErr) &&
                               !DoubleGreat(other.area, candidate.area);
                bool strictlyBetter = DoubleLess(other.kernelErr, candidate.kernelErr) ||
                                      DoubleLess(other.area, candidate.area);
                if (noWorse && strictlyBetter) {
                    dominated = true;
                    break;
                }
            }
        }
        if (!dominated)
            front.emplace_back(candidate);
    }
    sort(front.begin(), front.end(), [](const ParetoRecord& lhs, const ParetoRecord& rhs) {
        if (!DoubleEqual(lhs.kernelErr, rhs.kernelErr))
            return lhs.kernelErr < rhs.kernelErr;
        if (!DoubleEqual(lhs.area, rhs.area))
            return lhs.area < rhs.area;
        return lhs.delay < rhs.delay;
    });
    return front;
}


void ALSMan::WriteRecordsCsv(const string& path, const vector<ParetoRecord>& records) const {
    ofstream csv(path);
    assert(csv.good());
    csv << "round,kernel_error,error_vector,multi_error_vector,kernel_MAPE,kernel_NMED,kernel_NMSE,kernel_ER,kernel_ME,design_error,design_error_bound,area,delay,aig_netlist,lac,phase,candidates_generated,candidates_scanned,exact_validations,work_units,cumulative_candidates_generated,cumulative_candidates_scanned,cumulative_exact_validations,cumulative_work_units,round_runtime_sec,cumulative_runtime_sec,stop_reason\n";
    csv << setprecision(17);
    for (const auto& record: records) {
        ostringstream errorVector;
        errorVector << setprecision(17) << "[";
        for (size_t i = 0; i < record.errorVector.size(); ++i) {
            if (i)
                errorVector << ",";
            errorVector << record.errorVector[i];
        }
        errorVector << "]";
        ostringstream multiErrorVector;
        multiErrorVector << setprecision(17) << "["
                         << record.multiMetrics.nmed << ","
                         << record.multiMetrics.nmse << ","
                         << record.multiMetrics.er << ","
                         << record.multiMetrics.me << "]";
        const double kernelMape = record.errorVector.empty()? 0.0:
            accumulate(record.errorVector.begin(), record.errorVector.end(), 0.0)
                / static_cast<double>(record.errorVector.size());
        csv << record.round << ","
            << record.kernelErr << ","
            << CsvQuote(errorVector.str()) << ","
            << CsvQuote(multiErrorVector.str()) << ","
            << kernelMape << ","
            << record.multiMetrics.nmed << ","
            << record.multiMetrics.nmse << ","
            << record.multiMetrics.er << ","
            << record.multiMetrics.me << ","
            << record.designErr << ","
            << record.designErrBound << ","
            << record.area << ","
            << record.delay << ","
            << CsvQuote(record.netlistPath) << ","
            << CsvQuote(record.lac) << ","
            << record.phase << ","
            << record.candidatesGenerated << ","
            << record.candidatesScanned << ","
            << record.exactValidations << ","
            << record.workUnits << ","
            << record.cumulativeCandidatesGenerated << ","
            << record.cumulativeCandidatesScanned << ","
            << record.cumulativeExactValidations << ","
            << record.cumulativeWorkUnits << ","
            << record.roundRuntimeSec << ","
            << record.cumulativeRuntimeSec << ","
            << CsvQuote(record.stopReason) << "\n";
    }
}


void ALSMan::WriteParetoCsv() const {
    WriteRecordsCsv(outpPath + "pareto.csv", paretoRecords);
}


void ALSMan::WriteTrajectoryCsv() const {
    WriteRecordsCsv(outpPath + "trajectory.csv", trajectoryRecords);
    WriteTrajectoryJsonl();
}


void ALSMan::WriteParetoPhaseCsv(int phase) const {
    vector<ParetoRecord> records;
    for (const auto& record: trajectoryRecords) {
        if (record.phase <= phase)
            records.emplace_back(record);
    }
    WriteRecordsCsv(outpPath + "pareto_phase_" + to_string(phase) + ".csv", BuildLocalPareto(records));
}


void ALSMan::WriteBoundPhasesCsv() const {
    ofstream csv(outpPath + "bound_phases.csv");
    assert(csv.good());
    csv << "phase,bound,start_round,end_round,stop_reason,cumulative_runtime_sec\n";
    csv << setprecision(17);
    for (const auto& record: boundPhaseRecords) {
        csv << record.phase << ","
            << record.bound << ","
            << record.startRound << ","
            << record.endRound << ","
            << record.stopReason << ","
            << record.cumulativeRuntimeSec << "\n";
    }
}


static void AppendNet(Abc_Ntk_t* pResNtk, Abc_Ntk_t* pNtkAcc, Abc_Ntk_t* pNtkApp, Abc_Ntk_t* pNtkMit, int netMark, RETURN_VAR IntVect& miterId2AppId, RETURN_VAR IntVect& appId2MiterId) {
    // init
    // netMark = 0, accNet; netMark = 1, appNet; netMark = 2, mitNet;
    Abc_Ntk_t* pNtkDeal = nullptr;
    if (netMark == 0)
        pNtkDeal = pNtkAcc;
    else if (netMark == 1)
        pNtkDeal = pNtkApp;
    else if (netMark == 2)
        pNtkDeal = pNtkMit;
    else
        assert(0);
    assert(!Abc_NtkIsStrash(pNtkDeal));
    Abc_Obj_t* pObj = nullptr;
    Abc_Obj_t* pFanin = nullptr;
    int i = 0, k = 0;
    Abc_NtkCleanCopy(pNtkDeal);

    // deal with PIs
    if (netMark == 0) {
        Abc_NtkForEachPi(pNtkDeal, pObj, i)
            abc::Abc_NtkDupObj(pResNtk, pObj, 1);
    }
    else if (netMark == 1) {
        Abc_NtkForEachPi(pNtkDeal, pObj, i) {
            auto pPi = abc::Abc_NtkPi(pResNtk, i);
            pObj->pCopy = pPi;
            miterId2AppId[pPi->Id] = pObj->Id;
            appId2MiterId[pObj->Id] = pPi->Id;
        }
    }
    else if (netMark == 2) {
        Abc_NtkForEachPo(pNtkAcc, pObj, i)
            abc::Abc_NtkPi(pNtkDeal, i)->pCopy = abc::Abc_ObjChild0Copy(pObj);
        int nWidth = abc::Abc_NtkPoNum(pNtkAcc);
        Abc_NtkForEachPo(pNtkApp, pObj, i)
            abc::Abc_NtkPi(pNtkDeal, i + nWidth)->pCopy = abc::Abc_ObjChild0Copy(pObj);
    }
    else
        assert(0);
    // duplicate nodes
    Abc_NtkForEachNode(pNtkDeal, pObj, i) {
        if (pObj->pCopy == nullptr) {
            auto pNewNode = abc::Abc_NtkDupObj(pResNtk, pObj, 0);
            if (netMark == 1) {
                miterId2AppId[pNewNode->Id] = pObj->Id;
                appId2MiterId[pObj->Id] = pNewNode->Id;
            }
            if (netMark == 0)
                RenameAbcObj(pObj->pCopy, string(Abc_ObjName(pObj)) + "_acc");
            else if (netMark == 1)
                RenameAbcObj(pObj->pCopy, string(Abc_ObjName(pObj)) + "_app");
            else if (netMark == 2)
                RenameAbcObj(pObj->pCopy, string(Abc_ObjName(pObj)) + "_dev");
            else
                assert(0);
        }
    }
    // reconnect all nodes
    Abc_NtkForEachNode(pNtkDeal, pObj, i) {
        Abc_ObjForEachFanin(pObj, pFanin, k)
            Abc_ObjAddFanin(pObj->pCopy, pFanin->pCopy);
    }
    // deal with POs
    if (netMark == 2) {
        Abc_NtkForEachPo(pNtkDeal, pObj, i)
            abc::Abc_NtkDupObj(pResNtk, pObj, 1);
        Abc_NtkForEachPo(pNtkDeal, pObj, i)
            Abc_ObjAddFanin(pObj->pCopy, abc::Abc_ObjChild0Copy(pObj));
    }
}


NetManPtr ALSMan::BuildErrorRateMiter(NetMan& accNet, NetMan& appNet, RETURN_VAR IntVect& miterId2AppId, RETURN_VAR IntVect& appId2MiterId) {
    // check
    assert(IsPIOSame(accNet, appNet));
    assert(accNet.GetNetType() == NET_TYPE::SOP && appNet.GetNetType() == NET_TYPE::SOP);

    // start empty network
    auto pResNet = make_shared<NetMan>();
    pResNet->StartSopNet();

    // start XOR-OR unit
    int nBit = accNet.GetPoNum();
    auto pDevNet = BuildXorOrCircuit(nBit);
    // pXorOrNet->Sweep();

    // init miterId2AppId and appId2MiterId
    miterId2AppId.resize(accNet.GetIdMaxPlus1() + appNet.GetIdMaxPlus1() + pDevNet->GetIdMaxPlus1(), -1);
    appId2MiterId.resize(appNet.GetIdMaxPlus1(), -1);

    // copy accNet
    AppendNet(pResNet->GetNet(), accNet.GetNet(), appNet.GetNet(), pDevNet->GetNet(), 0, miterId2AppId, appId2MiterId);
    AppendNet(pResNet->GetNet(), accNet.GetNet(), appNet.GetNet(), pDevNet->GetNet(), 1, miterId2AppId, appId2MiterId);
    AppendNet(pResNet->GetNet(), accNet.GetNet(), appNet.GetNet(), pDevNet->GetNet(), 2, miterId2AppId, appId2MiterId);

    // return
    return pResNet;
}


NetManPtr ALSMan::BuildXorOrCircuit(int nBits) {
    // check
    assert(nBits > 0);

    // start empty network
    auto pResNet = make_shared<NetMan>();
    pResNet->StartSopNet();
    auto pAbcNet = pResNet->GetNet();

    // create PIs
    // cout << "create PIs" << endl;
    AbcObjVect piA(nBits, nullptr), piB(nBits, nullptr);
    for (int i = 0; i < nBits; ++i) {
        // cout << "create PIA " << i << endl;
        piA[i] = abc::Abc_NtkCreatePi(pAbcNet);
    }
    for (int i = 0; i < nBits; ++i) {
        // cout << "create PIB " << i << endl;
        piB[i] = abc::Abc_NtkCreatePi(pAbcNet);
    }

    // create XOR gates
    // cout << "create XOR gates" << endl;
    AbcObjVect xorGates(nBits, nullptr);
    for (int i = 0; i < nBits; ++i)
        xorGates[i] = pResNet->CreateNode(AbcObjVect{piA[i], piB[i]}, "01 1\n10 1\n");
    
    // create OR gate
    // cout << "create OR gate" << endl;
    string orFunc = "";
    for (int i = 0; i < nBits; ++i)
        orFunc += "0";
    orFunc += " 0\n";
    auto pOr = pResNet->CreateNode(xorGates, orFunc);

    // create POs
    // for (int i = 0; i < nBits; ++i) {
    //     auto pPo = abc::Abc_NtkCreatePo(pAbcNet);
    //     abc::Abc_ObjAddFanin(pPo, diff[i]);
    // }
    auto pPo = abc::Abc_NtkCreatePo(pAbcNet);
    abc::Abc_ObjAddFanin(pPo, pOr);

    // return
    return pResNet;
}


NetManPtr ALSMan::BuildErrorDistanceMiter(NetMan& accNet, NetMan& appNet, RETURN_VAR IntVect& miterId2AppId, RETURN_VAR IntVect& appId2MiterId) {
    // check
    assert(IsPIOSame(accNet, appNet));
    assert(accNet.GetNetType() == NET_TYPE::SOP && appNet.GetNetType() == NET_TYPE::SOP);

    // start empty network
    auto pResNet = make_shared<NetMan>();
    pResNet->StartSopNet();

    // start absolute difference unit 
    int nBit = accNet.GetPoNum();
    auto pAbsDiffNet = BuildAbsoluteDifferenceCircuit(nBit);
    pAbsDiffNet->Sweep();

    // init miterId2AppId and appId2MiterId
    miterId2AppId.resize(accNet.GetIdMaxPlus1() + appNet.GetIdMaxPlus1() + pAbsDiffNet->GetIdMaxPlus1(), -1);
    appId2MiterId.resize(appNet.GetIdMaxPlus1(), -1);

    // copy accNet
    AppendNet(pResNet->GetNet(), accNet.GetNet(), appNet.GetNet(), pAbsDiffNet->GetNet(), 0, miterId2AppId, appId2MiterId);
    AppendNet(pResNet->GetNet(), accNet.GetNet(), appNet.GetNet(), pAbsDiffNet->GetNet(), 1, miterId2AppId, appId2MiterId);
    AppendNet(pResNet->GetNet(), accNet.GetNet(), appNet.GetNet(), pAbsDiffNet->GetNet(), 2, miterId2AppId, appId2MiterId);

    // return
    return pResNet;
}


NetManPtr ALSMan::BuildAbsoluteDifferenceCircuit(int nBits) {
    // check
    assert(nBits > 0);

    // start empty network
    auto pResNet = make_shared<NetMan>();
    pResNet->StartSopNet();
    auto pAbcNet = pResNet->GetNet();

    // create constant nodes
    // cout << "create constant nodes" << endl;
    auto pConst0 = abc::Abc_NtkCreateNodeConst0(pAbcNet);
    auto pConst1 = abc::Abc_NtkCreateNodeConst1(pAbcNet);

    // create PIs
    // cout << "create PIs" << endl;
    AbcObjVect piA(nBits, nullptr), piB(nBits, nullptr);
    for (int i = 0; i < nBits; ++i) {
        // cout << "create PIA " << i << endl;
        piA[i] = abc::Abc_NtkCreatePi(pAbcNet);
    }
    for (int i = 0; i < nBits; ++i) {
        // cout << "create PIB " << i << endl;
        piB[i] = abc::Abc_NtkCreatePi(pAbcNet);
    }

    // extend A and B by a sign bit
    // cout << "extend A and B by a sign bit" << endl;
    AbcObjVect extA(nBits + 1, nullptr), extB(nBits + 1, nullptr);
    for (int i = 0; i < nBits; ++i)
        extA[i] = piA[i];
    extA[nBits] = pConst0;
    for (int i = 0; i < nBits; ++i)
        extB[i] = piB[i];
    extB[nBits] = pConst0;

    // get ~B
    // cout << "get ~B" << endl;
    AbcObjVect negB(nBits + 1, nullptr);
    for (int i = 0; i <= nBits; ++i)
        negB[i] = abc::Abc_NtkCreateNodeInv(pAbcNet, extB[i]);

    // sum = A + ~B + 1
    // ripple carry adder
    AbcObjVect sum(nBits + 1, nullptr), carry(nBits + 1, nullptr);
    // set C[0] = 1
    carry[0] = pConst1;
    for (int i = 0; i <= nBits; ++i) {
        // sum[i] = A[i] ^ B[i] ^ C[i]
        auto aXorB = pResNet->CreateNode(AbcObjVect{extA[i], negB[i]}, "01 1\n10 1\n");
        sum[i] = pResNet->CreateNode(AbcObjVect{aXorB, carry[i]}, "01 1\n10 1\n");
        // discard the carry out of the last bit
        if (i == nBits)
            break;
        // C[i + 1] = (A[i] & B[i]) | (C[i] & (A[i] ^ B[i]))
        auto aAndB = pResNet->CreateNode(AbcObjVect{extA[i], negB[i]}, "11 1\n");
        auto aXorBAndC = pResNet->CreateNode(AbcObjVect{aXorB, carry[i]}, "11 1\n");
        carry[i + 1] = pResNet->CreateNode(AbcObjVect{aAndB, aXorBAndC}, "00 0\n");
    }

    // get comp[nBits - 1: 0] = (~sum[nBits - 1: 0] + 1)
    AbcObjVect comp(nBits, nullptr), carry2(nBits, nullptr);
    carry2[0] = pConst1;
    for (int i = 0; i < nBits; ++i) {
        auto pNegSum = abc::Abc_NtkCreateNodeInv(pAbcNet, sum[i]);
        comp[i] = pResNet->CreateNode(AbcObjVect{pNegSum, carry2[i]}, "01 1\n10 1\n");
        if (i == nBits - 1)
            break;
        carry2[i + 1] = pResNet->CreateNode(AbcObjVect{pNegSum, carry2[i]}, "11 1\n");
    }

    // diff[nBits - 1: 0] = sum[nBits]? comp[nBits - 1: 0]: sum[nBits - 1: 0]
    AbcObjVect diff(nBits, nullptr);
    for (int i = 0; i < nBits; ++i)
        diff[i] = abc::Abc_NtkCreateNodeMux(pAbcNet, sum[nBits], comp[i], sum[i]);

    // create POs
    for (int i = 0; i < nBits; ++i) {
        auto pPo = abc::Abc_NtkCreatePo(pAbcNet);
        abc::Abc_ObjAddFanin(pPo, diff[i]);
    }

    // return
    return pResNet;
}


NetManPtr ALSMan::BuildMiterWithYosys(NetMan& accNet, NetMan& appNet, RETURN_VAR IntVect& miterId2AppId, RETURN_VAR IntVect& appId2MiterId) {
    // check
    assert(IsPIOSame(accNet, appNet));
    assert(accNet.GetNetType() == NET_TYPE::SOP && appNet.GetNetType() == NET_TYPE::SOP);

    // start empty network
    auto pResNet = make_shared<NetMan>();
    pResNet->StartSopNet();

    // start absolute difference unit 
    int nBit = accNet.GetPoNum();
    auto pDevNet = BuildDeviationCircuit(nBit);

    // init miterId2AppId and appId2MiterId
    miterId2AppId.resize(accNet.GetIdMaxPlus1() + appNet.GetIdMaxPlus1() + pDevNet->GetIdMaxPlus1(), -1);
    appId2MiterId.resize(appNet.GetIdMaxPlus1(), -1);

    // copy accNet
    AppendNet(pResNet->GetNet(), accNet.GetNet(), appNet.GetNet(), pDevNet->GetNet(), 0, miterId2AppId, appId2MiterId);
    AppendNet(pResNet->GetNet(), accNet.GetNet(), appNet.GetNet(), pDevNet->GetNet(), 1, miterId2AppId, appId2MiterId);
    AppendNet(pResNet->GetNet(), accNet.GetNet(), appNet.GetNet(), pDevNet->GetNet(), 2, miterId2AppId, appId2MiterId);

    // return
    return pResNet;
}


static void CreateBehLevDeviation(METR_TYPE metrType, bool isSign, int nBits, const string& fileName) {
    FILE* f = fopen(fileName.c_str(), "w");
    fprintf(f, "module deviation(a, b, f);\n");
    fprintf(f, "parameter width = %d;\n", nBits);
    if (isSign) {
        fprintf(f, "input signed [width - 1: 0] a;\n");
        fprintf(f, "input signed [width - 1: 0] b;\n");
    }
    else {
        fprintf(f, "input [width - 1: 0] a;\n");
        fprintf(f, "input [width - 1: 0] b;\n");
    }
    if (metrType == METR_TYPE::MSE)
        fprintf(f, "output [width * 2 - 1: 0] f;\n");
    // else if (metrType == METR_TYPE::MHD) {
    //     int poWidth = (int)(log2(nBits)) + 1;
    //     fprintf(f, "output [%d: 0] f;\n", poWidth - 1);
    // }
    else
        assert(0);
    
    if (metrType == METR_TYPE::MSE) {
        fprintf(f, "wire [width - 1: 0] diff;\n");
        fprintf(f, "assign diff = (a > b)? (a - b): (b - a);\n");
        fprintf(f, "assign f = diff * diff;\n");
    }
    // else if (metrType == METR_TYPE::MHD) {
    //     fprintf(f, "wire [width - 1: 0] diff;\n");
    //     fprintf(f, "assign diff = a ^ b;\n");
    //     fprintf(f, "assign f = 1'b0");
    //     for (int i = 0; i < nBits; ++i)
    //         fprintf(f, " + diff[%d]", i);
    //     fprintf(f, ";\n");
    // }
    else
        assert(0);
    fprintf(f, "endmodule\n");
    fclose(f);
}


NetManPtr ALSMan::BuildDeviationCircuit(int nBits) {
    // if the miter has been built or loaded, return the miter
    static std::shared_ptr<NetMan> pResNet = nullptr;
    if (pResNet != nullptr)
        return pResNet;

    // check & create folder
    assert(nBits > 0);
    const string folder = "input/miter/";
    CreatePath(folder);

    // get the name of the miter file
    ostringstream fileNameBase;
    if (metrType == METR_TYPE::MSE)
        fileNameBase << folder << (isSign? "signed_": "unsigned_") << "mse_width_" << nBits;
    else
        assert(0);
    auto behName = fileNameBase.str() + "_beh.v";
    auto finalName = fileNameBase.str() + "_sop.blif";

    // if miter file exists, load the miter file
    if (IsPathExist(finalName)) {
        AbcMan abcMan;
        abcMan.ReadNet(finalName);
        pResNet = make_shared<NetMan>(abcMan.GetNet(), true);
    }
    else { // if miter file doesn't exist, synthesize a new miter file
        CreateBehLevDeviation(metrType, isSign, nBits, behName);
        ostringstream comm;
        comm << "yosys -q -p \"read_verilog " << behName << "; synth; abc -script +source,abc.rc;st;compress2rs;ps; write_blif " << finalName << "\"";
        ExecSystComm(comm.str());
        AbcMan abcMan;
        abcMan.ReadNet(finalName);
        pResNet = make_shared<NetMan>(abcMan.GetNet(), true); 
    }

    // return
    return pResNet;
}



double ALSMan::ComputeError(Simulator& accSmlt, NetMan& net) {
    Simulator appSmlt(net, seed, nFrame);
    appSmlt.InpUnifFast();
    appSmlt.Sim();
    double err = -1;
    if (metrType == METR_TYPE::MED)
        err = accSmlt.GetMeanErrDist(appSmlt, isSign);
    else
        assert(0);
    return err;
}


double ALSMan::ComputeError(Simulator& accSmlt, Simulator& appSmlt) {
    double err = -1;
    if (metrType == METR_TYPE::MED)
        err = accSmlt.GetMeanErrDist(appSmlt, isSign);
    else
        assert(0);
    return err;
}
