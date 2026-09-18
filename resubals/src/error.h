#pragma once


#include "simulator.h"
#include "my_abc.h"
#include "transition_gmm.h"

#include <cmath>


// Canonical project-wide NMED/NMSE normalization.  Signed HALS datasets and
// wrapper testbenches use the maximum positive two's-complement magnitude;
// unsigned datasets use the full unsigned range.  The command-line NMED bound
// conversion and online multi-metric features must call this same helper.
static inline BigFlt GetErrorMetricNormalization(int width, bool isSigned) {
    assert(width > 0);
    const BigInt normalization = isSigned
        ? (BigInt(1) << (width - 1)) - 1
        : (BigInt(1) << width) - 1;
    assert(normalization > 0);
    return BigFlt(normalization);
}
#include "lac.h"


enum class METR_TYPE{
    MED, MSE, MAPE, SELF
};


enum class DISTR_TYPE {
    UNIF, ENUM, MIX, SELF
};

struct ErrorMetrics {
    double nmed = 0.0;
    double nmse = 0.0;
    double er = 0.0;
    double me = 0.0;

    std::vector<double> ToFeatureVector() const {
        return {nmed, nmse, er, me};
    }
};

struct CombinedErrorMetrics {
    double metricErr = 0.0;
    std::vector<double> mapeVector;
    ErrorMetrics multiMetrics;
};

struct VECBEEFeatureProfile {
    long long candidatesProcessed = 0;
    long long patternsVisited = 0;
    long long patternsPossible = 0;
    long long earlyExitCandidates = 0;
    long long legacyEarlyExitCandidates = 0;
    long long earlyExitChecks = 0;
    long long newValueTimeUs = 0;
    long long traceBuildTimeUs = 0;
    long long scalarMetricTimeUs = 0;
    long long gmmSketchTimeUs = 0;
    long long recordExportTimeUs = 0;
    long long jsonWriteTimeUs = 0;

    void Add(const VECBEEFeatureProfile& other) {
        candidatesProcessed += other.candidatesProcessed;
        patternsVisited += other.patternsVisited;
        patternsPossible += other.patternsPossible;
        earlyExitCandidates += other.earlyExitCandidates;
        legacyEarlyExitCandidates += other.legacyEarlyExitCandidates;
        earlyExitChecks += other.earlyExitChecks;
        newValueTimeUs += other.newValueTimeUs;
        traceBuildTimeUs += other.traceBuildTimeUs;
        scalarMetricTimeUs += other.scalarMetricTimeUs;
        gmmSketchTimeUs += other.gmmSketchTimeUs;
        recordExportTimeUs += other.recordExportTimeUs;
        jsonWriteTimeUs += other.jsonWriteTimeUs;
    }
};


struct VECBEEEarlyExitConfig {
    std::string policy = "legacy_scalar";

    bool IsDisabled() const {
        return policy == "disabled";
    }

    bool UsesLegacyScalar() const {
        return policy.empty() || policy == "legacy_scalar";
    }
};


static inline std::ostream & operator << (std::ostream & os, const METR_TYPE metrType) {
    const std::string strs[7] = {"MED", "MSE", "MAPE", "SELF"};
    os << strs[static_cast <ll> (metrType)];
    return os;
}


static inline std::ostream & operator << (std::ostream & os, const DISTR_TYPE distrType) {
    const std::string strs[4] = {"UNIF", "ENUM", "MIX", "SELF"};
    os << strs[static_cast <ll> (distrType)];
    return os;
}


class ErrMan {
private:
    NetMan & net0;
    NetMan & net1;
    std::shared_ptr <Simulator> pSmlt0;
    std::shared_ptr <Simulator> pSmlt1;
    unsigned seed;
    ll nFrame;
    DISTR_TYPE distrType;
    int outputNum;
    std::vector<int> outputWidths;
    std::string patternFilePath;

public:
    ErrMan(NetMan & netMan0, NetMan & netMan1, unsigned _seed, ll n_frame, DISTR_TYPE distr_type, int output_num, std::string pattern_file_path);
    ErrMan(NetMan & netMan0, NetMan & netMan1, unsigned _seed, ll n_frame, DISTR_TYPE distr_type, int output_num, std::vector<int> output_widths, std::string pattern_file_path);
    ~ErrMan() = default;
    ErrMan(const ErrMan &) = delete;
    ErrMan(ErrMan &&) = delete;
    ErrMan & operator = (const ErrMan &) = delete;
    ErrMan & operator = (ErrMan &&) = delete;

    void InitForStatErr();
    double CalcMeanErrDist(bool isSign);
    double CalcMeanSquareErr(bool isSign);
    double CalcMeanAbsPercentageErr(bool isSign);
    std::vector<double> CalcMeanAbsPercentageErrVector(bool isSign);
    ErrorMetrics CalcErrorMetrics(bool isSign);
    CombinedErrorMetrics CalcCombinedErrorMetrics(bool isSign, METR_TYPE metrType);
};


