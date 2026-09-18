#include "signed_lac_scorer.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#ifdef HALS_ENABLE_TORCH
#include <ATen/Parallel.h>
#include <torch/script.h>
#endif


namespace {

std::string HashFile(const std::string& path, std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input.good()) {
        error = "cannot open artifact for hashing: " + path;
        return "";
    }
    std::string contents(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>()
    );
    return StableTransitionCandidateHash(contents);
}

std::vector<double> FeatureVector(const TransitionGMMRecord& record) {
    std::vector<double> result;
    result.emplace_back(record.zeroProbability);
    for (const auto& component: record.mixture) {
        result.emplace_back(component.weight);
        result.insert(result.end(), component.mean.begin(), component.mean.end());
        result.insert(
            result.end(), component.logVariance.begin(),
            component.logVariance.end());
        result.insert(
            result.end(), component.correlation.begin(),
            component.correlation.end());
    }
    return result;
}

template<typename T>
bool ReadValues(std::istream& input, size_t count, std::vector<T>& values) {
    values.resize(count);
    for (size_t index = 0; index < count; ++index)
        if (!(input >> values[index]))
            return false;
    return true;
}

}  // namespace


struct SignedLACScorer::Impl {
    SignedLACScorerConfig config;
    bool ready = false;
    int nodeCount = 0;
    int staticDimension = 0;
    int edgeCount = 0;
    int edgeDimension = 0;
    int levelCount = 0;
    std::vector<float> staticFeatures;
    std::vector<long long> edgeSources;
    std::vector<long long> edgeTargets;
    std::vector<float> edgeFeatures;
    std::vector<long long> edgeTargetPosition;
    std::vector<long long> levelNodes;
    std::vector<long long> levelNodePointer;
    std::vector<long long> levelEdgePointer;
    std::vector<float> outputMask;
    std::unordered_map<std::string, int> originalToNode;
    std::unordered_map<std::string, std::vector<int>> regionToNodes;
    std::vector<std::vector<int>> adjacency;
#ifdef HALS_ENABLE_TORCH
    torch::jit::script::Module module;
    torch::Tensor staticEmbedding;
    torch::Tensor edgeIndexTensor;
    torch::Tensor edgeFeatureTensor;
    torch::Tensor edgeTargetPositionTensor;
    torch::Tensor levelNodesTensor;
    torch::Tensor levelNodePointerTensor;
    torch::Tensor levelEdgePointerTensor;
    torch::Tensor outputMaskTensor;
#endif

    bool LoadGraph(std::string& error) {
        std::ifstream input(config.graphTensorPath);
        if (!input.good()) {
            error = "cannot open signed-LAC graph tensor: " + config.graphTensorPath;
            return false;
        }
        std::string schema;
        std::string graphHash;
        if (!(input >> schema >> graphHash) ||
            schema != "HALS_SIGNED_LAC_GRAPH_V1") {
            error = "invalid signed-LAC graph tensor header";
            return false;
        }
        if (graphHash != config.graphHash) {
            error = "signed-LAC graph hash mismatch";
            return false;
        }
        if (!(input >> nodeCount >> staticDimension >> edgeCount >>
              edgeDimension >> levelCount) ||
            nodeCount <= 0 || staticDimension <= 0 || edgeCount < 0 ||
            edgeDimension <= 0 || levelCount <= 0) {
            error = "invalid signed-LAC graph tensor dimensions";
            return false;
        }
        if (!ReadValues(input, static_cast<size_t>(nodeCount) * staticDimension,
                        staticFeatures) ||
            !ReadValues(input, edgeCount, edgeSources) ||
            !ReadValues(input, edgeCount, edgeTargets) ||
            !ReadValues(input, static_cast<size_t>(edgeCount) * edgeDimension,
                        edgeFeatures) ||
            !ReadValues(input, edgeCount, edgeTargetPosition) ||
            !ReadValues(input, nodeCount, levelNodes) ||
            !ReadValues(input, levelCount + 1, levelNodePointer) ||
            !ReadValues(input, levelCount + 1, levelEdgePointer) ||
            !ReadValues(input, nodeCount, outputMask)) {
            error = "truncated signed-LAC graph tensor arrays";
            return false;
        }
        int mappingCount = 0;
        if (!(input >> mappingCount) || mappingCount <= 0) {
            error = "signed-LAC graph tensor lacks original-node mapping";
            return false;
        }
        for (int index = 0; index < mappingCount; ++index) {
            std::string identifier;
            int node = -1;
            if (!(input >> identifier >> node) || node < 0 || node >= nodeCount ||
                !originalToNode.emplace(identifier, node).second) {
                error = "invalid or duplicate original-node mapping";
                return false;
            }
        }
        int regionCount = 0;
        if (!(input >> regionCount) || regionCount < 0) {
            error = "invalid graph region count";
            return false;
        }
        for (int index = 0; index < regionCount; ++index) {
            std::string region;
            int count = 0;
            if (!(input >> region >> count) || count <= 0) {
                error = "invalid graph region entry";
                return false;
            }
            auto& nodes = regionToNodes[region];
            nodes.resize(count);
            for (int& node: nodes)
                if (!(input >> node) || node < 0 || node >= nodeCount) {
                    error = "invalid graph region node";
                    return false;
                }
        }
        if (regionToNodes.find(config.targetRegion) == regionToNodes.end()) {
            error = "target region is absent from signed-LAC graph tensor";
            return false;
        }
        adjacency.assign(nodeCount, {});
        for (int edge = 0; edge < edgeCount; ++edge) {
            int source = static_cast<int>(edgeSources[edge]);
            int target = static_cast<int>(edgeTargets[edge]);
            if (source < 0 || source >= nodeCount || target < 0 ||
                target >= nodeCount) {
                error = "graph tensor contains out-of-range edge";
                return false;
            }
            adjacency[source].emplace_back(target);
        }
        return true;
    }
};


