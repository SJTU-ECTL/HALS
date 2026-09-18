#include "transition_gmm.h"

#include <openssl/evp.h>
#include <unistd.h>


using namespace std;


namespace {

string Sha256Bytes(const string& bytes) {
    EVP_MD_CTX* context = EVP_MD_CTX_new();
    if (context == nullptr)
        throw runtime_error("cannot allocate SHA-256 context");
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digestLength = 0;
    const bool ok = EVP_DigestInit_ex(context, EVP_sha256(), nullptr) == 1
        && EVP_DigestUpdate(context, bytes.data(), bytes.size()) == 1
        && EVP_DigestFinal_ex(context, digest, &digestLength) == 1;
    EVP_MD_CTX_free(context);
    if (!ok || digestLength != 32)
        throw runtime_error("cannot compute SHA-256 digest");
    ostringstream out;
    out << "sha256:" << hex << setfill('0');
    for (unsigned int index = 0; index < digestLength; ++index)
        out << setw(2) << static_cast<unsigned>(digest[index]);
    return out.str();
}

string Sha256File(const filesystem::path& path) {
    ifstream input(path, ios::binary);
    if (!input.good())
        throw runtime_error("cannot open trace file for SHA-256: " + path.string());
    EVP_MD_CTX* context = EVP_MD_CTX_new();
    if (context == nullptr)
        throw runtime_error("cannot allocate SHA-256 context");
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digestLength = 0;
    bool ok = EVP_DigestInit_ex(context, EVP_sha256(), nullptr) == 1;
    char buffer[1 << 16];
    while (ok && input.good()) {
        input.read(buffer, sizeof(buffer));
        const streamsize count = input.gcount();
        if (count > 0)
            ok = EVP_DigestUpdate(context, buffer, static_cast<size_t>(count)) == 1;
    }
    ok = ok && input.eof()
        && EVP_DigestFinal_ex(context, digest, &digestLength) == 1;
    EVP_MD_CTX_free(context);
    if (!ok || digestLength != 32)
        throw runtime_error("cannot compute packed trace SHA-256");
    ostringstream out;
    out << "sha256:" << hex << setfill('0');
    for (unsigned int index = 0; index < digestLength; ++index)
        out << setw(2) << static_cast<unsigned>(digest[index]);
    return out.str();
}

void WriteLittleEndian(ofstream& out, uint64_t value, int bytes) {
    for (int index = 0; index < bytes; ++index) {
        const unsigned char byte = static_cast<unsigned char>(value & 0xffu);
        out.put(static_cast<char>(byte));
        value >>= 8;
    }
}

string JsonEscape(const string& text) {
    ostringstream out;
    for (unsigned char ch: text) {
        switch (ch) {
        case '\\': out << "\\\\"; break;
        case '"': out << "\\\""; break;
        case '\b': out << "\\b"; break;
        case '\f': out << "\\f"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (ch < 0x20)
                out << "\\u" << hex << setw(4) << setfill('0')
                    << static_cast<int>(ch) << dec << setfill(' ');
            else
                out << ch;
        }
    }
    return out.str();
}

string JsonNumber(double value) {
    if (!isfinite(value))
        return "null";
    ostringstream out;
    out << setprecision(17) << value;
    return out.str();
}

string JsonNumberVector(const vector<double>& values) {
    ostringstream out;
    out << "[";
    for (size_t i = 0; i < values.size(); ++i) {
        if (i)
            out << ",";
        out << JsonNumber(values[i]);
    }
    out << "]";
    return out.str();
}

string JsonStringVector(const vector<string>& values) {
    ostringstream out;
    out << "[";
    for (size_t i = 0; i < values.size(); ++i) {
        if (i)
            out << ",";
        out << "\"" << JsonEscape(values[i]) << "\"";
    }
    out << "]";
    return out.str();
}

}


bool TransitionGMMPrototypeSet::IsValid(string& error) const {
    if (dimension != 2 && dimension != 3) {
        error = "transition GMM dimension must be 2 or 3";
        return false;
    }
    if ((components != 1 && components != 2 && components != 4 && components != 8)
        || static_cast<int>(centers.size()) != components) {
        error = "transition GMM K must be 1, 2, 4, or 8 and match its rows";
        return false;
    }
    if (!isfinite(varianceFloor) || varianceFloor <= 0.0) {
        error = "transition GMM variance floor must be finite and positive";
        return false;
    }
    for (int k = 0; k < components; ++k) {
        if (static_cast<int>(centers[k].size()) != dimension) {
            error = "transition GMM prototype row has wrong dimension";
            return false;
        }
        for (double value: centers[k]) {
            if (!isfinite(value)) {
                error = "transition GMM prototype contains non-finite value";
                return false;
            }
        }
    }
    return true;
}


