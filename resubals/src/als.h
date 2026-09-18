#pragma once


#include "header.h"
#include "my_abc.h"
#include "simulator.h"
#include "error.h"
#include "lac.h"
#include "design_error_model.h"
#include "signed_lac_scorer.h"


struct ALSOpt {
    bool isSign = false;
    bool enableFastErrEst = false;
    unsigned sourceSeed = 0;
    LAC_TYPE lacType = LAC_TYPE::RESUB;
    DISTR_TYPE distrType = DISTR_TYPE::UNIF;
    METR_TYPE metrType = METR_TYPE::MED;
    int nFrame = 102400;
    int nFrame4ResubGen = 32;
    int maxCandResub = 50000;
    int maxExactCandValidate = 64;
    int nThread = 32;
    int maxRound = 0;
    int maxZeroErrorFallbackRounds = 1;
    // int maxLevelDiff = INT_MAX;
    double errUppBound = 0.05;
    std::vector<double> errUppBounds;
    double designErrUppBound = -1.0;
    std::vector<double> designErrUppBounds;
    double designMetricWeight = 1.0;
    double modelSafetyMargin = 0.0;
    bool scalarKernelFeature = false;
    std::string earlyExitPolicy = "legacy_scalar";
    int outputNum = 1;
    std::vector<int> outputWidths;
    std::string outpPath = "./tmp";
    std::string standCellPath = "./nangate_45nm_typ.lib";
    std::string patternFilePath = "";
    std::string designModelPath = "";
    std::vector<int> featureIndices;
    std::vector<std::string> featureMetrics;
    std::vector<double> initialFeatureVector;
    std::string candidateValidationPolicy = "size_gain";
    std::string candidateFeatureSource = "simulation";
    // Optional, write-only experiment instrumentation.  Empty keeps the
    // historical ALS path byte-for-byte free of candidate audit output.
    std::string candidateAuditDir = "";
    int candidateAuditLimit = 32;
    // Hash-ordered, pre-estimation real candidate pool for publication parity.
    // The external parity tool performs port/equivalence filtering; this path
    // deliberately does not select candidates by predicted or measured error.
    std::string publicationCandidateDir = "";
    int publicationCandidateLimit = 512;
    // Optional fixed-cost VECBEE boundary-transition export.  Both paths must
    // be supplied together; otherwise the historical scalar path is used.
    std::string transitionGMMPrototypePath = "";
    std::string transitionGMMOutputPath = "";
    std::string transitionGMMGraphHash = "";
    std::string transitionGMMBaseStateHash = "";
    std::string transitionGMMPatternHash = "";
    std::string transitionGMMTargetRegion = "";
    std::vector<std::string> transitionGMMBoundaryNodes;
    std::string transitionGMMPolicyPatternFile = "";
    // Zero preserves the historical behaviour (same count as truth nFrame).
    int transitionGMMPolicyFrames = 0;
    bool transitionGMMIncludeRawTrace = false;
    std::string transitionGMMPackedTraceDir = "";
    SignedLACScorerConfig signedLACScorerConfig;
    // Probe--Predict--Target is opt-in.  A zero probeRound preserves the
    // historical uninterrupted ALS behaviour.
    int probeRound = 0;
    std::string probeSummaryPath = "";
    std::string targetDecisionPath = "";
    double probeTimeoutSec = 0.0;
    int probePollMs = 100;
    std::string runId = "";
    abc::Abc_Ntk_t * pNtk = nullptr;

    void Print();
};


class ALSMan {
private:
    enum class StopReason {
        NONE,
        NO_FEASIBLE_REDUCING_LAC,
        NO_LACS_GENERATED,
        BASE_ERROR_INFEASIBLE,
        MAX_ROUND_REACHED,
        TARGET_OPERATING_ROUND_REACHED,
        TARGET_WORK_CAP_REACHED,
    };

    struct ParetoRecord {
        int round;
        int phase;
        double kernelErr;
        std::vector<double> errorVector;
        ErrorMetrics multiMetrics;
        double designErr;
        double designErrBound;
        double area;
        double delay;
        std::string netlistPath;
        std::string lac;
        long long candidatesGenerated = 0;
        long long candidatesScanned = 0;
        long long exactValidations = 0;
        long long workUnits = 0;
        long long cumulativeCandidatesGenerated = 0;
        long long cumulativeCandidatesScanned = 0;
        long long cumulativeExactValidations = 0;
        long long cumulativeWorkUnits = 0;
        double roundRuntimeSec = 0.0;
        double cumulativeRuntimeSec = 0.0;
        std::string stopReason;
    };

