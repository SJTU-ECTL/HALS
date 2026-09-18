#include "error.h"


using namespace std;
using namespace abc;
using namespace boost;

void InitSimulatorForStatErr(Simulator & smlt, DISTR_TYPE distrType, const string & patternFilePath) {
    if (distrType == DISTR_TYPE::UNIF) {
        smlt.InpUnifFast();
    }
    else if (distrType == DISTR_TYPE::ENUM) {
        smlt.InpEnum();
    }
    else if (distrType == DISTR_TYPE::MIX) {
        smlt.InpMix();
    }
    else if (distrType == DISTR_TYPE::SELF) {
        assert(!patternFilePath.empty());
        smlt.InpSelf(patternFilePath);
    }
    else
        assert(0);
    smlt.Sim();
}


struct CombinedMetricSums {
    BigFlt sumAbs = 0;
    BigFlt sumRawSq = 0;
    BigFlt sumNormAbs = 0;
    BigFlt sumNormSq = 0;
    BigFlt sumNormBias = 0;
    BigInt errCount = 0;
    vector<BigFlt> mapeSums;

    explicit CombinedMetricSums(int outputNum):
        mapeSums(outputNum, BigFlt(0)) {}
};


static vector<int> ResolveCombinedOutputWidths(
    int poNum,
    int outputNum,
    const vector<int>& configuredOutputWidths
) {
    vector<int> widths = configuredOutputWidths;
    if (widths.empty()) {
        assert(poNum % outputNum == 0);
        widths.assign(outputNum, poNum / outputNum);
    }
    assert(static_cast<int>(widths.size()) == outputNum);
    ll totalWidth = 0;
    for (int width: widths) {
        assert(width > 0);
        totalWidth += width;
    }
    assert(totalWidth == poNum);
    return widths;
}


static void AccumulateCombinedMetricValue(
    CombinedMetricSums& sums,
    int outputIdx,
    int width,
    bool isSign,
    const BigInt& accOut,
    const BigInt& appOut
) {
    const BigFlt norm = GetErrorMetricNormalization(width, isSign);
    assert(norm > 0);
    const BigInt diff = appOut - accOut;
    const BigInt absDiff = abs(diff);

    sums.sumAbs += BigFlt(absDiff);
    sums.sumRawSq += BigFlt(diff * diff);
    sums.sumNormAbs += BigFlt(absDiff) / norm;
    sums.sumNormSq += BigFlt(diff * diff) / (norm * norm);
    sums.sumNormBias += BigFlt(diff) / norm;
    if (diff != 0)
        ++sums.errCount;

    if (accOut != 0)
        sums.mapeSums[outputIdx] +=
            abs(BigFlt(appOut) - BigFlt(accOut)) / abs(BigFlt(accOut));
    else if (appOut != 0)
        sums.mapeSums[outputIdx] += BigFlt(1);
}


static CombinedErrorMetrics FinalizeCombinedMetricSums(
    const CombinedMetricSums& sums,
    ll nFrame,
    METR_TYPE metrType,
    int outputNum
) {
    const BigInt total = BigInt(nFrame) * BigInt(outputNum);
    CombinedErrorMetrics result;
    result.mapeVector.assign(outputNum, 0.0);
    BigFlt mapeSum(0);
    for (int o = 0; o < outputNum; ++o) {
        result.mapeVector[o] = double(sums.mapeSums[o] / BigFlt(nFrame));
        mapeSum += result.mapeVector[o];
    }

    const BigFlt rawMed = sums.sumAbs / BigFlt(total);
    const BigFlt rawMse = sums.sumRawSq / BigFlt(total);
    if (metrType == METR_TYPE::MED)
        result.metricErr = double(rawMed);
    else if (metrType == METR_TYPE::MSE)
        result.metricErr = double(rawMse);
    else if (metrType == METR_TYPE::MAPE)
        result.metricErr = double(mapeSum / BigFlt(outputNum));
    else {
        assert(0);
        result.metricErr = 0.0;
    }

    result.multiMetrics.nmed = double(sums.sumNormAbs / BigFlt(total));
    result.multiMetrics.nmse = double(sums.sumNormSq / BigFlt(total));
    result.multiMetrics.er = double(BigFlt(sums.errCount) / BigFlt(total));
    result.multiMetrics.me = double(sums.sumNormBias / BigFlt(total));
    return result;
}


static CombinedErrorMetrics CalcCombinedErrorMetricsFromSimulators(
    const Simulator & smlt0,
    const Simulator & smlt1,
    bool isSign,
    ll nFrame,
    METR_TYPE metrType,
    int outputNum,
    const vector<int> & outputWidths = {})
{
    assert(smlt0.IsPIOSame(smlt1));
    vector<int> widths = ResolveCombinedOutputWidths(
        smlt0.GetPoNum(), outputNum, outputWidths);
    CombinedMetricSums sums(outputNum);

    for (ll i = 0; i < nFrame; ++i) {
        ll lsb = 0;
        for (int o = 0; o < outputNum; ++o) {
            const int width = widths[o];
            const ll msb = lsb + width - 1;
            const BigInt accOut = smlt0.GetOutpRange(i, lsb, msb, isSign);
            const BigInt appOut = smlt1.GetOutpRange(i, lsb, msb, isSign);
            AccumulateCombinedMetricValue(
                sums, o, width, isSign, accOut, appOut);
            lsb += width;
        }
        assert(lsb == smlt0.GetPoNum());
    }
    return FinalizeCombinedMetricSums(sums, nFrame, metrType, outputNum);
}


ErrMan::ErrMan(NetMan & netMan0, NetMan & netMan1, unsigned _seed, ll n_frame, DISTR_TYPE distr_type, int output_num, string pattern_file_path):
    net0(netMan0), net1(netMan1), pSmlt0(nullptr), pSmlt1(nullptr), seed(_seed), nFrame(n_frame), distrType(distr_type), outputNum(output_num), patternFilePath(std::move(pattern_file_path)) {
    assert(IsPIOSame(net0, net1));
}

ErrMan::ErrMan(NetMan & netMan0, NetMan & netMan1, unsigned _seed, ll n_frame, DISTR_TYPE distr_type, int output_num, vector<int> output_widths, string pattern_file_path):
    net0(netMan0), net1(netMan1), pSmlt0(nullptr), pSmlt1(nullptr), seed(_seed), nFrame(n_frame), distrType(distr_type), outputNum(output_num), outputWidths(std::move(output_widths)), patternFilePath(std::move(pattern_file_path)) {
    assert(IsPIOSame(net0, net1));
}


void ErrMan::InitForStatErr() {
    if (pSmlt0 != nullptr || pSmlt1 != nullptr) {
        assert(pSmlt0 != nullptr && pSmlt1 != nullptr);
        return;
    }
    pSmlt0 = make_shared <Simulator> (net0, seed, nFrame);
    pSmlt1 = make_shared <Simulator> (net1, seed, nFrame); 
    InitSimulatorForStatErr(*pSmlt0, distrType, patternFilePath);
    InitSimulatorForStatErr(*pSmlt1, distrType, patternFilePath);
}


double ErrMan::CalcMeanErrDist(bool isSign) {
    InitForStatErr();
    return pSmlt0->GetMeanErrDist(*pSmlt1, isSign, outputNum);
}


double ErrMan::CalcMeanSquareErr(bool isSign) {
    InitForStatErr();
    return pSmlt0->GetMeanSquareErr(*pSmlt1, isSign, outputNum);
}


double ErrMan::CalcMeanAbsPercentageErr(bool isSign) {
    InitForStatErr();
    return pSmlt0->GetMeanAbsPercentageErr(*pSmlt1, isSign, outputNum);
}


vector<double> ErrMan::CalcMeanAbsPercentageErrVector(bool isSign) {
    InitForStatErr();
    return pSmlt0->GetMeanAbsPercentageErrVector(*pSmlt1, isSign, outputNum);
}