bool TransitionGMMPrototypeSet::Load(const string& path, string& error) {
    ifstream raw(path, ios::binary);
    if (!raw.good()) {
        error = "cannot open transition GMM prototype file: " + path;
        return false;
    }
    const string contents(
        (istreambuf_iterator<char>(raw)), istreambuf_iterator<char>()
    );
    try {
        sha256 = Sha256Bytes(contents);
    }
    catch (const exception& exception) {
        error = exception.what();
        return false;
    }
    istringstream in(contents);
    if (!in.good()) {
        error = "cannot open transition GMM prototype file: " + path;
        return false;
    }
    string schema;
    if (!(in >> schema >> dimension >> components >> varianceFloor) ||
        schema != "HALS_TRANSITION_GMM_PROTOTYPES_V1") {
        error = "invalid transition GMM prototype header in " + path;
        return false;
    }
    centers.assign(components, vector<double>(dimension, 0.0));
    for (int k = 0; k < components; ++k) {
        for (int d = 0; d < dimension; ++d) {
            if (!(in >> centers[k][d])) {
                error = "truncated transition GMM prototype matrix in " + path;
                return false;
            }
        }
    }
    string trailing;
    if (in >> trailing) {
        error = "unexpected trailing token in transition GMM prototype file: " + trailing;
        return false;
    }
    return IsValid(error);
}


TransitionGMMSketch::TransitionGMMSketch(
    const TransitionGMMPrototypeSet& prototypeSet
): prototypes(prototypeSet) {
    string error;
    if (!prototypes.IsValid(error))
        throw invalid_argument(error);
    counts.assign(prototypes.components, 0);
    sums.assign(
        prototypes.components,
        vector<double>(prototypes.dimension, 0.0)
    );
    crossProducts.assign(
        prototypes.components,
        vector<vector<double>>(
            prototypes.dimension,
            vector<double>(prototypes.dimension, 0.0)
        )
    );
}


void TransitionGMMSketch::Add(const vector<double>& value) {
    if (static_cast<int>(value.size()) != prototypes.dimension)
        throw invalid_argument("transition GMM sample has wrong dimension");
    ++totalCount;
    bool isZero = true;
    // In the 3-D signal-conditioned variant, dimensions 1 and 2 are the
    // transition errors.  Exact signal value alone must not leave the atom.
    const int errorStart = prototypes.dimension == 3? 1: 0;
    for (int d = errorStart; d < prototypes.dimension; ++d)
        isZero = isZero && value[d] == 0.0;
    if (isZero) {
        ++zeroCount;
        return;
    }

    int best = 0;
    double bestDistance = numeric_limits<double>::infinity();
    for (int k = 0; k < prototypes.components; ++k) {
        double distance = 0.0;
        for (int d = 0; d < prototypes.dimension; ++d) {
            const double delta = value[d] - prototypes.centers[k][d];
            distance += delta * delta;
        }
        if (distance < bestDistance) {
            bestDistance = distance;
            best = k;
        }
    }
    ++counts[best];
    for (int i = 0; i < prototypes.dimension; ++i) {
        sums[best][i] += value[i];
        for (int j = 0; j < prototypes.dimension; ++j)
            crossProducts[best][i][j] += value[i] * value[j];
    }
}


