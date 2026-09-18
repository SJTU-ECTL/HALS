#include "cmdline.hpp"
#include "header.h"
#include "my_abc.h"
#include "als.h"

#include <sstream>


using namespace abc;
using namespace boost;
using namespace cmdline;
using namespace std;


static vector<double> ParseDoubleVector(const string& text) {
    vector<double> values;
    string token;
    stringstream ss(text);
    while (getline(ss, token, ',')) {
        if (!token.empty())
            values.emplace_back(stod(token));
    }
    return values;
}


static vector<int> ParseIntVector(const string& text) {
    vector<int> values;
    string token;
    stringstream ss(text);
    while (getline(ss, token, ',')) {
        if (!token.empty())
            values.emplace_back(stoi(token));
    }
    return values;
}

static vector<string> ParseStringVector(const string& text) {
    vector<string> values;
    string token;
    stringstream ss(text);
    while (getline(ss, token, ',')) {
        if (!token.empty())
            values.emplace_back(token);
    }
    return values;
}


parser CommPars(int argc, char * argv[]) {
    parser option;
    option.add<string>("accCirc", '\0', "path to accurate circuit", true);
    option.add<string>("standCell", '\0', "path to standard cell library", false, "./nangate_45nm_typ.lib");
    option.add<string>("outpPath", '\0', "path to approximate circuits", false, "tmp");
    option.add<string>("metrType", '\0', "error metric type: MAE/MED, NMED, MSE, NMSE, MAPE", false, "NMED");
    option.add<string>("distrType", '\0', "error distribution type: UNIF, ENUM, SELF", false, "UNIF");
    option.add<string>("patternFile", '\0', "path to self-defined simulation input patterns", false, "");
    option.add<unsigned>("seed", '\0', "seed for randomness", false, 0);
    option.add<double>("errUppBound", '\0', "error upper bound", false, 0.15);
    option.add<string>("errUppBounds", '\0', "comma-separated tight-to-loose error upper bounds", false, "");
    option.add<double>("designMetricWeight", '\0', "weight of this kernel error in the design-level metric", false, 1.0);
    option.add<double>("designErrUppBound", '\0', "design-model error upper bound; negative derives from errUppBound", false, -1.0);
    option.add<string>("designErrUppBounds", '\0', "comma-separated tight-to-loose design-model error upper bounds", false, "");
    option.add<string>("designModelPath", '\0', "path to design-level error model surrogate JSON", false, "");
    option.add<string>("featureIndices", '\0', "comma-separated feature indices controlled by this kernel", false, "");
    option.add<string>("featureMetrics", '\0', "comma-separated feature metric names controlled by this kernel (ME is signed mean error; legacy BIAS is accepted as an alias)", false, "");
    option.add<string>("initialFeatureVector", '\0', "comma-separated initial design error feature vector", false, "");
    option.add<double>("modelSafetyMargin", '\0', "safety margin added to model-predicted design error", false, 0.0);
    option.add<string>("earlyExitPolicy", '\0', "VECBEE early-exit policy: legacy_scalar or disabled", false, "disabled");
    option.add("scalarKernelFeature", '\0', "use scalar kernel error for all feature indices controlled by this kernel");
    option.add<int>("nFrame", '\0', "#simulation samples; UNIF fast simulation aligns to 64-frame words, SELF accepts any positive count", false, 102400);
    option.add<int>("nFrame4ResubGen", '\0', "#patterns for AppResub Generation", false, 64);
    option.add<int>("maxCandResub", '\0', "max #candidate AppResubs", false, 100000);
    option.add<int>("maxExactCandValidate", '\0', "max #exact simulated candidate validations per ALS round", false, 64);
    option.add<string>("candidateValidationPolicy", '\0', "exact candidate validation policy: size_gain, stratified, gradient, gradient_stratified, or mlp_guarded", false, "size_gain");
    option.add<string>("candidateFeatureSource", '\0', "candidate full-feature source for guard validation: simulation, vecbee, or vecbee_shadow", false, "simulation");
    option.add<string>("candidateAuditDir", '\0', "optional directory for write-only candidate BLIF/JSONL audit artifacts", false, "");
    option.add<int>("candidateAuditLimit", '\0', "maximum audited exact candidates per ALS round", false, 32);
    option.add<string>("publicationCandidateDir", '\0', "optional hash-ordered pre-estimation candidate BLIF pool directory", false, "");
    option.add<int>("publicationCandidateLimit", '\0', "maximum hash-ordered publication candidates exported per round", false, 512);
    option.add<string>("transitionGMMPrototypes", '\0', "frozen transition-GMM prototype text file", false, "");
    option.add<string>("transitionGMMOutput", '\0', "optional VECBEE transition-GMM JSONL output", false, "");
    option.add<string>("transitionGMMGraphHash", '\0', "typed-IR graph hash stored in transition-GMM records", false, "");
    option.add<string>("transitionGMMBaseStateHash", '\0', "base ALS-state hash stored in transition-GMM records", false, "");
    option.add<string>("transitionGMMPatternHash", '\0', "pattern-set hash stored in transition-GMM records", false, "");
    option.add<string>("transitionGMMTargetRegion", '\0', "target sub-graph/region identifier stored in transition-GMM records", false, "");
    option.add<string>("transitionGMMBoundaryNodes", '\0', "comma-separated typed-IR boundary node ids, one per output value", false, "");
    option.add<string>("transitionGMMPolicyPatternFile", '\0', "SELF-distribution policy patterns, disjoint from truth patternFile", false, "");
    option.add<int>("transitionGMMPolicyFrames", '\0', "policy samples for VECBEE/GMM; 0 uses nFrame", false, 0);
    option.add("transitionGMMIncludeRawTrace", '\0', "include exact/current/candidate integer traces in transition-GMM JSONL (debug only)");
    option.add<string>("transitionGMMPackedTraceDir", '\0', "optional Phase-B compact VECBEE candidate-trace sidecar directory", false, "");
    option.add<string>("signedLACModel", '\0', "optional signed-DeltaQ TorchScript model", false, "");
    option.add<string>("signedLACGraphTensor", '\0', "whole-design C++ graph tensor bundle", false, "");
    option.add<string>("signedLACModelHash", '\0', "TorchScript file SHA-256", false, "");
    option.add<string>("signedLACGraphTensorHash", '\0', "graph tensor file SHA-256", false, "");
    option.add<string>("signedLACPrototypeHash", '\0', "frozen prototype SHA-256", false, "");
    option.add<string>("signedLACMetric", '\0', "model metric: MAPE/NMED/NMSE/MSE", false, "");
    option.add<string>("signedLACWrapperHash", '\0', "whole-design wrapper SHA-256", false, "");
    option.add<string>("signedLACIRHash", '\0', "packed truth-IR SHA-256", false, "");
    option.add<string>("signedLACTruthPatternHash", '\0', "whole-design truth pattern SHA-256", false, "");
    option.add<string>("signedLACExecutionBackend", '\0', "exact replay backend identifier", false, "");
    option.add<double>("signedLACMAPEBound", '\0', "MAPE bound recorded for one-pass four-metric truth", false, -1.0);
    option.add<double>("signedLACNMEDBound", '\0', "NMED bound recorded for one-pass four-metric truth", false, -1.0);
    option.add<double>("signedLACNMSEBound", '\0', "NMSE bound recorded for one-pass four-metric truth", false, -1.0);
    option.add<double>("signedLACMSEBound", '\0', "MSE bound recorded for one-pass four-metric truth", false, -1.0);
    option.add<double>("signedLACInitialQ", '\0', "whole-design Q(D_0) in the checkpoint metric unit", false, -1.0);
    option.add<double>("signedLACQMax", '\0', "whole-design quality bound in the checkpoint metric unit; negative uses errUppBound", false, -1.0);
    option.add<string>("signedLACControllerDir", '\0', "round panel/decision directory for exact whole-design replay", false, "");
    option.add<int>("signedLACTopB", '\0', "number of model-ranked candidates sent to exact replay", false, 8);
    option.add<double>("signedLACDecisionTimeoutSec", '\0', "exact-controller timeout per ALS round", false, 600.0);
    option.add<int>("signedLACDecisionPollMs", '\0', "exact-controller polling interval", false, 100);
    option.add<int>("signedLACBatchSize", '\0', "TorchScript candidate chunk size", false, 256);
    option.add<int>("signedLACTorchThreads", '\0', "Torch inference threads: 1, 4, or 8", false, 1);
    option.add<int>("nThread", '\0', "number of threads", false, 16);
    option.add<int>("maxRound", '\0', "max #ALS rounds; 0 means unlimited", false, 0);
    option.add<int>("maxZeroErrorFallbackRounds", '\0', "max #0-error LAC fallback rounds after positive-error candidates are preferred", false, 1);
    option.add<int>("probeRound", '\0', "pause after this completed ALS round boundary if search can continue; 0 disables", false, 0);
    option.add<string>("probeSummaryPath", '\0', "probe summary JSON path (default: <outpPath>/probe_summary.json)", false, "");
    option.add<string>("targetDecisionPath", '\0', "target decision JSON path (default: <outpPath>/target_decision.json)", false, "");
    option.add<double>("probeTimeoutSec", '\0', "seconds to wait for target decision; 0 waits indefinitely", false, 0.0);
    option.add<int>("probePollMs", '\0', "target decision polling interval in milliseconds", false, 100);
    option.add<string>("runId", '\0', "optional run identifier used to reject mismatched target decisions", false, "");
    // option.add <int> ("maxLevelDiff", '\0', "maximum level difference, affecting the number of 2-resubs", false, 8);
    option.add("enableFastErrEst", '\0', "when this option is enabled, the program performs faster approximate error estimation;\n\t\t\t\totherwise, the program performs slower accurate error estimation");
    option.add("isSigned", '\0', "treat each output value as signed two's-complement");
    option.add<int>("outputNum", '\0', "number of output values", false, 1);
    option.add<string>("outputWidths", '\0', "comma-separated output widths for unequal-width multi-output circuits", false, "");
    option.parse_check(argc, argv);
    return option;
}