ErrorMetrics ErrMan::CalcErrorMetrics(bool isSign) {
    InitForStatErr();
    assert(pSmlt0->IsPIOSame(*pSmlt1));
    assert(pSmlt0->GetPoNum() % outputNum == 0);

    const ll width = pSmlt0->GetPoNum() / outputNum;
    const BigFlt norm = GetErrorMetricNormalization(width, isSign);
    assert(norm > 0);

    BigFlt sumAbs(0);
    BigFlt sumSq(0);
    BigFlt sumBias(0);
    BigInt errCount(0);
    const BigInt total = BigInt(nFrame) * BigInt(outputNum);

    for (ll i = 0; i < nFrame; ++i) {
        for (int o = 0; o < outputNum; ++o) {
            const ll lsb = o * width;
            const ll msb = lsb + width - 1;
            const BigInt accOut = pSmlt0->GetOutpRange(i, lsb, msb, isSign);
            const BigInt appOut = pSmlt1->GetOutpRange(i, lsb, msb, isSign);
            const BigInt diff = appOut - accOut;
            const BigInt absDiff = abs(diff);
            sumAbs += BigFlt(absDiff);
            sumSq += BigFlt(diff * diff);
            sumBias += BigFlt(diff);
            if (diff != 0)
                ++errCount;
        }
    }

    ErrorMetrics metrics;
    metrics.nmed = double(sumAbs / BigFlt(total) / norm);
    metrics.nmse = double(sumSq / BigFlt(total) / (norm * norm));
    metrics.er = double(BigFlt(errCount) / BigFlt(total));
    metrics.me = double(sumBias / BigFlt(total) / norm);
    return metrics;
}


CombinedErrorMetrics ErrMan::CalcCombinedErrorMetrics(bool isSign, METR_TYPE metrType) {
    InitForStatErr();
    return CalcCombinedErrorMetricsFromSimulators(*pSmlt0, *pSmlt1, isSign, nFrame, metrType, outputNum, outputWidths);
}

// double ErrMan::CalcSelfDefErr(bool isSign, const string & selfDefMetr) {
//     InitForStatErr();
//     return pSmlt0->GetSelfDefErr(*pSmlt1, isSign, selfDefMetr);
// }

static void Abc_NtkMiterPrepare( Abc_Ntk_t * pNtk1, Abc_Ntk_t * pNtk2, Abc_Ntk_t * pNtkMiter) {
    Abc_Obj_t * pObj, * pObjNew; ll i;
    Abc_AigConst1(pNtk1)->pCopy = Abc_AigConst1(pNtkMiter);
    Abc_AigConst1(pNtk2)->pCopy = Abc_AigConst1(pNtkMiter);

    // create new PIs and remember them in the old PIs
    Abc_NtkForEachPi(pNtk1, pObj, i)
    {
        pObjNew = Abc_NtkCreatePi(pNtkMiter);
        // remember this PI in the old PIs
        pObj->pCopy = pObjNew;
        pObj = Abc_NtkPi(pNtk2, i);  
        pObj->pCopy = pObjNew;
            // add name
        Abc_ObjAssignName( pObjNew, Abc_ObjName(pObj), NULL );
    }

        // pObjNew = Abc_NtkCreatePo(pNtkMiter);
        // Abc_ObjAssignName(pObjNew, "miter", NULL);

    Abc_NtkForEachLatch( pNtk1, pObj, i )
    {
        pObjNew = Abc_NtkDupBox( pNtkMiter, pObj, 0 );
        // add names
        Abc_ObjAssignName( pObjNew, Abc_ObjName(pObj), "_1" );
        Abc_ObjAssignName( Abc_ObjFanin0(pObjNew),  Abc_ObjName(Abc_ObjFanin0(pObj)), "_1" );
        Abc_ObjAssignName( Abc_ObjFanout0(pObjNew), Abc_ObjName(Abc_ObjFanout0(pObj)), "_1" );
    }
    Abc_NtkForEachLatch( pNtk2, pObj, i )
    {
        pObjNew = Abc_NtkDupBox( pNtkMiter, pObj, 0 );
        // add name
        Abc_ObjAssignName( pObjNew, Abc_ObjName(pObj), "_2" );
        Abc_ObjAssignName( Abc_ObjFanin0(pObjNew),  Abc_ObjName(Abc_ObjFanin0(pObj)), "_2" );
        Abc_ObjAssignName( Abc_ObjFanout0(pObjNew), Abc_ObjName(Abc_ObjFanout0(pObj)), "_2" );
    }
}


static void Abc_NtkMiterAddOne( Abc_Ntk_t * pNtk, Abc_Ntk_t * pNtkMiter ) {
    Abc_Obj_t * pNode;
    ll i;
    assert( Abc_NtkIsDfsOrdered(pNtk) );
    Abc_AigForEachAnd( pNtk, pNode, i )
        pNode->pCopy = Abc_AigAnd( (Abc_Aig_t *)pNtkMiter->pManFunc, Abc_ObjChild0Copy(pNode), Abc_ObjChild1Copy(pNode) );
}

// double CalcErrPro(NetMan& net0, NetMan& net1, bool isSign, unsigned seed, ll nFrame, METR_TYPE metrType, DISTR_TYPE distrType) {
//     ErrManPro errMan(net0, net1, isSign, seed, nFrame, metrType, distrType);
//     errMan.InitMit();
//     auto err = errMan.CalcErr();
//     return err;
// }


double CalcErr(NetMan & netMan0, NetMan & netMan1, bool isSign, unsigned seed, ll nFrame, METR_TYPE metrType, DISTR_TYPE distrType, int outputNum, const string & patternFilePath) {
    ErrMan errMan(netMan0, netMan1, seed, nFrame, distrType, outputNum, patternFilePath);
    if (metrType == METR_TYPE::MED)
        return errMan.CalcMeanErrDist(isSign);
    else if (metrType == METR_TYPE::MSE)
        return errMan.CalcMeanSquareErr(isSign);
    else if (metrType == METR_TYPE::MAPE)
        return errMan.CalcMeanAbsPercentageErr(isSign);
    // else if (metrType == METR_TYPE::SELF)
    //     return errMan.CalcSelfDefErr(isSign, selfDefMetr);
    else {
        assert(0);
        return 0;
    }
}


vector<double> CalcMapeErrVector(NetMan & netMan0, NetMan & netMan1, bool isSign, unsigned seed, ll nFrame, DISTR_TYPE distrType, int outputNum, const string & patternFilePath) {
    ErrMan errMan(netMan0, netMan1, seed, nFrame, distrType, outputNum, patternFilePath);
    return errMan.CalcMeanAbsPercentageErrVector(isSign);
}


ErrorMetrics CalcMultiErrorMetrics(NetMan & netMan0, NetMan & netMan1, bool isSign, unsigned seed, ll nFrame, DISTR_TYPE distrType, int outputNum, const string & patternFilePath) {
    ErrMan errMan(netMan0, netMan1, seed, nFrame, distrType, outputNum, patternFilePath);
    return errMan.CalcErrorMetrics(isSign);
}


CombinedErrorMetrics CalcCombinedErrorMetrics(NetMan & netMan0, NetMan & netMan1, bool isSign, unsigned seed, ll nFrame, METR_TYPE metrType, DISTR_TYPE distrType, int outputNum, const string & patternFilePath, const vector<int> & outputWidths) {
    ErrMan errMan(netMan0, netMan1, seed, nFrame, distrType, outputNum, outputWidths, patternFilePath);
    return errMan.CalcCombinedErrorMetrics(isSign, metrType);
}


CombinedErrorMetrics CalcCombinedErrorMetrics(const Simulator & accSmlt, NetMan & appNet, bool isSign, unsigned seed, ll nFrame, METR_TYPE metrType, DISTR_TYPE distrType, int outputNum, const string & patternFilePath, const vector<int> & outputWidths) {
    Simulator appSmlt(appNet, seed, nFrame);
    InitSimulatorForStatErr(appSmlt, distrType, patternFilePath);
    return CalcCombinedErrorMetricsFromSimulators(accSmlt, appSmlt, isSign, nFrame, metrType, outputNum, outputWidths);
}