    struct TargetDecision {
        bool abstain = true;
        int expectedOperatingRound = -1;
        long long additionalWorkCap = -1;
        std::vector<double> expectedErrorProfile;
        std::string riskLevel;
        double oodScore = 0.0;
        bool hasOodScore = false;
        std::string rawJson;
    };

    struct BoundPhaseRecord {
        int phase;
        double bound;
        int startRound;
        int endRound;
        std::string stopReason;
        double cumulativeRuntimeSec;
    };

    struct CandidateAuditRecord {
        int round = 0;
        int phase = 0;
        int candidateRank = -1;
        int validationIndex = -1;
        int sizeGain = 0;
        unsigned searchSeed = 0;
        unsigned policySeed = 0;
        unsigned truthSeed = 0;
        int searchFrames = 0;
        int policyFrames = 0;
        int truthFrames = 0;
        double baseKernelErr = 0.0;
        double baseDesignErr = 0.0;
        double estimatedKernelErr = 0.0;
        double measuredKernelErr = 0.0;
        std::vector<double> baseErrorVector;
        std::vector<double> errorVector;
        ErrorMetrics baseMultiMetrics;
        ErrorMetrics multiMetrics;
        std::vector<double> localFeatures;
        std::vector<double> fullFeatures;
        double predictedDesignErr = 0.0;
        bool featureInRange = true;
        bool feasible = false;
        std::string metricType;
        std::string candidateFeatureSource;
        std::string baseStateHash;
        std::string policyPatternHash;
        std::string candidateHash;
        std::string lac;
        std::string baseNetlistPath;
        std::string candidateNetlistPath;
    };

    bool isSign;
    bool enableFastErrEst;
    unsigned sourceSeed;
    unsigned seed;
    LAC_TYPE lacType;
    DISTR_TYPE distrType;
    METR_TYPE metrType;
    int nFrame;
    int nFrame4ResubGen;
    int maxCandResub;
    int maxExactCandValidate;
    int nThread;
    int maxRound;
    int maxZeroErrorFallbackRounds;
    int maxLevelDiff;
    int round;
    int boundPhase;
    int zeroErrorFallbackRounds;
    double errUppBound;
    double designErrUppBound;
    std::vector<double> errUppBounds;
    std::vector<double> designErrUppBounds;
    double designMetricWeight;
    double modelSafetyMargin;
    bool scalarKernelFeature;
    std::string earlyExitPolicy;
    int outputNum;
    std::vector<int> outputWidths;
    // double errVariationTolerance;
    double maxDelay;
    NetMan accNet;
    std::string standCellPath;
    std::string outpPath;
    std::string patternFilePath;
    std::string transitionGMMPolicyPatternFile;
    int transitionGMMPolicyFrames;
    std::string lastAppliedLac;
    std::string candidateValidationPolicy;
    std::string candidateFeatureSource;
    std::string candidateAuditDir;
    int candidateAuditLimit;
    std::string publicationCandidateDir;
    int publicationCandidateLimit;
    TransitionGMMExportConfig transitionGMMConfig;
    SignedLACScorerConfig signedLACScorerConfig;
    std::unique_ptr<SignedLACScorer> signedLACScorer;
    std::vector<ParetoRecord> paretoRecords;
    std::vector<ParetoRecord> trajectoryRecords;
    std::vector<BoundPhaseRecord> boundPhaseRecords;
    StopReason lastStopReason;
    DesignErrorModel designErrorModel;
    bool useDesignErrorModel;
    std::vector<int> featureIndices;
    std::vector<std::string> featureMetrics;
    std::vector<double> designFeatureVector;
    boost::mt19937 randGen;

    int probeRound;
    std::string probeSummaryPath;
    std::string targetDecisionPath;
    double probeTimeoutSec;
    int probePollMs;
    std::string runId;
    bool probeHandled;
    bool targetActive;
    long long targetWorkLimit;
    TargetDecision targetDecision;

    long long roundCandidatesGenerated;
    long long roundCandidatesScanned;
    long long roundExactValidations;
    long long cumulativeCandidatesGenerated;
    long long cumulativeCandidatesScanned;
    long long cumulativeExactValidations;
    long long cumulativeWorkUnits;
    bool roundAccountingFinalized;
    std::chrono::steady_clock::time_point runStartTime;
    std::chrono::steady_clock::time_point roundStartTime;