SignedLACScorer::SignedLACScorer(): impl(std::make_unique<Impl>()) {}
SignedLACScorer::~SignedLACScorer() = default;

bool SignedLACScorer::BuiltWithLibTorch() {
#ifdef HALS_ENABLE_TORCH
    return true;
#else
    return false;
#endif
}

bool SignedLACScorer::Configure(
    const SignedLACScorerConfig& config,
    std::string& error
) {
    impl = std::make_unique<Impl>();
    impl->config = config;
    if (!config.Enabled() || config.modelPath.empty() ||
        config.graphTensorPath.empty()) {
        error = "signed-LAC model and graph tensor must be supplied together";
        return false;
    }
    if (!IsSha256Identity(config.modelHash) ||
        !IsSha256Identity(config.graphHash) ||
        !IsSha256Identity(config.graphTensorHash) ||
        !IsSha256Identity(config.prototypeHash)) {
        error = "signed-LAC model/graph/prototype identities must be SHA-256";
        return false;
    }
    if (config.metric != "MAPE" && config.metric != "NMED" &&
        config.metric != "NMSE" && config.metric != "MSE") {
        error = "invalid signed-LAC metric";
        return false;
    }
    if (config.targetRegion.empty() || config.batchSize <= 0 ||
        (config.torchThreads != 1 && config.torchThreads != 4 &&
         config.torchThreads != 8)) {
        error = "invalid signed-LAC region, batch size, or Torch thread count";
        return false;
    }
    if (HashFile(config.modelPath, error) != config.modelHash)
        return error.empty()? (error = "signed-LAC model SHA-256 mismatch", false): false;
    if (HashFile(config.graphTensorPath, error) != config.graphTensorHash)
        return error.empty()? (error = "signed-LAC graph tensor SHA-256 mismatch", false): false;
    if (!impl->LoadGraph(error))
        return false;
#ifndef HALS_ENABLE_TORCH
    error = "ResubALS was built without HALS_ENABLE_TORCH";
    return false;
#else
    try {
        at::set_num_threads(config.torchThreads);
        impl->module = torch::jit::load(config.modelPath, torch::kCPU);
        impl->module.eval();
        auto floatOptions = torch::TensorOptions().dtype(torch::kFloat32);
        auto longOptions = torch::TensorOptions().dtype(torch::kInt64);
        auto staticTensor = torch::from_blob(
            impl->staticFeatures.data(),
            {impl->nodeCount, impl->staticDimension}, floatOptions).clone();
        impl->edgeIndexTensor = torch::stack({
            torch::from_blob(impl->edgeSources.data(), {impl->edgeCount}, longOptions).clone(),
            torch::from_blob(impl->edgeTargets.data(), {impl->edgeCount}, longOptions).clone(),
        });
        impl->edgeFeatureTensor = torch::from_blob(
            impl->edgeFeatures.data(), {impl->edgeCount, impl->edgeDimension},
            floatOptions).clone();
        impl->edgeTargetPositionTensor = torch::from_blob(
            impl->edgeTargetPosition.data(), {impl->edgeCount}, longOptions).clone();
        impl->levelNodesTensor = torch::from_blob(
            impl->levelNodes.data(), {impl->nodeCount}, longOptions).clone();
        impl->levelNodePointerTensor = torch::from_blob(
            impl->levelNodePointer.data(), {impl->levelCount + 1}, longOptions).clone();
        impl->levelEdgePointerTensor = torch::from_blob(
            impl->levelEdgePointer.data(), {impl->levelCount + 1}, longOptions).clone();
        impl->outputMaskTensor = torch::from_blob(
            impl->outputMask.data(), {impl->nodeCount}, floatOptions).clone();
        torch::NoGradGuard noGrad;
        impl->staticEmbedding = impl->module.get_method("encode_static")(
            {staticTensor}).toTensor();
    }
    catch (const c10::Error& exception) {
        error = std::string("cannot load signed-LAC TorchScript: ") + exception.what();
        return false;
    }
    impl->ready = true;
    return true;
#endif
}