// double GetMSEFromSNR(NetMan & net, bool isSign, unsigned seed, ll nFrame, DISTR_TYPE distrType, double snr) {
//     Simulator smlt(net, seed, nFrame);
//     if (distrType == DISTR_TYPE::ENUM)
//         smlt.InpEnum();
//     else if (distrType == DISTR_TYPE::UNIF)
//         smlt.InpUnifFast();
//     else if (distrType == DISTR_TYPE::MIX)
//         smlt.InpMix();
//     else
//         assert(0);
//     smlt.Sim();
//     BigInt sumAcc2 = 0;
//     for (ll i = 0; i < nFrame; ++i) {
//         BigInt accOut = smlt.GetOutpPro(i, isSign);
//         sumAcc2 += accOut * accOut;
//     }
//     return static_cast <double> (BigFlt(sumAcc2) / BigFlt(nFrame) / BigFlt(pow(BigFlt(10),  BigFlt(snr) / 10)));
// }


// multiple-thread lock
std::mutex mtx;


static long long ElapsedUs(const chrono::steady_clock::time_point& begin) {
    return chrono::duration_cast<chrono::microseconds>(
        chrono::steady_clock::now() - begin).count();
}


static void CalcSomeLACLocalERs(Simulator& appSmlt, LACMan& lacMan, boost::dynamic_bitset<ull>& IsErroneousPattern, timer::progress_display& pd, ll startIndex, ll endIndex) {
    for (ll lacId = startIndex; lacId < endIndex; ++lacId) {
        auto pLac = lacMan.GetLac(lacId);
        ll targId = pLac->GetTargId();

        // calculate $\partial n / \partial LAC$
        auto & specLac = *dynamic_pointer_cast <ResubLAC>(pLac);
        auto divIds = specLac.GetDivIds(); 
        auto sop = specLac.GetSop();
        BitVect newValue(appSmlt.GetFrameNumb(), 0);
        GetNewValue(appSmlt, divIds, sop, newValue);
        auto isChanged = (*appSmlt.GetDat(targId)) ^ newValue;
        auto estimatedDiff = isChanged & (~IsErroneousPattern);
        BigInt er = estimatedDiff.count();
        pLac->SetErrPro(er);

        // update progress
        std::unique_lock<std::mutex> lock(mtx);
        ++pd;
        lock.unlock();
    }
}


static void EstSomeLACErrorBounds(Simulator& miterSmlt, LACMan& lacMan, const IntVect& appId2MiterId, vector<vector<BitVect>>& bdPo2Nodes, timer::progress_display& pd, ll startIndex, ll endIndex) {
    int nPo = miterSmlt.GetPoNum();
    for (ll lacId = startIndex; lacId < endIndex; ++lacId) {
        auto pLac = lacMan.GetResubLac(lacId);
        // cout << "deal with LAC " << lacId << endl;
        // pLac->Print();
        int targIdInApp = pLac->GetTargId();
        int targId = appId2MiterId[targIdInApp];
        assert(targId != -1);
        // calculate $\partial n / \partial LAC$
        // auto & specLac = *dynamic_pointer_cast <ResubLAC>(pLac);
        auto divIdsInApp = pLac->GetDivIds(); 
        IntVect divIds; divIds.clear();
        for (auto divIdInApp: divIdsInApp) {
            assert(appId2MiterId[divIdInApp] != -1);
            divIds.emplace_back(appId2MiterId[divIdInApp]);
        }
        auto sop = pLac->GetSop();
        BitVect newValue(miterSmlt.GetFrameNumb(), 0);
        GetNewValue(miterSmlt, divIds, sop, newValue);
        auto isChanged = (*miterSmlt.GetDat(targId)) ^ newValue;
        // cout << miterSmlt.GetName(targId) << endl;
        // cout << "current value: " << *miterSmlt.GetDat(targId) << endl;
        // cout << "new value:" << newValue << endl;
        BigInt deltaErrorBound(0);
        for (int k = nPo - 1; k >= 0; --k) {
            deltaErrorBound <<= 1;
            // auto affectPoK = isChanged & bdPo2Nodes[k][targId];
            // the k-th miter PO is currently 0, and the LAC influences the k-th miter PO
            auto temp = (isChanged & bdPo2Nodes[k][targId]) & (~*miterSmlt.GetDat(miterSmlt.GetPoId(k)));
            deltaErrorBound += temp.count();
        }
        pLac->SetErrPro(deltaErrorBound);

        // update progress
        std::unique_lock<std::mutex> lock(mtx);
        ++pd;
        lock.unlock();
    }
}


void VECBEEMan::EstimateErrorBoundForEachLAC(Simulator& miterSmlt, LACMan& lacMan, const BigInt& upperBound, bool enableFastErrEst, const IntVect& miterId2AppId, const IntVect& appId2MiterId) {
    assert(lacType == LAC_TYPE::RESUB);
    // assert(metrType == METR_TYPE::MED);
    
    auto topoNodes = miterSmlt.TopoSort();
    NetMan& miterNet = miterSmlt;
    // miterNet.WriteBlif("./tmp/miter.blif");
    // miterNet.WriteDot("./tmp/miter.dot");
    // PrintVect(topoNodes, "\n");
    FindDisjCut(miterNet, topoNodes);
    CalcBoolDiffCut2Node(miterSmlt, topoNodes);
    CalcBoolDiffPo2NodeParallelly(miterSmlt, topoNodes);

    cout << "estimating LACs' error bounds" << endl;
    int lacNum = lacMan.GetLacNum();
    if (lacNum == 0) {
        cout << "no LACs to estimate" << endl;
        return;
    }
    int realThread = min(nThread, lacNum);
    assert(realThread > 0);
    cout << "using " << realThread << " threads" << endl;
    int chunkSize = lacNum / realThread;
    int remainder = lacNum % realThread;
    timer::progress_display pd(lacMan.GetLacNum());
    vector<thread> threads;
    ll start = 0;
    for (ll i = 0; i < realThread; ++i) {
        ll end = start + chunkSize + (i < remainder? 1: 0);
        threads.emplace_back(EstSomeLACErrorBounds, std::ref(miterSmlt), std::ref(lacMan), std::ref(appId2MiterId), std::ref(bdPo2Nodes), std::ref(pd), start, end);
        start = end;
    }
    for (auto& thread: threads)
        thread.join();

    // for (ll lacId = 0; lacId < lacMan.GetLacNum(); ++lacId) {
    //     auto pLac = lacMan.GetResubLac(lacId);
    //     pLac->Print();
    // }
}


// void VECBEEMan::EstimateErrorBoundForEachLAC(Simulator& accSmlt, Simulator& appSmlt, LACMan& lacMan, const BigInt& upperBound, bool enableFastErrEst) {
//     assert(IsPIOSame(accSmlt, appSmlt));
//     assert(lacType == LAC_TYPE::RESUB);
//     assert((nFrame & 63) == 0);
//     if (metrType == METR_TYPE::ER)
//         EstimateERBound(accSmlt, appSmlt, lacMan);
//     else if (metrType == METR_TYPE::MED) {
//         auto topoNodes = appSmlt.TopoSort();
//         NetMan& appNet = appSmlt;
//         FindDisjCut(appNet, topoNodes);
//         CalcBoolDiffCut2NodeParallelly(appSmlt, topoNodes);
//         CalcBoolDiffPo2NodeParallelly(appSmlt, topoNodes);
//         EstimateMEDBound(accSmlt, appSmlt, lacMan);
//     }
//     else
//         assert(0);
// }


// void VECBEEMan::ComputeBooleanDifferenceOfPos2Nodes(Simulator& appSmlt, AbcObjVect& topoNodes, bool enableFastErrEst) {
//     assert((nFrame & 63) == 0);
//     NetMan& appNet = appSmlt;
//      FindDisjCut(appNet, topoNodes);
//     CalcBoolDiffCut2Node(appSmlt, topoNodes); // perf: multiple threads
//     CalcBoolDiffPo2NodeParallelly(appSmlt, topoNodes);
// }