    std::vector<ParetoRecord> BuildLocalPareto(const std::vector<ParetoRecord>& records) const;
    void WriteRecordsCsv(const std::string& path, const std::vector<ParetoRecord>& records) const;
    void WriteTrajectoryJsonl() const;
    std::string RecordJson(const ParetoRecord& record) const;
    std::string StopReasonString(StopReason reason) const;
    void BeginRoundAccounting();
    void FinalizeRoundAccounting();
    void MarkTrajectoryStop(StopReason reason);
    void WriteProbeSummary(const std::string& status) const;
    bool WaitForTargetDecision();
    bool HandleProbeBarrier();
    bool ShouldStopForTarget(StopReason& reason) const;
    bool UseGradientCandidateRanking() const;
    bool UseStratifiedCandidateOrdering() const;
    double LocalEstimationUpperBound() const;
    VECBEEEarlyExitConfig BuildVECBEEEarlyExitConfig(
        const std::vector<double>& baseLocalFeatures,
        const std::vector<double>& baseFullFeatures
    ) const;
    std::vector<double> EstimateLocalDesignFeatureVector(double kernelErr) const;
    std::vector<double> ExpandDesignFeatureVector(const std::vector<double>& localFeatures) const;
    std::vector<double> EstimateFullDesignFeatureVector(double kernelErr) const;
    double PredictDesignErrorFromFullFeatures(const std::vector<double>& features) const;
    double EstimateCandidateRankingDelta(
        const std::vector<double>& baseFullFeatures,
        const std::vector<double>& candidateFullFeatures,
        double baseDesignErr
    ) const;
    void WriteCandidateAuditRecords(
        const std::vector<CandidateAuditRecord>& records,
        const std::string& selectedLac
    ) const;
    void WritePublicationCandidatePool(
        NetMan& baseNet, LACMan& lacMan, double baseError,
        unsigned searchSeed, unsigned policySeed, unsigned truthSeed
    );

    ALSMan(const ALSMan &);
    ALSMan(ALSMan &&);
    ALSMan & operator = (const ALSMan &);
    ALSMan & operator = (ALSMan &&);

public:
    explicit ALSMan(ALSOpt & opt);
    ~ALSMan() = default;
    void Run(); 
    double ApplyTheBestLAC(NetMan & net);
    unsigned NewSeed();
    void ApplyLacPro(NetMan & net, std::shared_ptr<LAC> pLac, double backErr);
    void ExactSimpl(NetMan & net);
    double Eval(NetMan & net, double err, bool useYosys = false, bool isInitialCircuit = false);
    void RecordPareto(double err, const std::vector<double>& errorVector, const ErrorMetrics& multiMetrics, double area, double delay, const std::string& netlistPath);
    void WriteParetoCsv() const;
    void WriteTrajectoryCsv() const;
    void WriteParetoPhaseCsv(int phase) const;
    void WriteBoundPhasesCsv() const;
    bool IsDesignErrorFeasible(double kernelErr) const;
    bool IsDesignErrorFeasible(const std::vector<double>& errorVector) const;
    double PredictDesignError(double kernelErr) const;
    double PredictDesignError(const std::vector<double>& errorVector) const;
    std::vector<double> SelectDesignFeatureVector(double kernelErr, const std::vector<double>& errorVector, const ErrorMetrics& multiMetrics) const;
    NetManPtr BuildErrorRateMiter(NetMan& accNet, NetMan& appNet, RETURN_VAR IntVect& miterId2AppId, RETURN_VAR IntVect& appId2MiterId);
    NetManPtr BuildXorOrCircuit(int nBits);
    NetManPtr BuildErrorDistanceMiter(NetMan& accNet, NetMan& appNet, RETURN_VAR IntVect& miterId2AppId, RETURN_VAR IntVect& appId2MiterId);
    NetManPtr BuildAbsoluteDifferenceCircuit(int nBits);
    NetManPtr BuildMiterWithYosys(NetMan& accNet, NetMan& appNet, RETURN_VAR IntVect& miterId2AppId, RETURN_VAR IntVect& appId2MiterId);
    NetManPtr BuildDeviationCircuit(int nBits);
    double ComputeError(Simulator& accSmlt, NetMan& net);
    double ComputeError(Simulator& accSmlt, Simulator& appSmlt);
};