bool SignedLACScorer::Score(
    const std::vector<std::vector<TransitionGMMRecord>>& records,
    int candidateCount,
    std::vector<double>& predictions,
    std::string& error
) {
    predictions.clear();
    if (!impl->ready || candidateCount <= 0 ||
        static_cast<int>(records.size()) != candidateCount) {
        error = "signed-LAC scorer is not ready or candidate alignment is invalid";
        return false;
    }
#ifndef HALS_ENABLE_TORCH
    error = "ResubALS was built without HALS_ENABLE_TORCH";
    return false;
#else
    size_t featureCount = 0;
    for (int candidate = 0; candidate < candidateCount; ++candidate) {
        if (records[candidate].empty()) {
            error = "candidate lacks transition-GMM records";
            return false;
        }
        for (const auto& record: records[candidate]) {
            if (record.candidateId != candidate ||
                record.graphHash != impl->config.graphHash ||
                record.prototypeHash != impl->config.prototypeHash ||
                record.targetRegion != impl->config.targetRegion) {
                error = "stale or misaligned transition-GMM record";
                return false;
            }
            const auto features = FeatureVector(record);
            if (featureCount == 0)
                featureCount = features.size();
            if (features.size() != featureCount ||
                !std::all_of(features.begin(), features.end(),
                    [](double value) { return std::isfinite(value); })) {
                error = "transition-GMM feature width/value mismatch";
                return false;
            }
            if (impl->originalToNode.find(record.boundaryNode) ==
                impl->originalToNode.end()) {
                error = "transition-GMM boundary node is absent from graph";
                return false;
            }
        }
    }
    const int dynamicDimension = static_cast<int>(featureCount) + 3;
    predictions.reserve(candidateCount);
    try {
        torch::NoGradGuard noGrad;
        for (int begin = 0; begin < candidateCount; begin += impl->config.batchSize) {
            int end = std::min(candidateCount, begin + impl->config.batchSize);
            int count = end - begin;
            std::vector<float> dynamic(
                static_cast<size_t>(count) * impl->nodeCount * dynamicDimension,
                0.0f);
            auto at = [&](int candidate, int node, int feature) -> float& {
                return dynamic[(static_cast<size_t>(candidate) * impl->nodeCount + node)
                    * dynamicDimension + feature];
            };
            for (int local = 0; local < count; ++local) {
                int candidate = begin + local;
                for (int node: impl->regionToNodes.at(impl->config.targetRegion))
                    at(local, node, dynamicDimension - 3) = 1.0f;
                std::unordered_map<int, int> boundaryCounts;
                for (const auto& record: records[candidate]) {
                    int node = impl->originalToNode.at(record.boundaryNode);
                    auto features = FeatureVector(record);
                    for (size_t feature = 0; feature < features.size(); ++feature)
                        at(local, node, static_cast<int>(feature)) +=
                            static_cast<float>(features[feature]);
                    ++boundaryCounts[node];
                }
                std::vector<int> stack;
                std::unordered_set<int> reachable;
                for (const auto& [node, countAtNode]: boundaryCounts) {
                    for (size_t feature = 0; feature < featureCount; ++feature)
                        at(local, node, static_cast<int>(feature)) /= countAtNode;
                    at(local, node, dynamicDimension - 2) = 1.0f;
                    stack.emplace_back(node);
                    reachable.emplace(node);
                }
                while (!stack.empty()) {
                    int source = stack.back();
                    stack.pop_back();
                    for (int target: impl->adjacency[source])
                        if (reachable.emplace(target).second)
                            stack.emplace_back(target);
                }
                for (int node: reachable)
                    at(local, node, dynamicDimension - 1) = 1.0f;
            }
            auto dynamicTensor = torch::from_blob(
                dynamic.data(), {count, impl->nodeCount, dynamicDimension},
                torch::TensorOptions().dtype(torch::kFloat32)).clone();
            auto output = impl->module.get_method("forward_from_static")({
                impl->staticEmbedding,
                dynamicTensor,
                impl->edgeIndexTensor,
                impl->edgeFeatureTensor,
                impl->outputMaskTensor,
                impl->levelNodesTensor,
                impl->levelNodePointerTensor,
                impl->levelEdgePointerTensor,
                impl->edgeTargetPositionTensor,
            }).toTensor().to(torch::kCPU).contiguous();
            if (output.dim() != 1 || output.size(0) != count) {
                error = "TorchScript scorer returned an invalid prediction shape";
                return false;
            }
            const float* values = output.data_ptr<float>();
            for (int index = 0; index < count; ++index) {
                if (!std::isfinite(values[index])) {
                    error = "TorchScript scorer returned NaN/Inf";
                    return false;
                }
                predictions.emplace_back(values[index]);
            }
        }
    }
    catch (const c10::Error& exception) {
        error = std::string("signed-LAC inference failed: ") + exception.what();
        return false;
    }
    return static_cast<int>(predictions.size()) == candidateCount;
#endif
}

bool SignedLACScorer::IsReady() const { return impl->ready; }