static void EstSomeLACERBounds(Simulator& appSmlt, LACMan& lacMan, boost::dynamic_bitset<ull>& isCorrectPattern, timer::progress_display& pd, ll startIndex, ll endIndex) {
    for (ll lacId = startIndex; lacId < endIndex; ++lacId) {
        auto pLac = lacMan.GetLac(lacId);
        ll targId = pLac->GetTargId();

        // calculate $\partial n / \partial LAC$
        auto & specLac = *dynamic_pointer_cast <ResubLAC>(pLac);
        auto divIds = specLac.GetDivIds(); 
        auto sop = specLac.GetSop();
        BitVect newValue(appSmlt.GetFrameNumb(), 0);
        GetNewValue(appSmlt, divIds, sop, newValue);
        auto isChanged = (*appSmlt.GetDat(targId)) ^ newValue;
        auto estimatedDiff = isChanged & isCorrectPattern;
        BigInt er = estimatedDiff.count();
        pLac->SetErrPro(er);

        // update progress
        std::unique_lock<std::mutex> lock(mtx);
        ++pd;
        lock.unlock();
    }
}


static void EstSomeLACMEDBounds(Simulator& appSmlt, LACMan& lacMan, vector<vector<BitVect>>& bdPo2Nodes, timer::progress_display& pd, ll startIndex, ll endIndex) {
    int nPo = appSmlt.GetPoNum();
    for (ll lacId = startIndex; lacId < endIndex; ++lacId) {
        auto pLac = lacMan.GetLac(lacId);
        ll targId = pLac->GetTargId();

        // calculate $\partial n / \partial LAC$
        auto & specLac = *dynamic_pointer_cast <ResubLAC>(pLac);
        auto divIds = specLac.GetDivIds(); 
        auto sop = specLac.GetSop();
        dynamic_bitset <ull> newValue(appSmlt.GetFrameNumb(), 0);
        GetNewValue(appSmlt, divIds, sop, newValue);
        auto isChanged = (*appSmlt.GetDat(targId)) ^ newValue;
        BigInt deltaMEDBound(0);
        for (int k = nPo - 1; k >= 0; --k) {
            deltaMEDBound <<= 1;
            auto affectPoK = isChanged & bdPo2Nodes[k][targId];
            deltaMEDBound += affectPoK.count();
        }
        pLac->SetErrPro(deltaMEDBound);

        // update progress
        std::unique_lock<std::mutex> lock(mtx);
        ++pd;
        lock.unlock();
    }
}


void VECBEEMan::EstimateMEDBound(Simulator& accSmlt, Simulator& appSmlt, LACMan& lacMan) {
    cout << "estimating LACs' MED bounds" << endl;
    int lacNum = lacMan.GetLacNum();
    if (lacNum == 0) {
        cout << "no LACs to estimate" << endl;
        return;
    }
    int realThread = min(nThread, lacNum);
    assert(realThread > 0);
    cout << "using " << realThread << " threads" << endl;
    ll chunkSize = lacNum / realThread;
    ll remainder = lacNum % realThread;
    timer::progress_display pd(lacMan.GetLacNum());
    vector<thread> threads;
    ll start = 0;
    for (ll i = 0; i < realThread; ++i) {
        ll end = start + chunkSize + (i < remainder? 1: 0);
        threads.emplace_back(EstSomeLACMEDBounds, std::ref(appSmlt), std::ref(lacMan), std::ref(bdPo2Nodes), std::ref(pd), start, end);
        start = end;
    }
    for (auto& thread: threads)
        thread.join();
}


void VECBEEMan::BatchErrEstPro(NetMan& accNet, NetMan& appNet, LACMan& lacMan, const BigInt& uppBound, bool enableFastErrEst, BigInt& backErrInt, bool collectCombinedMetrics) {
    assert(IsPIOSame(accNet, appNet));
    assert(lacType == LAC_TYPE::RESUB);
    const auto sharedAnalysisBegin = chrono::steady_clock::now();
    auto topoNodes = appNet.TopoSort();
    FindDisjCut(appNet, topoNodes);
    Simulator accSmlt(accNet, seed, nFrame);
    Simulator appSmlt(appNet, seed, nFrame);
    if (distrType == DISTR_TYPE::UNIF) {
        accSmlt.InpUnifFast();
        appSmlt.InpUnifFast();
    }
    else if (distrType == DISTR_TYPE::ENUM) {
        accSmlt.InpEnum();
        appSmlt.InpEnum();
    }
    else if (distrType == DISTR_TYPE::MIX) {
        accSmlt.InpMix();
        appSmlt.InpMix();
    }
    else if (distrType == DISTR_TYPE::SELF) {
        assert(!patternFilePath.empty());
        accSmlt.InpSelf(patternFilePath);
        appSmlt.InpSelf(patternFilePath);
    }
    else
        assert(0);
    accSmlt.Sim();
    appSmlt.Sim();
    // Policy patterns are deliberately disjoint from exact-validation truth
    // patterns.  Recompute the scalar base contribution on this partition;
    // reusing the caller's truth-partition value would corrupt unchanged-LAC
    // estimates and defeat the split.
    const auto policyBase = CalcCombinedErrorMetricsFromSimulators(
        accSmlt,
        appSmlt,
        isSign,
        nFrame,
        metrType,
        outputNum,
        outputWidths
    );
    BigFlt policyBaseSum = BigFlt(policyBase.metricErr)
        * BigFlt(nFrame) * BigFlt(outputNum);
    if (metrType == METR_TYPE::MAPE)
        policyBaseSum *= MAPE_ERR_SCALE;
    backErrInt = BigInt(policyBaseSum);
    if (transitionGMMConfig.Enabled() && transitionGMMPrototypes == nullptr) {
        transitionGMMPrototypes = make_shared<TransitionGMMPrototypeSet>();
        string error;
        if (!transitionGMMPrototypes->Load(
                transitionGMMConfig.prototypePath,
                error
            )) {
            cerr << "ERROR: " << error << endl;
            exit(1);
        }
    }
    CalcBoolDiffCut2NodeParallelly(appSmlt, topoNodes);
    CalcBoolDiffPo2NodeParallelly(appSmlt, topoNodes);
    transitionGMMConfig.sharedAnalysisTimeUs =
        chrono::duration_cast<chrono::microseconds>(
            chrono::steady_clock::now() - sharedAnalysisBegin
        ).count();
    if (metrType == METR_TYPE::MED || metrType == METR_TYPE::MSE || metrType == METR_TYPE::MAPE)
        CalcLACNonERErrsNew(
            accSmlt,
            appSmlt,
            lacMan,
            uppBound,
            backErrInt,
            collectCombinedMetrics
        );
}


void VECBEEMan::FindDisjCut(NetMan& net, AbcObjVect& topoNodes) {
    cout << "finding disjoint cuts" << endl;
    assert(disjCuts.empty());
    assert(cutNtks.empty());
    assert(topoNodes.size());
    assert(topoNodes[0]->pNtk == net.GetNet());

    // init
    cutNtks.resize(net.GetIdMaxPlus1());
    disjCuts.resize(net.GetIdMaxPlus1());
    poMarks.resize(net.GetIdMaxPlus1(), dynamic_bitset <ull>(net.GetPoNum(), 0));
    for (ll i = 0; i < net.GetIdMaxPlus1(); ++i) {
        if (net.GetObj(i) == nullptr)
            continue;
        poMarks[i].reset();
    }

    // update topo ids
    topoIds.resize(net.GetIdMaxPlus1());
    for (ll i = 0; i < topoNodes.size(); ++i)
        topoIds[topoNodes[i]->Id] = i;
    ll topoId = -1;
    for (ll i = 0; i < net.GetPiNum(); ++i)
        topoIds[net.GetPiId(i)] = topoId--;
    topoId = topoNodes.size();
    for (ll i = 0; i < net.GetPoNum(); ++i)
        topoIds[net.GetPoId(i)] = topoId++;

    // determine the POs that each node will affect
    for (ll i = 0; i < net.GetPoNum(); ++i)
        poMarks[net.GetPoId(i)].set(i);
    for (auto it = topoNodes.rbegin(); it != topoNodes.rend(); ++it) {
        auto pObj = *it;
        if (pObj == nullptr)
            continue;
        ll i = net.GetId(pObj);
        for (ll j = 0; j < net.GetFanoutNum(pObj); ++j)
            poMarks[i] |= poMarks[net.GetFanoutId(pObj, j)];
    }

    // collect disjoint cuts and the corresponding cut networks
    timer::progress_display pd(net.GetIdMaxPlus1());
    for (ll i = 0; i < net.GetIdMaxPlus1(); ++i) {
        auto pObj = net.GetObj(i);
        if (!net.IsNode(pObj)) {
            ++pd;
            continue;
        }
        // cout << "finding " << pObj << endl;
        Abc_NtkIncrementTravId(net.GetNet());
        FindDisjCutOfNode(pObj, disjCuts[i]);
        for (const auto & node: topoNodes) {
            if (Abc_NodeIsTravIdCurrent(node))
                cutNtks[i].emplace_back(node);
        }
        for (ll j = 0; j < net.GetPoNum(); ++j) {
            auto pPo = net.GetPo(j);
            if (Abc_NodeIsTravIdCurrent(pPo))
                cutNtks[i].emplace_back(pPo);
        }
        ++pd;
    }
}