TransitionGMMRecord TransitionGMMSketch::Finalize() const {
    TransitionGMMRecord record;
    record.patternCount = static_cast<int>(totalCount);
    record.dimension = prototypes.dimension;
    record.components = prototypes.components;
    record.zeroProbability = totalCount == 0
        ? 0.0
        : static_cast<double>(zeroCount) / static_cast<double>(totalCount);
    const long long nonzeroCount = totalCount - zeroCount;
    record.mixture.reserve(prototypes.components);

    for (int k = 0; k < prototypes.components; ++k) {
        TransitionGMMComponent component;
        component.weight = nonzeroCount == 0
            ? 0.0
            : static_cast<double>(counts[k]) / static_cast<double>(nonzeroCount);
        component.mean = prototypes.centers[k];
        component.logVariance.assign(
            prototypes.dimension,
            log(prototypes.varianceFloor)
        );
        component.correlation.assign(
            prototypes.dimension * (prototypes.dimension - 1) / 2,
            0.0
        );
        if (counts[k] > 0) {
            for (int d = 0; d < prototypes.dimension; ++d)
                component.mean[d] = sums[k][d] / counts[k];
            vector<double> variance(prototypes.dimension, prototypes.varianceFloor);
            for (int d = 0; d < prototypes.dimension; ++d) {
                variance[d] = max(
                    prototypes.varianceFloor,
                    crossProducts[k][d][d] / counts[k]
                        - component.mean[d] * component.mean[d]
                );
                component.logVariance[d] = log(variance[d]);
            }
            int correlationIndex = 0;
            for (int i = 0; i < prototypes.dimension; ++i) {
                for (int j = i + 1; j < prototypes.dimension; ++j) {
                    const double covariance = crossProducts[k][i][j] / counts[k]
                        - component.mean[i] * component.mean[j];
                    const double denominator = sqrt(variance[i] * variance[j]);
                    component.correlation[correlationIndex++] = clamp(
                        covariance / denominator,
                        -1.0,
                        1.0
                    );
                }
            }
        }
        record.mixture.emplace_back(std::move(component));
    }

    // e_t/e_phi are the final two dimensions in both supported variants.
    stable_sort(
        record.mixture.begin(),
        record.mixture.end(),
        [](const TransitionGMMComponent& lhs,
           const TransitionGMMComponent& rhs) {
            const double lhsDelta = lhs.mean.back()
                - lhs.mean[lhs.mean.size() - 2];
            const double rhsDelta = rhs.mean.back()
                - rhs.mean[rhs.mean.size() - 2];
            return lhsDelta < rhsDelta;
        }
    );
    return record;
}


string TransitionGMMRecord::ToJson() const {
    ostringstream out;
    out << "{\"schema\":\"hals_lac_transition_gmm_v2\""
        << ",\"run_id\":\"" << JsonEscape(runId) << "\""
        << ",\"graph_sha256\":\"" << JsonEscape(graphHash) << "\""
        << ",\"base_state_sha256\":\"" << JsonEscape(baseStateHash) << "\""
        << ",\"candidate_sha256\":\"" << JsonEscape(candidateHash) << "\""
        << ",\"prototype_sha256\":\"" << JsonEscape(prototypeHash) << "\""
        << ",\"pattern_sha256\":\"" << JsonEscape(patternHash) << "\""
        << ",\"target_region\":\"" << JsonEscape(targetRegion) << "\""
        << ",\"boundary_node\":\"" << JsonEscape(boundaryNode) << "\""
        << ",\"candidate_id\":" << candidateId
        << ",\"target_node_id\":" << targetNodeId
        << ",\"output_index\":" << outputIndex
        << ",\"output_width\":" << outputWidth
        << ",\"output_signed\":" << (outputSigned? "true": "false")
        << ",\"pattern_count\":" << patternCount
        << ",\"dimension\":" << dimension
        << ",\"components\":" << components
        << ",\"p0\":" << JsonNumber(zeroProbability)
        << ",\"vecbee_scalar_error\":" << JsonNumber(vecbeeScalarError)
        << ",\"normalization\":\"2^w-1\""
        << ",\"extraction_method\":\"" << JsonEscape(extractionMethod) << "\""
        << ",\"timing_us\":{\"trace\":" << traceTimeUs
        << ",\"sketch\":" << sketchTimeUs
        << ",\"shared_analysis\":" << sharedAnalysisTimeUs
        << ",\"packed_trace_write\":" << packedTraceTimeUs << "}"
        << ",\"mixture\":[";
    for (size_t k = 0; k < mixture.size(); ++k) {
        if (k)
            out << ",";
        out << "{\"weight\":" << JsonNumber(mixture[k].weight)
            << ",\"mean\":" << JsonNumberVector(mixture[k].mean)
            << ",\"log_variance\":"
            << JsonNumberVector(mixture[k].logVariance)
            << ",\"correlation\":"
            << JsonNumberVector(mixture[k].correlation) << "}";
    }
    out << "]";
    if (!candidateTrace.empty() || !packedCandidateTracePath.empty()) {
        out << ",\"raw_trace\":{";
        bool needsComma = false;
        if (!candidateTrace.empty()) {
            out << "\"exact\":" << JsonStringVector(exactTrace)
                << ",\"current\":" << JsonStringVector(currentTrace)
                << ",\"candidate\":" << JsonStringVector(candidateTrace);
            needsComma = true;
        }
        if (!packedCandidateTracePath.empty()) {
            if (needsComma)
                out << ",";
            out << "\"format\":\"hals_vecbee_packed_trace_v1\""
                << ",\"candidate_path\":\""
                << JsonEscape(packedCandidateTracePath) << "\""
                << ",\"candidate_sha256\":\""
                << JsonEscape(packedCandidateTraceHash) << "\""
                << ",\"bytes_per_value\":"
                << packedCandidateTraceBytesPerValue;
        }
        out << "}";
    }
    out << "}";
    return out.str();
}