void ConfigureOptions(ALSOpt & alsOpt, string & accCirc, string & metrType, string & distrType) {
    // load standard cell
    AbcMan abcMan;
    if (alsOpt.standCellPath != "")
        abcMan.ReadStandCell(alsOpt.standCellPath);
    abcMan.ReadNet(accCirc, false);
    alsOpt.pNtk = abcMan.GetNet();
    
    // fix output path
    FixPath(alsOpt.outpPath);
    CreatePath(alsOpt.outpPath);
    for (const auto& probePath: {alsOpt.probeSummaryPath, alsOpt.targetDecisionPath}) {
        if (!probePath.empty()) {
            filesystem::path parent = filesystem::path(probePath).parent_path();
            if (!parent.empty())
                filesystem::create_directories(parent);
        }
    }
    if (alsOpt.candidateAuditLimit <= 0) {
        cerr << "ERROR: candidateAuditLimit must be positive" << endl;
        exit(1);
    }
    if (alsOpt.publicationCandidateLimit <= 0) {
        cerr << "ERROR: publicationCandidateLimit must be positive" << endl;
        exit(1);
    }
    if (!alsOpt.candidateAuditDir.empty()) {
        alsOpt.candidateAuditDir = filesystem::absolute(
            alsOpt.candidateAuditDir).lexically_normal().string();
        filesystem::create_directories(alsOpt.candidateAuditDir);
    }
    if (!alsOpt.publicationCandidateDir.empty()) {
        if (alsOpt.runId.empty()) {
            cerr << "ERROR: publication candidate export requires a non-empty runId"
                 << endl;
            exit(1);
        }
        alsOpt.publicationCandidateDir = filesystem::absolute(
            alsOpt.publicationCandidateDir).lexically_normal().string();
        filesystem::create_directories(alsOpt.publicationCandidateDir);
    }
    const bool hasTransitionPrototype =
        !alsOpt.transitionGMMPrototypePath.empty();
    const bool hasTransitionOutput = !alsOpt.transitionGMMOutputPath.empty();
    if (alsOpt.transitionGMMPolicyFrames < 0) {
        cerr << "ERROR: transitionGMMPolicyFrames must be non-negative" << endl;
        exit(1);
    }
    if (hasTransitionPrototype != hasTransitionOutput) {
        cerr << "ERROR: transitionGMMPrototypes and transitionGMMOutput "
             << "must be supplied together" << endl;
        exit(1);
    }
    if (!hasTransitionPrototype && alsOpt.transitionGMMPolicyFrames != 0) {
        cerr << "ERROR: transitionGMMPolicyFrames requires transition GMM export"
             << endl;
        exit(1);
    }
    if (!hasTransitionPrototype && !alsOpt.transitionGMMPackedTraceDir.empty()) {
        cerr << "ERROR: transitionGMMPackedTraceDir requires transition GMM export"
             << endl;
        exit(1);
    }
    if (hasTransitionPrototype) {
        alsOpt.transitionGMMPrototypePath = filesystem::absolute(
            alsOpt.transitionGMMPrototypePath).lexically_normal().string();
        alsOpt.transitionGMMOutputPath = filesystem::absolute(
            alsOpt.transitionGMMOutputPath).lexically_normal().string();
        if (!filesystem::is_regular_file(alsOpt.transitionGMMPrototypePath)) {
            cerr << "ERROR: transition GMM prototype file does not exist: "
                 << alsOpt.transitionGMMPrototypePath << endl;
            exit(1);
        }
        if (alsOpt.transitionGMMPrototypePath == alsOpt.transitionGMMOutputPath) {
            cerr << "ERROR: transition GMM prototype and output paths must differ"
                 << endl;
            exit(1);
        }
        if (filesystem::exists(alsOpt.transitionGMMOutputPath) &&
            filesystem::file_size(alsOpt.transitionGMMOutputPath) > 0) {
            cerr << "ERROR: transition GMM output already exists and is non-empty: "
                 << alsOpt.transitionGMMOutputPath << endl;
            exit(1);
        }
        if (alsOpt.runId.empty() ||
            alsOpt.transitionGMMGraphHash.empty() ||
            alsOpt.transitionGMMPatternHash.empty() ||
            alsOpt.transitionGMMTargetRegion.empty() ||
            alsOpt.transitionGMMBoundaryNodes.empty()) {
            cerr << "ERROR: transition GMM export requires run id, graph/pattern "
                 << "SHA-256, target region, and boundary-node provenance" << endl;
            exit(1);
        }
        if (!IsSha256Identity(alsOpt.transitionGMMGraphHash) ||
            !IsSha256Identity(alsOpt.transitionGMMPatternHash) ||
            (!alsOpt.transitionGMMBaseStateHash.empty() &&
             !IsSha256Identity(alsOpt.transitionGMMBaseStateHash))) {
            cerr << "ERROR: transition GMM graph, pattern, and optional base-state "
                 << "identities must use sha256:<64 lowercase hex>" << endl;
            exit(1);
        }
        filesystem::path parent = filesystem::path(
            alsOpt.transitionGMMOutputPath).parent_path();
        if (!parent.empty())
            filesystem::create_directories(parent);
        if (!alsOpt.transitionGMMPackedTraceDir.empty()) {
            alsOpt.transitionGMMPackedTraceDir = filesystem::absolute(
                alsOpt.transitionGMMPackedTraceDir).lexically_normal().string();
            const filesystem::path traceDirectory(
                alsOpt.transitionGMMPackedTraceDir);
            if (filesystem::exists(traceDirectory)
                && !filesystem::is_directory(traceDirectory)) {
                cerr << "ERROR: packed transition trace target is not a directory: "
                     << traceDirectory << endl;
                exit(1);
            }
            if (filesystem::exists(traceDirectory)
                && !filesystem::is_empty(traceDirectory)) {
                cerr << "ERROR: packed transition trace directory is non-empty: "
                     << traceDirectory << endl;
                exit(1);
            }
            filesystem::create_directories(traceDirectory);
        }
        if (distrType == "SELF") {
            if (alsOpt.transitionGMMPolicyPatternFile.empty()) {
                cerr << "ERROR: SELF transition-GMM collection requires "
                     << "transitionGMMPolicyPatternFile distinct from patternFile"
                     << endl;
                exit(1);
            }
            alsOpt.transitionGMMPolicyPatternFile = filesystem::absolute(
                alsOpt.transitionGMMPolicyPatternFile).lexically_normal().string();
            if (!filesystem::is_regular_file(
                    alsOpt.transitionGMMPolicyPatternFile)) {
                cerr << "ERROR: transition GMM policy pattern file does not exist: "
                     << alsOpt.transitionGMMPolicyPatternFile << endl;
                exit(1);
            }
            if (filesystem::absolute(alsOpt.transitionGMMPolicyPatternFile)
                    .lexically_normal()
                == filesystem::absolute(alsOpt.patternFilePath)
                    .lexically_normal()) {
                cerr << "ERROR: policy and truth pattern files must be distinct"
                     << endl;
                exit(1);
            }
        }
    }
    const bool hasSignedScorer = alsOpt.signedLACScorerConfig.Enabled();
    if (hasSignedScorer) {
        auto& scorer = alsOpt.signedLACScorerConfig;
        if (!hasTransitionPrototype || scorer.modelPath.empty() ||
            scorer.graphTensorPath.empty() || scorer.modelHash.empty() ||
            scorer.graphTensorHash.empty() || scorer.graphHash.empty() ||
            scorer.prototypeHash.empty() || scorer.metric.empty() ||
            scorer.targetRegion.empty() || scorer.wrapperHash.empty() ||
            scorer.irHash.empty() || scorer.truthPatternHash.empty() ||
            scorer.executionBackend.empty()) {
            cerr << "ERROR: signed-LAC scoring requires transition GMM export and "
                 << "complete model/graph/prototype/metric provenance" << endl;
            exit(1);
        }
        scorer.modelPath = filesystem::absolute(
            scorer.modelPath).lexically_normal().string();
        scorer.graphTensorPath = filesystem::absolute(
            scorer.graphTensorPath).lexically_normal().string();
        scorer.controllerDirectory = filesystem::absolute(
            scorer.controllerDirectory).lexically_normal().string();
        if (!filesystem::is_regular_file(scorer.modelPath) ||
            !filesystem::is_regular_file(scorer.graphTensorPath)) {
            cerr << "ERROR: signed-LAC model and graph tensor must be regular files"
                 << endl;
            exit(1);
        }
        filesystem::create_directories(scorer.controllerDirectory);
        if (!filesystem::is_directory(scorer.controllerDirectory)) {
            cerr << "ERROR: signed-LAC controller path is not a directory" << endl;
            exit(1);
        }
        if (scorer.graphHash != alsOpt.transitionGMMGraphHash ||
            scorer.targetRegion != alsOpt.transitionGMMTargetRegion) {
            cerr << "ERROR: signed-LAC graph/region disagrees with transition GMM"
                 << endl;
            exit(1);
        }
        if (!IsSha256Identity(scorer.wrapperHash) ||
            !IsSha256Identity(scorer.irHash) ||
            !IsSha256Identity(scorer.truthPatternHash) ||
            scorer.mapeBound <= 0.0 || scorer.nmedBound <= 0.0 ||
            scorer.nmseBound <= 0.0 || scorer.mseBound <= 0.0) {
            cerr << "ERROR: signed-LAC exact replay requires wrapper/IR/truth "
                 << "SHA-256 and four positive metric bounds" << endl;
            exit(1);
        }
        if (!isfinite(scorer.qCurrent) || scorer.qCurrent < 0.0 ||
            !isfinite(scorer.qMax) || scorer.qMax <= 0.0 ||
            scorer.qCurrent > scorer.qMax || scorer.controllerDirectory.empty() ||
            scorer.topB <= 0 || scorer.decisionTimeoutSeconds <= 0.0 ||
            scorer.decisionPollMilliseconds <= 0 || scorer.batchSize <= 0 ||
            (scorer.torchThreads != 1 && scorer.torchThreads != 4 &&
             scorer.torchThreads != 8)) {
            cerr << "ERROR: invalid signed-LAC quality/controller/batch configuration" << endl;
            exit(1);
        }
        if (alsOpt.errUppBounds.size() > 1) {
            cerr << "ERROR: signed-LAC trajectories freeze one q_max; run each "
                 << "quality bound as a separate campaign trajectory" << endl;
            exit(1);
        }
        if (!SignedLACScorer::BuiltWithLibTorch()) {
            cerr << "ERROR: signed-LAC scoring requested, but ResubALS was built "
                 << "without -DHALS_ENABLE_TORCH=ON" << endl;
            exit(1);
        }
    }

    // configure random seed
    if (alsOpt.sourceSeed == 0) {
        random::mt19937 rng(time(0));
        boost::uniform_int <> unif(INT_MIN, INT_MAX);
        alsOpt.sourceSeed = static_cast <unsigned> (unif(rng));
    }

    // configure LAC type
    alsOpt.lacType = LAC_TYPE::RESUB;

    // configure input distribution type
    if (distrType == "UNIF")
        alsOpt.distrType = DISTR_TYPE::UNIF;
    else if (distrType == "ENUM") {
        alsOpt.distrType = DISTR_TYPE::ENUM;
        assert(Abc_NtkPiNum(abcMan.GetNet()) < 20);
        alsOpt.nFrame = 1ll << Abc_NtkPiNum(abcMan.GetNet());
        cout << "nFrame for enumeration = " << alsOpt.nFrame << endl;
    }
    else if (distrType == "SELF") {
        alsOpt.distrType = DISTR_TYPE::SELF;
        assert(!alsOpt.patternFilePath.empty());
    }
    else {
        cerr << "ERROR: unknown distrType: " << distrType << ", valid: UNIF, ENUM, SELF" << endl;
        exit(1);
    }
    if (alsOpt.distrType == DISTR_TYPE::ENUM &&
        alsOpt.transitionGMMPolicyFrames != 0 &&
        alsOpt.transitionGMMPolicyFrames != alsOpt.nFrame) {
        cerr << "ERROR: ENUM policy frames must equal the exhaustive truth count"
             << endl;
        exit(1);
    }
    if (hasTransitionPrototype && alsOpt.transitionGMMPolicyFrames == 0)
        alsOpt.transitionGMMPolicyFrames = alsOpt.nFrame;
    if (alsOpt.nFrame <= 0 || alsOpt.nFrame4ResubGen <= 0) {
        cerr << "ERROR: nFrame and nFrame4ResubGen must be positive" << endl;
        exit(1);
    }
    if (alsOpt.distrType == DISTR_TYPE::UNIF &&
        ((alsOpt.nFrame & 63) != 0 ||
         (hasTransitionPrototype &&
          (alsOpt.transitionGMMPolicyFrames & 63) != 0))) {
        cerr << "ERROR: UNIF truth and policy frame counts must be multiples of 64"
             << endl;
        exit(1);
    }

    assert(alsOpt.outputNum >= 1);
    if (hasTransitionPrototype &&
        static_cast<int>(alsOpt.transitionGMMBoundaryNodes.size()) !=
            alsOpt.outputNum) {
        cerr << "ERROR: transitionGMMBoundaryNodes must contain exactly outputNum "
             << "entries" << endl;
        exit(1);
    }
    if (!alsOpt.outputWidths.empty()) {
        assert(static_cast<int>(alsOpt.outputWidths.size()) == alsOpt.outputNum);
        int totalWidth = 0;
        for (auto width: alsOpt.outputWidths)
            totalWidth += width;
        assert(totalWidth == Abc_NtkPoNum(abcMan.GetNet()));
    }
    else {
        assert(Abc_NtkPoNum(abcMan.GetNet()) % alsOpt.outputNum == 0);
    }
    assert(alsOpt.designMetricWeight > 0.0);
    if (alsOpt.probeRound < 0 || alsOpt.probeTimeoutSec < 0.0 || alsOpt.probePollMs <= 0) {
        cerr << "ERROR: probeRound/probeTimeoutSec must be non-negative and probePollMs must be positive" << endl;
        exit(1);
    }
    if (alsOpt.probeRound > 0 && alsOpt.maxRound > 0 && alsOpt.probeRound > alsOpt.maxRound) {
        cerr << "ERROR: probeRound cannot exceed a finite maxRound" << endl;
        exit(1);
    }
    
    if (alsOpt.errUppBounds.empty())
        alsOpt.errUppBounds.emplace_back(alsOpt.errUppBound);
    for (size_t i = 1; i < alsOpt.errUppBounds.size(); ++i) {
        if (alsOpt.errUppBounds[i] < alsOpt.errUppBounds[i - 1]) {
            cerr << "ERROR: errUppBounds must be ordered from tight to loose" << endl;
            exit(1);
        }
    }
    if (!alsOpt.designModelPath.empty() && alsOpt.designErrUppBounds.empty()) {
        if (alsOpt.designErrUppBound >= 0.0)
            alsOpt.designErrUppBounds.emplace_back(alsOpt.designErrUppBound);
        else
            alsOpt.designErrUppBounds = alsOpt.errUppBounds;
    }
    if (!alsOpt.designErrUppBounds.empty()) {
        if (alsOpt.designErrUppBounds.size() != alsOpt.errUppBounds.size()) {
            cerr << "ERROR: designErrUppBounds length must match errUppBounds length" << endl;
            exit(1);
        }
        for (size_t i = 1; i < alsOpt.designErrUppBounds.size(); ++i) {
            if (alsOpt.designErrUppBounds[i] < alsOpt.designErrUppBounds[i - 1]) {
                cerr << "ERROR: designErrUppBounds must be ordered from tight to loose" << endl;
                exit(1);
            }
        }
    }

    // configure error metric type
    if (metrType == "MAE" || metrType == "MED")
        alsOpt.metrType = METR_TYPE::MED;
    else if (metrType == "NMED") {
        alsOpt.metrType = METR_TYPE::MED;
        auto bitWidthPerOutput = alsOpt.outputWidths.empty()
            ? Abc_NtkPoNum(abcMan.GetNet()) / alsOpt.outputNum
            : *max_element(alsOpt.outputWidths.begin(), alsOpt.outputWidths.end());
        for (auto& bound: alsOpt.errUppBounds) {
            auto errUppBoundHP = BigFlt(bound) *
                GetErrorMetricNormalization(bitWidthPerOutput, alsOpt.isSign);
            assert(errUppBoundHP < BigFlt(DBL_MAX));
            bound = (double)(errUppBoundHP);
        }
    }
    else if (metrType == "MSE" || metrType == "NMSE") {
        alsOpt.metrType = METR_TYPE::MSE;
        if (metrType == "NMSE") {
            auto bitWidthPerOutput = alsOpt.outputWidths.empty()
                ? Abc_NtkPoNum(abcMan.GetNet()) / alsOpt.outputNum
                : *max_element(alsOpt.outputWidths.begin(), alsOpt.outputWidths.end());
            const BigFlt normalization = GetErrorMetricNormalization(
                bitWidthPerOutput, alsOpt.isSign);
            for (auto& bound: alsOpt.errUppBounds) {
                auto errUppBoundHP = BigFlt(bound) * normalization * normalization;
                assert(errUppBoundHP < BigFlt(DBL_MAX));
                bound = static_cast<double>(errUppBoundHP);
            }
        }
    }
    else if (metrType == "MAPE")
        alsOpt.metrType = METR_TYPE::MAPE;
    else {
        cerr << "ERROR: unknown metrType: " << metrType << ", valid: MAE/MED, NMED, MSE, NMSE, MAPE" << endl;
        exit(1);
    }

    if (alsOpt.signedLACScorerConfig.Enabled()) {
        auto& scorer = alsOpt.signedLACScorerConfig;
        if (scorer.metric != metrType) {
            cerr << "ERROR: signed-LAC checkpoint metric " << scorer.metric
                 << " does not match metrType " << metrType << endl;
            exit(1);
        }
    }

    // errUppBounds are the local search bounds after metric normalization.
    // In legacy linear mode ALS operates on one kernel, so derive the local
    // per-kernel bound from the design-level bound.
    if (alsOpt.designModelPath.empty())
        for (auto& bound: alsOpt.errUppBounds)
            bound /= alsOpt.designMetricWeight;
    alsOpt.errUppBound = alsOpt.errUppBounds.front();
}