void VECBEEMan::FindDisjCutOfNode(Abc_Obj_t* pObj, AbcObjList& disjCut) {
    disjCut.clear();
    ExpandCut(pObj, disjCut);
    Abc_Obj_t * pObjExpd = nullptr;
    while ((pObjExpd = ExpandWhich(disjCut)) != nullptr) {
        ExpandCut(pObjExpd, disjCut);
    }
}


void VECBEEMan::ExpandCut(Abc_Obj_t* pObj, AbcObjList& disjCut) {
    abc::Abc_Obj_t * pFanout = nullptr;
    ll i = 0;
    Abc_ObjForEachFanout(pObj, pFanout, i) {
        if (!abc::Abc_NodeIsTravIdCurrent(pFanout)) {
            if (abc::Abc_ObjFanoutNum(pFanout) || abc::Abc_ObjIsPo(pFanout)) {
                abc::Abc_NodeSetTravIdCurrent(pFanout);
                disjCut.emplace_back(pFanout);
            }
        }
    } 
}


Abc_Obj_t* VECBEEMan::ExpandWhich(AbcObjList& disjCut) {
    for (auto ppAbcObj1 = disjCut.begin(); ppAbcObj1 != disjCut.end(); ++ppAbcObj1) {
        auto ppAbcObj2 = ppAbcObj1;
        for (++ppAbcObj2; ppAbcObj2 != disjCut.end(); ++ppAbcObj2) {
            assert(poMarks[(*ppAbcObj1)->Id].size() == poMarks[(*ppAbcObj2)->Id].size());
            assert((*ppAbcObj1)->Id != (*ppAbcObj2)->Id);
            assert(topoIds[(*ppAbcObj1)->Id] != topoIds[(*ppAbcObj2)->Id]);
            auto isJoint = poMarks[(*ppAbcObj1)->Id] & poMarks[(*ppAbcObj2)->Id];
            if (isJoint.any()) {
                abc::Abc_Obj_t * pRet = nullptr;
                if (topoIds[(*ppAbcObj1)->Id] < topoIds[(*ppAbcObj2)->Id]) {
                    pRet = *ppAbcObj1;
                    disjCut.erase(ppAbcObj1);
                }
                else {
                    pRet = *ppAbcObj2;
                    disjCut.erase(ppAbcObj2);
                }
                return pRet;
            }
        }
    }
    return nullptr;
}


void VECBEEMan::CalcBoolDiffCut2Node(Simulator& appSmlt, AbcObjVect& topoNodes) {
    cout << "calculating boolean difference of cuts with regard to nodes" << endl;
    assert(topoNodes.size());
    assert(topoNodes[0]->pNtk == appSmlt.GetNet());
    timer::progress_display pd(topoNodes.size());
    bdCut2Nodes.resize(appSmlt.GetIdMaxPlus1());
    for (const auto & pObj: topoNodes) {
        ll i = appSmlt.GetId(pObj);
        if (!appSmlt.IsNode(pObj) || appSmlt.IsConst(pObj)) {
            ++pd;
            continue;
        }
        appSmlt.CalcLocBoolDiff(pObj, disjCuts[i], cutNtks[i], bdCut2Nodes[i]);
        ++pd;
    }
}


static void UpdSopForBoolAndPartDiff(Simulator& appSmlt, Abc_Obj_t* pObj, char* pSop, unordered_map<int, BitVect>& tempDat) {
    int nVars = abc::Abc_SopGetVarNum(pSop);
    int nFrame = appSmlt.GetFrameNumb();
    BitVect product(nFrame, 0);
    for (char * pCube = pSop; *pCube; pCube += nVars + 3) {
        bool isFirst = true;
        for (ll i = 0; pCube[i] != ' '; i++) {
            Abc_Obj_t * pFanin = Abc_ObjFanin(pObj, i);
            BitVect &datFi = tempDat.count(pFanin->Id)? tempDat[pFanin->Id]: *appSmlt.GetDat(pFanin->Id);
            switch (pCube[i]) {
                case '-':
                    continue;
                    break;
                case '0':
                    if (isFirst) {
                        isFirst = false;
                        product = ~datFi;
                    }
                    else
                        product &= ~datFi;
                    break;
                case '1':
                    if (isFirst) {
                        isFirst = false;
                        product = datFi;
                    }
                    else
                        product &= datFi;
                    break;
                default:
                    assert(0);
            }
        }
        if (isFirst) {
            isFirst = false;
            product.set();
        }
        assert(!isFirst);
        if (pCube == pSop)
            tempDat[pObj->Id] = product;
        else
            tempDat[pObj->Id] |= product;
    }
    // complement
    if (abc::Abc_SopIsComplement(pSop))
        tempDat[pObj->Id].flip();
}


static void CalcSomeLocBoolDiff(AbcObjVect& targetNodes, Simulator& appSmlt, vector<AbcObjList>& disjCuts, vector<AbcObjVect>& cutNtks, vector<vector<BitVect>>& bdCut2Nodes, timer::progress_display& pd, int start, int end) {
    assert(appSmlt.GetNetType() == NET_TYPE::SOP);
    for (int index = start; index < end; ++index) {
        auto pTarget = targetNodes[index];
        auto& cutNtk = cutNtks[pTarget->Id];
        auto& bdCut2Node = bdCut2Nodes[pTarget->Id];
        auto& disjCut = disjCuts[pTarget->Id];
        assert(pTarget->pNtk == appSmlt.GetNet());
        unordered_map<int, BitVect> tempDat;
        tempDat.clear();
        // flip the node
        tempDat[pTarget->Id] = ~(*appSmlt.GetDat(pTarget));
        // simulate
        for (auto& pInner: cutNtk) {
            assert(!Abc_ObjIsPi(pInner));
            assert(!Abc_NodeIsConst(pInner));
            if (Abc_ObjIsPo(pInner)) {
                assert(!abc::Abc_ObjIsComplement(pInner));
                Abc_Obj_t* pDriver = abc::Abc_ObjFanin0(pInner);
                if (tempDat.count(pDriver->Id))
                    tempDat[pInner->Id] = tempDat[pDriver->Id];
                else
                    tempDat[pInner->Id] = *appSmlt.GetDat(pDriver);
            }
            else
                UpdSopForBoolAndPartDiff(appSmlt, pInner, static_cast<char *>(pInner->pData), tempDat);
        }
        // get boolean difference from the node to its disjoint cuts
        bdCut2Node.resize(disjCut.size());
        int i = 0;
        for (auto pCut: disjCut) {
            bdCut2Node[i] = *appSmlt.GetDat(pCut) ^ tempDat[pCut->Id];
            ++i;
        }
        std::unique_lock<std::mutex> lock(mtx);
        ++pd;
        lock.unlock();
    }
}


