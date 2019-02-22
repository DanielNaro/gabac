#include "analysis.h"

#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <cstdio>

#include <algorithm>
#include <cassert>
#include <iomanip>
#include <iostream>
#include <limits>
#include <utility>
#include <string>
#include <vector>

#include "gabac/constants.h"
#include "gabac/encoding.h"

#include "gabac/configuration.h"
#include "gabacify/encode.h"
#include "gabac/exceptions.h"
#include "gabac/input_stream.h"
#include "gabacify/output_file.h"
#include "gabacify/input_file.h"

#include <stack>

namespace gabac {

struct Snapshot
{
    std::vector<DataBlock> streams;
};

struct TraversalInfo
{
    const IOConfiguration *ioconf{};
    EncodingConfiguration currConfig;
    EncodingConfiguration bestConfig;

    TransformedSequenceConfiguration bestSeqConfig;

    size_t currStreamIndex{};

    size_t currSequenceSize{};
    size_t bestSequenceSize{};

    size_t currTotalSize{};
    size_t bestTotalSize{};

    std::stack<Snapshot> stack;

};

//------------------------------------------------------------------------------

void getOptimumOfBinarizationParameter(const AnalysisConfiguration& aconf,
                                       TraversalInfo *info
){
    for (const auto& transID : aconf.candidateContextSelectionIds) {
        info->currConfig.transformedSequenceConfigurations[info->currStreamIndex].contextSelectionId = transID;
        info->stack.push(info->stack.top());
        size_t maxSize = std::min(info->bestSequenceSize - info->currSequenceSize, info->bestTotalSize - info->currTotalSize) - sizeof(uint32_t);
        gabac::encode_cabac(
                info->currConfig.transformedSequenceConfigurations[info->currStreamIndex].binarizationId,
                info->currConfig.transformedSequenceConfigurations[info->currStreamIndex].binarizationParameters,
                info->currConfig.transformedSequenceConfigurations[info->currStreamIndex].contextSelectionId,
                &(info->stack.top().streams.front()) , maxSize
        );
        info->currSequenceSize += sizeof(uint32_t) + info->stack.top().streams.front().size();
        if (info->bestSequenceSize > info->currSequenceSize) {
            info->bestSequenceSize = info->currSequenceSize;
            info->bestSeqConfig = info->currConfig.transformedSequenceConfigurations[info->currStreamIndex];
        }

        info->currSequenceSize -= sizeof(uint32_t) + info->stack.top().streams.front().size();

        info->stack.pop();
    }
}

//------------------------------------------------------------------------------

static void getMinMax(const gabac::DataBlock& b, uint64_t *umin, uint64_t *umax, int64_t *smin, int64_t *smax){
    gabac::BlockStepper r = b.getReader();
    *umin = std::numeric_limits<uint64_t>::max();
    *umax = std::numeric_limits<uint64_t>::min();
    *smin = std::numeric_limits<int64_t>::max();
    *smax = std::numeric_limits<int64_t>::min();
    while (r.isValid()) {
        uint64_t val = r.get();
        *umax = std::max(*umax, val);
        *umin = std::min(*umin, val);
        auto sval = int64_t(val);
        *smax = std::max(*smax, sval);
        *smin = std::min(*smin, sval);
        r.inc();
    }
}


void getOptimumOfBinarization(const AnalysisConfiguration& aconf,
                              TraversalInfo *info
){

    uint64_t min, max;
    int64_t smin, smax;
    getMinMax(info->stack.top().streams.front(), &min, &max, &smin, &smax);

    const unsigned BIPARAM = (max > 0) ? unsigned(std::floor(std::log2(max)) + 1) : 1;
    const unsigned TUPARAM = (max > 0) ? unsigned(std::min(max, uint64_t(256))) : 1;
    const std::vector<std::vector<unsigned>> candidates = {{std::min(BIPARAM, 32u)},
                                                           {std::min(TUPARAM, 32u)},
                                                           {0},
                                                           {0},
                                                           aconf.candidateBinarizationParameters,
                                                           aconf.candidateBinarizationParameters};
    auto id = unsigned(
            info->currConfig
                    .transformedSequenceConfigurations[info->currStreamIndex].binarizationId
    );

    for (const auto& transID : candidates[id]) {
        if (!binarizationInformation[id].isSigned) {
            if (!binarizationInformation[id].sbCheck(min, max, 0)) {
                continue;
            }
        } else {
            if (!binarizationInformation[id].sbCheck(uint64_t(smin), uint64_t(smax), 0)) {
                continue;
            }
        }


        info->currConfig.transformedSequenceConfigurations[info->currStreamIndex].binarizationParameters = {transID};

        getOptimumOfBinarizationParameter(
                aconf,
                info
        );
    }
}

//------------------------------------------------------------------------------

void getOptimumOfDiffTransformedStream(const AnalysisConfiguration& aconf,
                                       TraversalInfo *info
){
    std::vector<gabac::BinarizationId>
            candidates = (!info->currConfig.transformedSequenceConfigurations[info->currStreamIndex].diffCodingEnabled)
                         ? aconf.candidateUnsignedBinarizationIds
                         : aconf.candidateSignedBinarizationIds; // TODO: avoid copy

    for (const auto& transID : candidates) {
        info->currConfig.transformedSequenceConfigurations[info->currStreamIndex].binarizationId = transID;
        getOptimumOfBinarization(
                aconf,
                info
        );
    }
}

//------------------------------------------------------------------------------

void getOptimumOfLutTransformedStream(const AnalysisConfiguration& aconf,
                                      TraversalInfo *info
){

    for (const auto& transID : aconf.candidateDiffParameters) {
        info->currConfig.transformedSequenceConfigurations[info->currStreamIndex].diffCodingEnabled = transID;
        if (transID) {
            info->stack.push(info->stack.top());
            const size_t DIFF_INDEX = 5;
            gabac::transformationInformation[DIFF_INDEX].transform(
                    0,
                    &info->stack.top().streams
            );
        }
        getOptimumOfDiffTransformedStream(aconf, info);

        if (transID) {
            info->stack.pop();
        }
    }
}

//------------------------------------------------------------------------------

void getOptimumOfLutEnabled(const AnalysisConfiguration& aconf,
                            TraversalInfo *info
){
    for (const auto& transID : aconf.candidateLutOrder) {
        info->currConfig.transformedSequenceConfigurations[info->currStreamIndex].lutOrder = transID;
        info->stack.push(info->stack.top());

        try {
            const size_t LUT_INDEX = 4;
            gabac::transformationInformation[LUT_INDEX].transform(
                    info->currConfig.transformedSequenceConfigurations[info->currStreamIndex].lutOrder,
                    &info->stack.top().streams
            );
        } catch (...) {
            continue;
        }

        unsigned bits0 = 0;
        uint64_t min, max;
        int64_t smin, smax;
        getMinMax(info->stack.top().streams[1], &min, &max, &smin, &smax);
        bits0 = unsigned(std::ceil(std::log2(max + 1)));
        if (max <= 1) {
            bits0 = 1;
        }
        auto bits1 = unsigned(info->stack.top().streams[1].size());

        info->currConfig.transformedSequenceConfigurations[info->currStreamIndex].lutBits = bits0;

        gabac::encode_cabac(
                gabac::BinarizationId::BI,
                {bits0},
                gabac::ContextSelectionId::bypass,
                &info->stack.top().streams[1]
        );


        info->currSequenceSize += sizeof(uint32_t) + info->stack.top().streams[1].size();

        if (info->currConfig.transformedSequenceConfigurations[info->currStreamIndex].lutOrder > 0) {
            bits1 = unsigned(std::ceil(std::log2(bits1)));
            gabac::encode_cabac(
                    gabac::BinarizationId::BI,
                    {bits1},
                    gabac::ContextSelectionId::bypass,
                    &info->stack.top().streams[2]
            );

            info->currSequenceSize += sizeof(uint32_t) + info->stack.top().streams[2].size();
        }

        getOptimumOfLutTransformedStream(aconf, info);

        info->stack.pop();
    }
}

//------------------------------------------------------------------------------

void getOptimumOfTransformedStream(const AnalysisConfiguration& aconf,
                                   TraversalInfo *info
){
    for (const auto& transID : aconf.candidateLUTCodingParameters) {
        info->currConfig.transformedSequenceConfigurations[info->currStreamIndex].lutTransformationEnabled = transID;
        info->currSequenceSize = 0;
        if (transID) {
            getOptimumOfLutEnabled(aconf, info);
        } else {
            getOptimumOfLutTransformedStream(aconf, info);
        }
    }
}

//------------------------------------------------------------------------------

void getOptimumOfSequenceTransform(const AnalysisConfiguration& aconf,
                                   TraversalInfo *info
){
    const std::vector<uint32_t> candidateDefaultParameters = {0};
    const std::vector<uint32_t> *params[] = {&candidateDefaultParameters,
                                             &candidateDefaultParameters,
                                             &aconf.candidateMatchCodingParameters,
                                             &aconf.candidateRLECodingParameters};
    for (auto const& p : *(params[unsigned(info->currConfig.sequenceTransformationId)])) {
        info->stack.push(info->stack.top());

        info->currConfig.sequenceTransformationParameter = p;

        gabac::transformationInformation[unsigned(info->currConfig.sequenceTransformationId)].transform(
                p,
                &info->stack.top().streams
        );

        info->currTotalSize = 0;

        for (unsigned i = 0; i < info->stack.top().streams.size(); ++i) {
            info->stack.push(info->stack.top());
            info->stack.top().streams[i].swap(&info->stack.top().streams[0]);
            info->stack.top().streams.resize(1);

            info->ioconf->log(gabac::IOConfiguration::LogLevel::INFO) << "Stream " << i << "..." << std::endl;
            info->bestSequenceSize = std::numeric_limits<size_t>::max();
            info->currStreamIndex = i;
            getOptimumOfTransformedStream(aconf, info);
            info->currConfig.transformedSequenceConfigurations[i] = info->bestSeqConfig;
            if (info->bestSequenceSize == std::numeric_limits<size_t>::max()) {
                info->ioconf->log(gabac::IOConfiguration::LogLevel::DEBUG)
                        << "Found no valid configuration for stream "
                        << info->currStreamIndex
                        << " of transformation"
                        << unsigned(info->currConfig.sequenceTransformationId)
                        << " in word size "
                        << info->currConfig.wordSize
                        << " Skipping!"
                        << std::endl;
                info->currTotalSize = info->bestSequenceSize;
                info->stack.pop();
                break;
            }

            info->currTotalSize += info->bestSequenceSize;
            if(info->currTotalSize >= info->bestTotalSize) {
                info->ioconf->log(gabac::IOConfiguration::LogLevel::TRACE)
                    << "Skipping. Bitstream already larger than permitted." << std::endl;
                info->stack.pop();
                break;
            }
            info->stack.pop();
        }

        if (info->currTotalSize < info->bestTotalSize) {
            info->ioconf->log(gabac::IOConfiguration::LogLevel::DEBUG)
                    << "Found configuration compressing to "
                    << info->currTotalSize
                    << " bytes."
                    << std::endl;
            info->bestTotalSize = info->currTotalSize;
            info->bestConfig = info->currConfig;
        }

        info->stack.pop();
    }
}

//------------------------------------------------------------------------------

void getOptimumOfSymbolSequence(const AnalysisConfiguration& aconf,
                                TraversalInfo *info
){
    for (const auto& transID : aconf.candidateSequenceTransformationIds) {
        info->ioconf->log(gabac::IOConfiguration::LogLevel::INFO) << "Transformation " << unsigned(transID) << "..." << std::endl;
        info->currConfig.sequenceTransformationId = transID;
        info->currConfig
                .transformedSequenceConfigurations
                .resize(transformationInformation[unsigned(transID)].wordsizes.size());
        // Core of analysis
        getOptimumOfSequenceTransform(
                aconf,
                info
        );
    }
}


size_t analyze(const IOConfiguration& ioconf, const AnalysisConfiguration& aconf, EncodingConfiguration *econf){
    ioconf.validate();
    TraversalInfo info;
    info.ioconf = &ioconf;
    info.stack.emplace();
    info.stack.top().streams.emplace_back(ioconf.inputStream->getTotalSize(), 1);
    info.bestTotalSize = std::numeric_limits<size_t>::max();
    ioconf.inputStream->readFull(&info.stack.top().streams.front());

    for (const auto& w : aconf.candidateWordsizes) {
        ioconf.log(gabac::IOConfiguration::LogLevel::INFO) << "Wordsize " << w << "..." << std::endl;
        if (ioconf.inputStream->getTotalSize() % w != 0) {
            ioconf.log(gabac::IOConfiguration::LogLevel::WARNING) << "Input stream size "
                                                                  << ioconf.inputStream->getTotalSize()
                                                                  << " is not a multiple of word size "
                                                                  << w
                                                                  << "! Skipping word size." << std::endl;
            continue;
        }

        info.stack.top().streams.front().setWordSize((uint8_t) w);
        info.currConfig.wordSize = w;

        getOptimumOfSymbolSequence(aconf, &info);
    }

    *econf = info.bestConfig;

    ioconf.log(gabac::IOConfiguration::LogLevel::INFO)
            << "Success! Best configuration will compress down to "
            << info.bestTotalSize
            << " bytes."
            << std::endl;

    return 0;
}

//------------------------------------------------------------------------------

}  // namespace gabacify

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------