double CalcErr(NetMan & netMan0, NetMan & netMan1, bool isSign, unsigned seed, ll nFrame, METR_TYPE metrType, DISTR_TYPE distrType, int outputNum, const std::string & patternFilePath = "");
std::vector<double> CalcMapeErrVector(NetMan & netMan0, NetMan & netMan1, bool isSign, unsigned seed, ll nFrame, DISTR_TYPE distrType, int outputNum, const std::string & patternFilePath = "");
ErrorMetrics CalcMultiErrorMetrics(NetMan & netMan0, NetMan & netMan1, bool isSign, unsigned seed, ll nFrame, DISTR_TYPE distrType, int outputNum, const std::string & patternFilePath = "");
void InitSimulatorForStatErr(Simulator & smlt, DISTR_TYPE distrType, const std::string & patternFilePath = "");
CombinedErrorMetrics CalcCombinedErrorMetrics(NetMan & netMan0, NetMan & netMan1, bool isSign, unsigned seed, ll nFrame, METR_TYPE metrType, DISTR_TYPE distrType, int outputNum, const std::string & patternFilePath = "", const std::vector<int> & outputWidths = {});
CombinedErrorMetrics CalcCombinedErrorMetrics(const Simulator & accSmlt, NetMan & appNet, bool isSign, unsigned seed, ll nFrame, METR_TYPE metrType, DISTR_TYPE distrType, int outputNum, const std::string & patternFilePath = "", const std::vector<int> & outputWidths = {});
// double GetMSEFromSNR(NetMan & net, bool isSign, unsigned seed, ll nFrame, DISTR_TYPE distrType, double snr);


class VECBEEMan {
private:
    bool isSign;
    int outputNum;
    std::vector<int> outputWidths;
    unsigned seed;
    int nFrame;
    METR_TYPE metrType;
    LAC_TYPE lacType;
    DISTR_TYPE distrType;
    std::string patternFilePath;
    const int nThread;
    std::vector< std::vector<BitVect> > bdPo2Nodes; // bdPo2Node[poId][nodeId], the boolean difference of poId in terms of nodeId
    std::vector< std::vector<BitVect> > bdCut2Nodes; // bdCut2Node[nodeId][cutId], the boolean difference of nodeId in terms of cutId
    std::vector<AbcObjList> disjCuts;
    std::vector<AbcObjVect> cutNtks;
    std::vector<BitVect> poMarks;
    std::vector<ll> topoIds;
    TransitionGMMExportConfig transitionGMMConfig;
    std::shared_ptr<TransitionGMMPrototypeSet> transitionGMMPrototypes;
    std::vector<std::vector<TransitionGMMRecord>> transitionGMMRecords;
    std::vector<CombinedErrorMetrics> lacCombinedMetrics;
    VECBEEFeatureProfile featureProfile;
    VECBEEEarlyExitConfig earlyExitConfig;

public:
    VECBEEMan() = default;
    VECBEEMan(bool is_sign, int output_num, std::vector<int> output_widths, unsigned _seed, int n_frame, METR_TYPE metr_type, LAC_TYPE lac_type, DISTR_TYPE distr_type, const std::string & pattern_file_path, int n_thread, TransitionGMMExportConfig transition_gmm_config = {}, VECBEEEarlyExitConfig early_exit_config = {}):
        isSign(is_sign), outputNum(output_num), outputWidths(std::move(output_widths)), seed(_seed), nFrame(n_frame), metrType(metr_type), lacType(lac_type), distrType(distr_type), patternFilePath(pattern_file_path), nThread(n_thread), transitionGMMConfig(std::move(transition_gmm_config)), transitionGMMPrototypes(nullptr), earlyExitConfig(std::move(early_exit_config)) {}
    ~VECBEEMan() = default;
    VECBEEMan(const VECBEEMan &) = delete;
    VECBEEMan(VECBEEMan &&) = delete;
    VECBEEMan & operator = (const VECBEEMan &) = delete;
    VECBEEMan & operator = (VECBEEMan &&) = delete;

    void EstimateErrorBoundForEachLAC(Simulator& miterSmlt, LACMan& lacMan, const BigInt& upperBound, bool enableFastErrEst, const IntVect& miterId2AppId, const IntVect& appId2MiterId);
    void EstimateErrorBoundForEachLAC(Simulator& accSmlt, Simulator& appSmlt, LACMan& lacMan, const BigInt& upperBound, bool enableFastErrEst);
    void ComputeBooleanDifferenceOfPos2Nodes(Simulator& appSmlt, AbcObjVect& topoNodes, bool enableFastErrEst);
    void EstimateMEDBound(Simulator& accSmlt, Simulator& appSmlt, LACMan& lacMan);
    void BatchErrEstPro(NetMan& accNet, NetMan& appNet, LACMan& lacMan, const BigInt& upperBound, bool enableFastErrEst, BigInt& backErrInt, bool collectCombinedMetrics = false);
    void FindDisjCut(NetMan& net, AbcObjVect& topoNodes);
    void FindDisjCutOfNode(abc::Abc_Obj_t* pObj, AbcObjList& disjCut);
    void ExpandCut(abc::Abc_Obj_t* pObj, AbcObjList& disjCut);
    abc::Abc_Obj_t* ExpandWhich(AbcObjList& disjCut);
    void CalcBoolDiffCut2Node(Simulator& appSmlt, AbcObjVect & topoNodes);
    void CalcBoolDiffCut2NodeParallelly(Simulator& appSmlt, AbcObjVect & topoNodes);
    void CalcBoolDiffPo2Node(Simulator& appSmlt, AbcObjVect& topoNodes);
    void CalcBoolDiffPo2NodeParallelly(Simulator& appSmlt, AbcObjVect& topoNodes);
    void CalcLACNonERErrs(Simulator& accSmlt, Simulator& appSmlt, LACMan& lacMan, const BigInt& upperBound);
    void CalcLACNonERErrsNew(Simulator& accSmlt, Simulator& appSmlt, LACMan& lacMan, const BigInt& upperBound, BigInt& backErrInt, bool collectCombinedMetrics = false);
    const std::vector<std::vector<TransitionGMMRecord>>& GetTransitionGMMRecords() const {
        return transitionGMMRecords;
    }
    const std::vector<CombinedErrorMetrics>& GetLACCombinedMetrics() const {
        return lacCombinedMetrics;
    }
    const VECBEEFeatureProfile& GetFeatureProfile() const {
        return featureProfile;
    }

};