void VECBEEMan::CalcBoolDiffCut2NodeParallelly(Simulator& appSmlt, AbcObjVect& topoNodes) {
    cout << "calculating boolean difference of cuts with regard to nodes" << endl;
    assert(topoNodes.size());
    assert(topoNodes[0]->pNtk == appSmlt.GetNet());

    // collect target nodes
    AbcObjVect targetNodes;
    targetNodes.reserve(topoNodes.size());
    for (const auto & pObj: topoNodes) {
        if (appSmlt.IsNode(pObj) && !appSmlt.IsConst(pObj))
            targetNodes.emplace_back(pObj);
    }

    // compute multiple thread parameters
    int targetNodeNum = targetNodes.size();
    int realThread = min(targetNodeNum, nThread);
    assert(realThread > 0);
    cout << "real thread number: " << realThread << endl;
    int chunkSize = targetNodeNum / realThread;
    int remainder = targetNodeNum % realThread;

    // multi-thread calculation
    bdCut2Nodes.resize(appSmlt.GetIdMaxPlus1());
    timer::progress_display pd(targetNodeNum);
    vector<thread> threads;
    int start = 0;
    for (int i = 0; i < realThread; ++i) {
        int end = start + chunkSize + (i < remainder? 1: 0);
        threads.emplace_back(CalcSomeLocBoolDiff, std::ref(targetNodes), std::ref(appSmlt), std::ref(disjCuts), std::ref(cutNtks), std::ref(bdCut2Nodes), std::ref(pd), start, end);
        start = end;
    }
    for (auto& thread: threads)
        thread.join();
}


void VECBEEMan::CalcBoolDiffPo2Node(Simulator& appSmlt, AbcObjVect& topoNodes) {
    cout << "calculating boolean difference of POs with regard to nodes" << endl;
    assert(topoNodes.size());
    assert(topoNodes[0]->pNtk == appSmlt.GetNet());
    ll nPo = appSmlt.GetPoNum();
    bdPo2Nodes.resize(nPo);
    // timer::progress_display pd(nPo);
    for (ll o = 0; o < nPo; ++o) {
        // init boolean difference
        auto & bdPo2Node = bdPo2Nodes[o];
        bdPo2Node.resize(appSmlt.GetIdMaxPlus1(), dynamic_bitset <ull> (nFrame, 0));
        // for each PO, update boolean difference
        for (ll n = 0; n < appSmlt.GetPoNum(); ++n) {
            auto pNodeN = appSmlt.GetPo(n);
            auto nId = appSmlt.GetId(pNodeN);
            if (n == o)
                bdPo2Node[nId].set(); 
            else
                bdPo2Node[nId].reset(); 
        }
        // for each node, update boolean difference
        for (auto it = topoNodes.rbegin(); it != topoNodes.rend(); ++it) {
            auto pNodeN = *it;
            if (!appSmlt.IsNode(pNodeN))
                continue;
            ll n = appSmlt.GetId(pNodeN);
            bdPo2Node[n].reset();
            ll i = 0;
            for (auto pCut: disjCuts[n]) {
                bdPo2Node[n] |= bdPo2Node[pCut->Id] & bdCut2Nodes[n][i];
                ++i;
            } 
        }
        // ++pd;
    }
}


static void CalcSomeBoolDiffPo2Node(Simulator & appSmlt, std::vector < std::vector < boost::dynamic_bitset <ull> > > & bdPo2Nodes, vector <Abc_Obj_t *> & topoNodes, std::vector < std::list <abc::Abc_Obj_t *> > & disjCuts, std::vector < std::vector < boost::dynamic_bitset <ull> > > & bdCut2Nodes, timer::progress_display & pd, ll start, ll end) {
    ll nFrame = appSmlt.GetFrameNumb();
    for (ll o = start; o < end; ++o) {
        auto & bdPo2Node = bdPo2Nodes[o];
        // init boolean difference
        bdPo2Node.resize(appSmlt.GetIdMaxPlus1(), dynamic_bitset <ull> (nFrame, 0));
        // for each PO, update boolean difference
        for (ll n = 0; n < appSmlt.GetPoNum(); ++n) {
            auto pNodeN = appSmlt.GetPo(n);
            auto nId = appSmlt.GetId(pNodeN);
            if (n == o)
                bdPo2Node[nId].set(); 
            else
                bdPo2Node[nId].reset(); 
        }
        // for each node, update boolean difference
        for (auto it = topoNodes.rbegin(); it != topoNodes.rend(); ++it) {
            auto pNodeN = *it;
            if (!appSmlt.IsNode(pNodeN))
                continue;
            ll n = appSmlt.GetId(pNodeN);
            bdPo2Node[n].reset();
            ll i = 0;
            assert(disjCuts[n].size() == bdCut2Nodes[n].size());
            for (auto pCut: disjCuts[n]) {
                bdPo2Node[n] |= bdPo2Node[pCut->Id] & bdCut2Nodes[n][i];
                ++i;
            } 
        }
        std::unique_lock<std::mutex> lock(mtx);
        ++pd;
        lock.unlock();
    }
}


void VECBEEMan::CalcBoolDiffPo2NodeParallelly(Simulator& appSmlt, AbcObjVect& topoNodes) {
    cout << "calculating boolean difference of POs with regard to nodes" << endl;
    assert(topoNodes.size());
    assert(topoNodes[0]->pNtk == appSmlt.GetNet());
    int nPo = appSmlt.GetPoNum();
    bdPo2Nodes.resize(nPo);

    int realThread = min(nPo, nThread);
    assert(realThread > 0);
    cout << "real thread number: " << realThread << endl;
    ll chunkSize = nPo / realThread;
    ll remainder = nPo % realThread;

    timer::progress_display pd(nPo);
    vector<thread> threads;
    ll start = 0;
    for (ll i = 0; i < realThread; ++i) {
        ll end = start + chunkSize + (i < remainder? 1: 0);
        threads.emplace_back(CalcSomeBoolDiffPo2Node, std::ref(appSmlt), std::ref(bdPo2Nodes), std::ref(topoNodes), std::ref(disjCuts), std::ref(bdCut2Nodes), std::ref(pd), start, end);
        start = end;
    }
    for (auto& thread: threads)
        thread.join();
}


static void GetNewValueForBlock(Simulator & smlt, const std::vector <ll> & faninIds, const std::string & sop, ull & value, ll iBlock) {
    if (sop == " 0\n") {
        value = 0;
        return;
    }
    if (sop == " 1\n") {
        value = numeric_limits <ull>::max();
        return;
    }
    char * pSop = const_cast <char *> (sop.c_str());
    ll nVars = Abc_SopGetVarNum(pSop);
    assert(nVars == faninIds.size());

    ull product = 0;
    for (char * pCube = pSop; *pCube; pCube += nVars + 3) {
        bool isFirst = true;
        for (ll i = 0; pCube[i] != ' '; i++) {
            ll faninId = faninIds[i];
            switch (pCube[i]) {
                case '-':
                    continue;
                    break;
                case '0':
                    if (isFirst) {
                        isFirst = false;
                        product = ~GetBlockFromDynBitset(*smlt.GetDat(faninId), iBlock);
                    }
                    else
                        product &= ~GetBlockFromDynBitset(*smlt.GetDat(faninId), iBlock);
                    break;
                case '1':
                    if (isFirst) {
                        isFirst = false;
                        product = GetBlockFromDynBitset(*smlt.GetDat(faninId), iBlock);
                    }
                    else
                        product &= GetBlockFromDynBitset(*smlt.GetDat(faninId), iBlock);
                    break;
                default:
                    assert(0);
            }
        }
        if (isFirst) {
            isFirst = false;
            // product.set();
            product = numeric_limits <ull>::max();
        }
        assert(!isFirst);
        if (pCube == pSop)
            value = product;
        else
            value |= product;
    }

    // complement
    if (abc::Abc_SopIsComplement(pSop))
        // value.flip();
        value = ~value;
}


static BigInt GetValue(const vector<BitVect> & dat, int iPatt, int lsb, int msb, bool isSign) {
    int shift = msb - lsb;
    assert(msb < static_cast<int>(dat.size()));
    BigInt ret(0);
    for (ll k = msb; k >= lsb; --k) {
        ret <<= 1;
        if (dat[k][iPatt])
            ++ret;
    }
    if (isSign && ret >= (BigInt(1) << shift))
        ret = -((BigInt(1) << (shift + 1)) - ret);
    return ret;
}