void ALS(ALSOpt & alsOpt) {
    alsOpt.Print();
    ALSMan alsMan(alsOpt);
    alsMan.Run();
}


int main(int argc, char * argv[]) {
    // start abc engine
    GlobStartAbc();

    // parse options
    parser option = CommPars(argc, argv);
    ALSOpt alsOpt;
    string accCirc = option.get <string> ("accCirc");
    alsOpt.standCellPath = option.get <string> ("standCell");
    alsOpt.outpPath = option.get <string> ("outpPath");
    alsOpt.patternFilePath = option.get <string> ("patternFile");
    string metrType = option.get <string> ("metrType");
    string distrType = option.get <string> ("distrType");
    alsOpt.sourceSeed = option.get <unsigned> ("seed");
    alsOpt.errUppBound = option.get <double> ("errUppBound");
    alsOpt.errUppBounds = ParseDoubleVector(option.get <string> ("errUppBounds"));
    alsOpt.designErrUppBound = option.get <double> ("designErrUppBound");
    alsOpt.designErrUppBounds = ParseDoubleVector(option.get <string> ("designErrUppBounds"));
    alsOpt.designMetricWeight = option.get <double> ("designMetricWeight");
    alsOpt.designModelPath = option.get <string> ("designModelPath");
    alsOpt.featureIndices = ParseIntVector(option.get <string> ("featureIndices"));
    alsOpt.featureMetrics = ParseStringVector(option.get <string> ("featureMetrics"));
    alsOpt.initialFeatureVector = ParseDoubleVector(option.get <string> ("initialFeatureVector"));
    alsOpt.modelSafetyMargin = option.get <double> ("modelSafetyMargin");
    alsOpt.earlyExitPolicy = option.get <string> ("earlyExitPolicy");
    alsOpt.scalarKernelFeature = option.exist("scalarKernelFeature");
    alsOpt.nFrame = option.get <int> ("nFrame");
    alsOpt.nFrame4ResubGen = option.get <int> ("nFrame4ResubGen");
    alsOpt.maxCandResub = option.get <int> ("maxCandResub");
    alsOpt.maxExactCandValidate = option.get <int> ("maxExactCandValidate");
    alsOpt.candidateValidationPolicy = option.get <string> ("candidateValidationPolicy");
    alsOpt.candidateFeatureSource = option.get <string> ("candidateFeatureSource");
    alsOpt.candidateAuditDir = option.get <string> ("candidateAuditDir");
    alsOpt.candidateAuditLimit = option.get <int> ("candidateAuditLimit");
    alsOpt.publicationCandidateDir = option.get<string>("publicationCandidateDir");
    alsOpt.publicationCandidateLimit = option.get<int>("publicationCandidateLimit");
    alsOpt.transitionGMMPrototypePath = option.get<string>("transitionGMMPrototypes");
    alsOpt.transitionGMMOutputPath = option.get<string>("transitionGMMOutput");
    alsOpt.transitionGMMGraphHash = option.get<string>("transitionGMMGraphHash");
    alsOpt.transitionGMMBaseStateHash = option.get<string>("transitionGMMBaseStateHash");
    alsOpt.transitionGMMPatternHash = option.get<string>("transitionGMMPatternHash");
    alsOpt.transitionGMMTargetRegion = option.get<string>("transitionGMMTargetRegion");
    alsOpt.transitionGMMBoundaryNodes = ParseStringVector(
        option.get<string>("transitionGMMBoundaryNodes"));
    alsOpt.transitionGMMPolicyPatternFile = option.get<string>("transitionGMMPolicyPatternFile");
    alsOpt.transitionGMMPolicyFrames = option.get<int>("transitionGMMPolicyFrames");
    alsOpt.transitionGMMIncludeRawTrace = option.exist("transitionGMMIncludeRawTrace");
    alsOpt.transitionGMMPackedTraceDir = option.get<string>(
        "transitionGMMPackedTraceDir");
    alsOpt.signedLACScorerConfig.modelPath = option.get<string>("signedLACModel");
    alsOpt.signedLACScorerConfig.graphTensorPath = option.get<string>("signedLACGraphTensor");
    alsOpt.signedLACScorerConfig.modelHash = option.get<string>("signedLACModelHash");
    alsOpt.signedLACScorerConfig.graphHash = alsOpt.transitionGMMGraphHash;
    alsOpt.signedLACScorerConfig.graphTensorHash = option.get<string>("signedLACGraphTensorHash");
    alsOpt.signedLACScorerConfig.prototypeHash = option.get<string>("signedLACPrototypeHash");
    alsOpt.signedLACScorerConfig.metric = option.get<string>("signedLACMetric");
    alsOpt.signedLACScorerConfig.targetRegion = alsOpt.transitionGMMTargetRegion;
    alsOpt.signedLACScorerConfig.wrapperHash = option.get<string>("signedLACWrapperHash");
    alsOpt.signedLACScorerConfig.irHash = option.get<string>("signedLACIRHash");
    alsOpt.signedLACScorerConfig.truthPatternHash = option.get<string>("signedLACTruthPatternHash");
    alsOpt.signedLACScorerConfig.executionBackend = option.get<string>("signedLACExecutionBackend");
    alsOpt.signedLACScorerConfig.mapeBound = option.get<double>("signedLACMAPEBound");
    alsOpt.signedLACScorerConfig.nmedBound = option.get<double>("signedLACNMEDBound");
    alsOpt.signedLACScorerConfig.nmseBound = option.get<double>("signedLACNMSEBound");
    alsOpt.signedLACScorerConfig.mseBound = option.get<double>("signedLACMSEBound");
    alsOpt.signedLACScorerConfig.qCurrent = option.get<double>("signedLACInitialQ");
    alsOpt.signedLACScorerConfig.qMax = option.get<double>("signedLACQMax");
    if (alsOpt.signedLACScorerConfig.qMax < 0.0)
        alsOpt.signedLACScorerConfig.qMax = alsOpt.errUppBounds.empty()
            ? alsOpt.errUppBound : alsOpt.errUppBounds.front();
    alsOpt.signedLACScorerConfig.controllerDirectory = option.get<string>("signedLACControllerDir");
    alsOpt.signedLACScorerConfig.topB = option.get<int>("signedLACTopB");
    alsOpt.signedLACScorerConfig.decisionTimeoutSeconds = option.get<double>("signedLACDecisionTimeoutSec");
    alsOpt.signedLACScorerConfig.decisionPollMilliseconds = option.get<int>("signedLACDecisionPollMs");
    alsOpt.signedLACScorerConfig.batchSize = option.get<int>("signedLACBatchSize");
    alsOpt.signedLACScorerConfig.torchThreads = option.get<int>("signedLACTorchThreads");
    if (alsOpt.candidateValidationPolicy != "size_gain" &&
        alsOpt.candidateValidationPolicy != "complete_size_gain" &&
        alsOpt.candidateValidationPolicy != "stratified" &&
        alsOpt.candidateValidationPolicy != "gradient" &&
        alsOpt.candidateValidationPolicy != "gradient_stratified" &&
        alsOpt.candidateValidationPolicy != "mlp_guarded") {
        cerr << "ERROR: unknown candidateValidationPolicy: " << alsOpt.candidateValidationPolicy << endl;
        return 1;
    }
    if (alsOpt.candidateFeatureSource != "simulation" &&
        alsOpt.candidateFeatureSource != "vecbee" &&
        alsOpt.candidateFeatureSource != "vecbee_shadow") {
        cerr << "ERROR: unknown candidateFeatureSource: "
             << alsOpt.candidateFeatureSource
             << ", valid: simulation, vecbee, vecbee_shadow" << endl;
        return 1;
    }
    if (alsOpt.earlyExitPolicy != "legacy_scalar" &&
        alsOpt.earlyExitPolicy != "disabled") {
        cerr << "ERROR: unknown earlyExitPolicy: " << alsOpt.earlyExitPolicy
             << ", valid: legacy_scalar, disabled"
             << endl;
        return 1;
    }
    alsOpt.nThread = option.get <int> ("nThread");
    alsOpt.maxRound = option.get <int> ("maxRound");
    alsOpt.maxZeroErrorFallbackRounds = option.get <int> ("maxZeroErrorFallbackRounds");
    alsOpt.probeRound = option.get <int> ("probeRound");
    alsOpt.probeSummaryPath = option.get <string> ("probeSummaryPath");
    alsOpt.targetDecisionPath = option.get <string> ("targetDecisionPath");
    alsOpt.probeTimeoutSec = option.get <double> ("probeTimeoutSec");
    alsOpt.probePollMs = option.get <int> ("probePollMs");
    alsOpt.runId = option.get <string> ("runId");
    alsOpt.enableFastErrEst = option.exist("enableFastErrEst");
    alsOpt.isSign = option.exist("isSigned");
    alsOpt.outputNum = option.get <int> ("outputNum");
    alsOpt.outputWidths = ParseIntVector(option.get <string> ("outputWidths"));
    // configure options
    ConfigureOptions(alsOpt, accCirc, metrType, distrType);

    // ALS
    ALS(alsOpt);

    // stop abc engine
    GlobStopAbc();
    return 0;
}