bool WritePackedTransitionTrace(
    const string& path,
    int outputWidth,
    bool outputSigned,
    const BigIntVect& values,
    string& sha256,
    int& bytesPerValue,
    string& error
) {
    if (outputWidth <= 0 || outputWidth > 512) {
        error = "packed transition trace width must be in [1,512]";
        return false;
    }
    bytesPerValue = (outputWidth + 7) / 8;
    const filesystem::path output(path);
    std::error_code ec;
    if (!output.parent_path().empty())
        filesystem::create_directories(output.parent_path(), ec);
    if (ec) {
        error = "cannot create packed transition trace directory: " + ec.message();
        return false;
    }
    ostringstream suffix;
    suffix << ".tmp." << getpid() << "." << this_thread::get_id();
    const filesystem::path temporary = output.string() + suffix.str();
    ofstream out(temporary, ios::binary | ios::trunc);
    if (!out.good()) {
        error = "cannot open packed transition trace: " + temporary.string();
        return false;
    }
    out.write("HVTBIN01", 8);
    WriteLittleEndian(out, static_cast<uint32_t>(outputWidth), 4);
    WriteLittleEndian(out, outputSigned? 1u: 0u, 1);
    WriteLittleEndian(out, 0u, 3);
    WriteLittleEndian(out, static_cast<uint64_t>(values.size()), 8);
    WriteLittleEndian(out, static_cast<uint32_t>(bytesPerValue), 4);
    const BigInt modulus = BigInt(1) << outputWidth;
    const BigInt mask = modulus - 1;
    for (const BigInt& value: values) {
        const BigInt raw = value & mask;
        for (int byte = 0; byte < bytesPerValue; ++byte) {
            const unsigned converted = static_cast<unsigned>(
                ((raw >> (byte * 8)) & 0xff).convert_to<unsigned>());
            out.put(static_cast<char>(converted));
        }
    }
    out.flush();
    if (!out.good()) {
        error = "failed while writing packed transition trace: " + temporary.string();
        out.close();
        filesystem::remove(temporary, ec);
        return false;
    }
    out.close();
    try {
        sha256 = Sha256File(temporary);
    }
    catch (const exception& exception) {
        error = exception.what();
        filesystem::remove(temporary, ec);
        return false;
    }
    if (filesystem::exists(output)) {
        try {
            if (Sha256File(output) != sha256) {
                error = "existing packed transition trace has different content: "
                    + output.string();
                filesystem::remove(temporary, ec);
                return false;
            }
        }
        catch (const exception& exception) {
            error = exception.what();
            filesystem::remove(temporary, ec);
            return false;
        }
        filesystem::remove(temporary, ec);
        return true;
    }
    filesystem::rename(temporary, output, ec);
    if (ec) {
        error = "cannot publish packed transition trace: " + ec.message();
        filesystem::remove(temporary, ec);
        return false;
    }
    return true;
}


string StableTransitionCandidateHash(const string& representation) {
    return Sha256Bytes(representation);
}


bool IsSha256Identity(const string& value) {
    if (value.size() != 71 || value.rfind("sha256:", 0) != 0)
        return false;
    return all_of(value.begin() + 7, value.end(), [](unsigned char ch) {
        return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
    });
}


bool WriteTransitionGMMJsonl(
    const string& path,
    const vector<vector<TransitionGMMRecord>>& records,
    bool append,
    string& error
) {
    filesystem::path output(path);
    if (!output.parent_path().empty()) {
        std::error_code ec;
        filesystem::create_directories(output.parent_path(), ec);
        if (ec) {
            error = "cannot create transition GMM output directory: " + ec.message();
            return false;
        }
    }
    ofstream out(
        output,
        ios::binary | (append? ios::app: ios::trunc)
    );
    if (!out.good()) {
        error = "cannot open transition GMM output: " + path;
        return false;
    }
    for (const auto& candidateRecords: records)
        for (const auto& record: candidateRecords)
            out << record.ToJson() << "\n";
    out.flush();
    if (!out.good()) {
        error = "failed while writing transition GMM output: " + path;
        return false;
    }
    return true;
}