static BigFlt CalcMetricContribution(const BigInt & accValue, const BigInt & appValue, METR_TYPE metrType) {
    if (metrType == METR_TYPE::MED)
        return abs(BigFlt(appValue - accValue));
    if (metrType == METR_TYPE::MSE) {
        BigInt diff = appValue - accValue;
        return BigFlt(diff * diff);
    }
    if (metrType == METR_TYPE::MAPE) {
        if (accValue == 0 && appValue == 0)
            return BigFlt(0);
        if (accValue != 0)
            return abs(BigFlt(appValue) - BigFlt(accValue)) / abs(BigFlt(accValue));
        return abs(BigFlt(1));
    }
    assert(0);
    return 0;
}


static CombinedErrorMetrics CalcCombinedErrorMetricsFromTrace(
    const Simulator& accSmlt,
    const vector<BitVect>& candidateOutputs,
    bool isSign,
    int nFrame,
    METR_TYPE metrType,
    int outputNum,
    const vector<int>& configuredOutputWidths
) {
    assert(static_cast<int>(candidateOutputs.size()) == accSmlt.GetPoNum());
    vector<int> outputWidths = ResolveCombinedOutputWidths(
        accSmlt.GetPoNum(), outputNum, configuredOutputWidths);
    vector<int> outputOffsets(outputNum, 0);
    for (int outputIdx = 1; outputIdx < outputNum; ++outputIdx)
        outputOffsets[outputIdx] = outputOffsets[outputIdx - 1]
            + outputWidths[outputIdx - 1];

    CombinedMetricSums sums(outputNum);
    for (int iPatt = 0; iPatt < nFrame; ++iPatt) {
        for (int outputIdx = 0; outputIdx < outputNum; ++outputIdx) {
            int lsb = outputOffsets[outputIdx];
            int msb = lsb + outputWidths[outputIdx] - 1;
            const BigInt accValue = accSmlt.GetOutpRange(
                iPatt, lsb, msb, isSign);
            const BigInt candidateValue = GetValue(
                candidateOutputs, iPatt, lsb, msb, isSign);
            AccumulateCombinedMetricValue(
                sums, outputIdx, outputWidths[outputIdx], isSign,
                accValue, candidateValue);
        }
    }
    return FinalizeCombinedMetricSums(
        sums, nFrame, metrType, outputNum);
}


static void CalcSomeLACNonERErrsNew(
    Simulator& accSmlt,
    Simulator& appSmlt,
    LACMan& lacMan,
    vector<vector<BitVect>>& bdPo2Nodes,
    int outputNum,
    const vector<int>& configuredOutputWidths,
    bool isSign,
    BigFlt& runMin,
    timer::progress_display& pd,
    std::atomic<int>& nextLacId,
    int lacNum,
    METR_TYPE metrType,
    BigFlt& backErrSum,
    const TransitionGMMPrototypeSet* transitionPrototypes,
    const TransitionGMMExportConfig* transitionConfig,
    vector<vector<TransitionGMMRecord>>* transitionRecords,
    bool collectCombinedMetrics,
    vector<CombinedErrorMetrics>* combinedMetricsRecords,
    const VECBEEEarlyExitConfig& earlyExitConfig,
    VECBEEFeatureProfile* featureProfile
) {
    int nPo = appSmlt.GetPoNum();
    int nFrame = appSmlt.GetFrameNumb();
    vector<int> outputWidths = configuredOutputWidths;
    if (outputWidths.empty())
        outputWidths.assign(outputNum, nPo / outputNum);
    vector<int> outputOffsets(outputNum, 0);
    for (int outputIdx = 1; outputIdx < outputNum; ++outputIdx)
        outputOffsets[outputIdx] = outputOffsets[outputIdx - 1]
            + outputWidths[outputIdx - 1];
    const bool exportTransition = transitionPrototypes != nullptr;
    VECBEEFeatureProfile localProfile;

    while (true) {
        const int lacId = nextLacId.fetch_add(1);
        if (lacId >= lacNum)
            break;
        ++localProfile.candidatesProcessed;
        localProfile.patternsPossible += nFrame;
        auto pLac = lacMan.GetResubLac(lacId);
        int targId = pLac->GetTargId();

        const auto newValueBegin = chrono::steady_clock::now();
        BitVect newValue(nFrame, 0);
        GetNewValue(appSmlt, pLac->GetDivIds(), pLac->GetSop(), newValue);
        const long long newValueTimeUs = ElapsedUs(newValueBegin);
        localProfile.newValueTimeUs += newValueTimeUs;

        const auto traceBuildBegin = chrono::steady_clock::now();
        auto isChanged = (*appSmlt.GetDat(targId)) ^ newValue;

        vector<BitVect> tempOutps;
        if (!isChanged.none() || exportTransition || collectCombinedMetrics) {
            tempOutps.resize(nPo);
            for (int j = 0; j < nPo; ++j) {
                auto poId = appSmlt.GetPoId(j);
                tempOutps[j] = *appSmlt.GetDat(poId)
                    ^ (isChanged & bdPo2Nodes[j][targId]);
            }
        }
        const long long traceBuildTimeUs = ElapsedUs(traceBuildBegin);
        localProfile.traceBuildTimeUs += traceBuildTimeUs;
        const long long traceTimeUs = newValueTimeUs + traceBuildTimeUs;

        const auto scalarMetricBegin = chrono::steady_clock::now();
        BigFlt ser = 0;
        int scannedPatterns = 0;
        bool legacyEarlyExit = false;
        if (collectCombinedMetrics) {
            CombinedErrorMetrics combinedMetrics =
                CalcCombinedErrorMetricsFromTrace(
                    accSmlt,
                    tempOutps,
                    isSign,
                    nFrame,
                    metrType,
                    outputNum,
                    outputWidths
                );
            combinedMetricsRecords->at(lacId) = combinedMetrics;
            ser = BigFlt(combinedMetrics.metricErr) *
                BigFlt(nFrame) * BigFlt(outputNum);
            scannedPatterns = nFrame;
        }
        else if (isChanged.none())
            ser = backErrSum;
        else {
            BigFlt localRunMin = 0;
            const bool useLegacyEarlyExit =
                earlyExitConfig.UsesLegacyScalar();
            if (useLegacyEarlyExit) {
                std::unique_lock<std::mutex> lock(mtx);
                localRunMin = runMin;
            }
            for (int iPatt = 0; iPatt < nFrame; ++iPatt) {
                for (int outputIdx = 0; outputIdx < outputNum; ++outputIdx) {
                    int lsb = outputOffsets[outputIdx];
                    int msb = lsb + outputWidths[outputIdx] - 1;
                    auto accValue = accSmlt.GetOutpRange(iPatt, lsb, msb, isSign);
                    auto appValue = GetValue(tempOutps, iPatt, lsb, msb, isSign);
                    ser += CalcMetricContribution(accValue, appValue, metrType);
                }
                scannedPatterns = iPatt + 1;
                if (useLegacyEarlyExit && ser > localRunMin) {
                    legacyEarlyExit = true;
                    break;
                }
            }
        }
        localProfile.scalarMetricTimeUs += ElapsedUs(scalarMetricBegin);
        localProfile.patternsVisited += scannedPatterns;
        if (legacyEarlyExit) {
            ++localProfile.earlyExitCandidates;
            ++localProfile.legacyEarlyExitCandidates;
        }

        std::unique_lock<std::mutex> lock(mtx);
        if (earlyExitConfig.UsesLegacyScalar())
            runMin = min(runMin, ser);
        ++pd;
        lock.unlock();

        BigInt serInt = (metrType == METR_TYPE::MAPE)
            ? BigInt(ser * MAPE_ERR_SCALE)
            : BigInt(ser);
        pLac->SetErrPro(serInt);

        if (exportTransition) {
            auto& candidateRecords = transitionRecords->at(lacId);
            candidateRecords.reserve(outputNum);
            for (int outputIdx = 0; outputIdx < outputNum; ++outputIdx) {
                const int width = outputWidths[outputIdx];
                const int lsb = outputOffsets[outputIdx];
                const int msb = lsb + width - 1;
                const BigFlt normalization = BigFlt(
                    (BigInt(1) << width) - 1
                );
                TransitionGMMSketch sketch(*transitionPrototypes);
                vector<string> exactTrace;
                vector<string> currentTrace;
                vector<string> candidateTrace;
                BigIntVect packedCandidateTrace;
                if (transitionConfig->includeRawTrace) {
                    exactTrace.reserve(nFrame);
                    currentTrace.reserve(nFrame);
                    candidateTrace.reserve(nFrame);
                }
                if (!transitionConfig->packedTraceDirectory.empty())
                    packedCandidateTrace.reserve(nFrame);
                const auto sketchBegin = chrono::steady_clock::now();
                for (int iPatt = 0; iPatt < nFrame; ++iPatt) {
                    const BigInt exactValue = accSmlt.GetOutpRange(
                        iPatt, lsb, msb, isSign
                    );
                    const BigInt currentValue = appSmlt.GetOutpRange(
                        iPatt, lsb, msb, isSign
                    );
                    const BigInt candidateValue = GetValue(
                        tempOutps, iPatt, lsb, msb, isSign
                    );
                    const double currentError = double(
                        BigFlt(currentValue - exactValue) / normalization
                    );
                    const double candidateError = double(
                        BigFlt(candidateValue - exactValue) / normalization
                    );
                    vector<double> value;
                    if (transitionPrototypes->dimension == 3) {
                        value = {
                            double(BigFlt(exactValue) / normalization),
                            currentError,
                            candidateError,
                        };
                    }
                    else
                        value = {currentError, candidateError};
                    sketch.Add(value);
                    if (transitionConfig->includeRawTrace) {
                        exactTrace.emplace_back(exactValue.convert_to<string>());
                        currentTrace.emplace_back(currentValue.convert_to<string>());
                        candidateTrace.emplace_back(candidateValue.convert_to<string>());
                    }
                    if (!transitionConfig->packedTraceDirectory.empty())
                        packedCandidateTrace.emplace_back(candidateValue);
                }
                auto record = sketch.Finalize();
                const long long sketchTimeUs = ElapsedUs(sketchBegin);
                localProfile.gmmSketchTimeUs += sketchTimeUs;

                const auto recordExportBegin = chrono::steady_clock::now();
                record.runId = transitionConfig->runId;
                record.graphHash = transitionConfig->graphHash;
                record.baseStateHash = transitionConfig->baseStateHash;
                record.candidateHash = StableTransitionCandidateHash(
                    pLac->GetReprStr()
                );
                record.patternHash = transitionConfig->patternHash;
                record.prototypeHash = transitionPrototypes->sha256;
                record.targetRegion = transitionConfig->targetRegion;
                record.boundaryNode = transitionConfig->boundaryNodes.at(outputIdx);
                record.candidateId = lacId;
                record.targetNodeId = targId;
                record.outputIndex = outputIdx;
                record.outputWidth = width;
                record.outputSigned = isSign;
                BigInt scalarScale = BigInt(nFrame) * BigInt(outputNum);
                if (metrType == METR_TYPE::MAPE)
                    scalarScale *= BigInt(MAPE_ERR_SCALE);
                record.vecbeeScalarError = double(
                    BigFlt(serInt) / BigFlt(scalarScale)
                );
                record.traceTimeUs = traceTimeUs;
                record.sharedAnalysisTimeUs =
                    transitionConfig->sharedAnalysisTimeUs;
                record.sketchTimeUs = sketchTimeUs;
                if (!transitionConfig->packedTraceDirectory.empty()) {
                    const auto packedBegin = chrono::steady_clock::now();
                    const string candidateHashBare = record.candidateHash.substr(7);
                    const filesystem::path packedPath = filesystem::path(
                        transitionConfig->packedTraceDirectory
                    ) / candidateHashBare / ("output_" + to_string(outputIdx) + ".hvt");
                    string packedError;
                    if (!WritePackedTransitionTrace(
                            packedPath.string(), width, isSign,
                            packedCandidateTrace, record.packedCandidateTraceHash,
                            record.packedCandidateTraceBytesPerValue,
                            packedError)) {
                        cerr << "ERROR: " << packedError << endl;
                        exit(1);
                    }
                    record.packedCandidateTracePath = filesystem::absolute(
                        packedPath).lexically_normal().string();
                    record.packedTraceTimeUs =
                        chrono::duration_cast<chrono::microseconds>(
                            chrono::steady_clock::now() - packedBegin).count();
                }
                record.exactTrace = std::move(exactTrace);
                record.currentTrace = std::move(currentTrace);
                record.candidateTrace = std::move(candidateTrace);
                candidateRecords.emplace_back(std::move(record));
                localProfile.recordExportTimeUs += ElapsedUs(recordExportBegin);
            }
        }
    }
    if (featureProfile != nullptr) {
        std::unique_lock<std::mutex> lock(mtx);
        featureProfile->Add(localProfile);
    }
}


void VECBEEMan::CalcLACNonERErrsNew(Simulator& accSmlt, Simulator& appSmlt, LACMan& lacMan, const BigInt& uppBound, BigInt& backErrInt, bool collectCombinedMetrics) {
    cout << "calculating LAC errors" << endl;

    assert(IsPIOSame(accSmlt, appSmlt));
    assert(nFrame > 0);
    assert(uppBound >= 0);
    assert(lacType == LAC_TYPE::RESUB);
    assert(outputNum >= 1);
    if (outputWidths.empty())
        assert(appSmlt.GetPoNum() % outputNum == 0);
    else {
        assert(static_cast<int>(outputWidths.size()) == outputNum);
        assert(accumulate(outputWidths.begin(), outputWidths.end(), 0)
            == appSmlt.GetPoNum());
    }

    int lacNum = lacMan.GetLacNum();
    if (lacNum == 0) {
        cout << "no LACs to evaluate" << endl;
        return;
    }
    int realThread = min(nThread, lacNum);
    assert(realThread > 0);
    cout << "real thread number: " << realThread << endl;

    timer::progress_display pd(lacMan.GetLacNum());
    BigFlt runMin = BigFlt(uppBound + 1);
    vector<thread> threads;
    featureProfile = {};
    transitionGMMRecords.clear();
    if (transitionGMMPrototypes != nullptr)
        transitionGMMRecords.resize(lacNum);
    lacCombinedMetrics.clear();
    if (collectCombinedMetrics)
        lacCombinedMetrics.resize(lacNum);
    BigFlt backErrSum = BigFlt(backErrInt);
    if (metrType == METR_TYPE::MAPE)
        backErrSum /= MAPE_ERR_SCALE;
    std::atomic<int> nextLacId(0);
    for (int i = 0; i < realThread; ++i) {
        threads.emplace_back(
            CalcSomeLACNonERErrsNew,
            std::ref(accSmlt),
            std::ref(appSmlt),
            std::ref(lacMan),
            std::ref(bdPo2Nodes),
            outputNum,
            std::cref(outputWidths),
            isSign,
            std::ref(runMin),
            std::ref(pd),
            std::ref(nextLacId),
            lacNum,
            metrType,
            std::ref(backErrSum),
            transitionGMMPrototypes.get(),
            transitionGMMPrototypes == nullptr? nullptr: &transitionGMMConfig,
            transitionGMMPrototypes == nullptr? nullptr: &transitionGMMRecords,
            collectCombinedMetrics,
            collectCombinedMetrics? &lacCombinedMetrics: nullptr,
            std::cref(earlyExitConfig),
            &featureProfile
        );
    }
    for (auto& thread: threads)
        thread.join();
    if (transitionGMMPrototypes != nullptr) {
        const auto writeBegin = chrono::steady_clock::now();
        string error;
        if (!WriteTransitionGMMJsonl(
                transitionGMMConfig.outputPath,
                transitionGMMRecords,
                true,
                error
            )) {
            cerr << "ERROR: " << error << endl;
            exit(1);
        }
        const long long jsonWriteUs = ElapsedUs(writeBegin);
        featureProfile.jsonWriteTimeUs += jsonWriteUs;
        featureProfile.recordExportTimeUs += jsonWriteUs;
    }
}
