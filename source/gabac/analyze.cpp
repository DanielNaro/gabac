
#include "gabac/analyze.h"
#include "gabac/configuration.h"
#include "gabac/encoding.h"
#include "gabac/return_codes.h"


#include <cmath>
#include <limits>

namespace gabac {

//------------------------------------------------------------------------------

void getOptimumOfBinarizationParameter(const std::vector <int64_t>& diffTransformedSequence,
                                       gabac::BinarizationId binID,
                                       unsigned binParameter,
                                       const LogInfo& l,
                                       const CandidateConfig& candidateConfig,
                                       std::vector <uint8_t> *const bestByteStream,
                                       const std::vector <uint8_t>& lut,
                                       gabac::TransformedSequenceConfiguration *const bestConfig,
                                       gabac::TransformedSequenceConfiguration *const currentConfig
){

    for (const auto& transID : candidateConfig.candidateContextSelectionIds) {
        l.log(LogInfo::LogLevel::TRACE) << "Trying Context: " << unsigned(transID);
        std::vector <uint8_t> currentStream;

        currentConfig->contextSelectionId = transID;
        gabac::encode_core(diffTransformedSequence, binID, {binParameter}, transID, &currentStream);

        l.log(LogInfo::LogLevel::TRACE) << "Compressed size with parameter: " << currentStream.size();

        if ((currentStream.size() + lut.size() + 4 < bestByteStream->size()) || bestByteStream->empty()) {
            l.log(LogInfo::LogLevel::TRACE) << "Found new best context config: " << currentConfig->toPrintableString();
            *bestByteStream = lut;
            appendToBytestream(currentStream, bestByteStream);
            *bestConfig = *currentConfig;
        }

    }
}

//------------------------------------------------------------------------------

void getOptimumOfBinarization(const std::vector <int64_t>& diffTransformedSequence,
                              gabac::BinarizationId binID,
                              int64_t min, int64_t max,
                              std::vector <uint8_t> *const bestByteStream,
                              const std::vector <uint8_t>& lut,
                              const LogInfo& l,
                              const CandidateConfig& candidateConfig,
                              gabac::TransformedSequenceConfiguration *const bestConfig,
                              gabac::TransformedSequenceConfiguration *const currentConfig
){

    const unsigned BIPARAM = (max > 0) ? unsigned(std::floor(std::log2(max)) + 1) : 1;
    const unsigned TUPARAM = (max > 0) ? max : 1;
    const std::vector <std::vector<unsigned>> candidates = {{std::min(BIPARAM, 32u)},
                                                            {std::min(TUPARAM, 32u)},
                                                            {0},
                                                            {0},
                                                            candidateConfig.candidateBinarizationParameters,
                                                            candidateConfig.candidateBinarizationParameters};

    for (const auto& transID : candidates[unsigned(binID)]) {
        l.log(LogInfo::LogLevel::TRACE) << "Trying Parameter: " << transID;

        if (!gabac::binarizationInformation[unsigned(binID)].sbCheck(min, max, transID)) {
            l.log(LogInfo::LogLevel::TRACE) << "NOT valid for this stream!" << transID;
            continue;
        }

        currentConfig->binarizationParameters = {transID};

        getOptimumOfBinarizationParameter(
                diffTransformedSequence,
                binID,
                transID,
                l,
                candidateConfig,
                bestByteStream,
                lut,
                bestConfig,
                currentConfig
        );

    }
}

//------------------------------------------------------------------------------

void getOptimumOfDiffTransformedStream(const std::vector <int64_t>& diffTransformedSequence,
                                       unsigned wordsize,
                                       std::vector <uint8_t> *const bestByteStream,
                                       const std::vector <uint8_t>& lut,
                                       const LogInfo& l,
                                       const CandidateConfig& candidateConfig,
                                       gabac::TransformedSequenceConfiguration *const bestConfig,
                                       gabac::TransformedSequenceConfiguration *const currentConfig
){
    int64_t min = std::numeric_limits<int64_t>::max(), max = std::numeric_limits<int64_t>::min();
    l.log(LogInfo::LogLevel::TRACE) << "Stream analysis: ";
    for(const auto& v : diffTransformedSequence) {
        if(v > max)
            max = v;
        if(v < min)
            min = v;
    }

    l.log(LogInfo::LogLevel::TRACE) << "Min: " << min << "; Max: " << max;

    std::vector <gabac::BinarizationId>
            candidates = (min >= 0)
                         ? candidateConfig.candidateUnsignedBinarizationIds
                         : candidateConfig.candidateSignedBinarizationIds; // TODO: avoid copy

    for (const auto& transID : candidates) {
        l.log(LogInfo::LogLevel::TRACE) << "Trying Binarization: " << unsigned(transID);


        currentConfig->binarizationId = transID;
        getOptimumOfBinarization(
                diffTransformedSequence,
                transID,
                min,
                max,
                bestByteStream,
                lut,
                l,
                candidateConfig,
                bestConfig,
                currentConfig
        );

    }
}

//------------------------------------------------------------------------------

void getOptimumOfLutTransformedStream(const std::vector <uint64_t>& lutTransformedSequence,
                                      unsigned wordsize,
                                      std::vector <uint8_t> *const bestByteStream,
                                      const std::vector <uint8_t>& lut,
                                      const LogInfo& l,
                                      const CandidateConfig& candidateConfig,
                                      gabac::TransformedSequenceConfiguration *const bestConfig,
                                      gabac::TransformedSequenceConfiguration *const currentConfig
){
    for (const auto& transID : candidateConfig.candidateDiffParameters) {
        l.log(LogInfo::LogLevel::DEBUG) << "Trying Diff transformation: " << transID;
        std::vector <int64_t> diffStream;

        doDiffTransform(transID, lutTransformedSequence, l, &diffStream);
        l.log(LogInfo::LogLevel::DEBUG) << "Diff stream (uncompressed): " << diffStream.size() << " bytes";
        currentConfig->diffCodingEnabled = transID;
        getOptimumOfDiffTransformedStream(diffStream, wordsize, bestByteStream, lut, l, candidateConfig, bestConfig, currentConfig);
    }
}

//------------------------------------------------------------------------------

void getOptimumOfTransformedStream(const std::vector <uint64_t>& transformedSequence,
                                   unsigned wordsize,
                                   const LogInfo& l,
                                   const CandidateConfig& candidateConfig,
                                   std::vector<unsigned char> *const bestByteStream,
                                   gabac::TransformedSequenceConfiguration *const bestConfig
){
    for (const auto& transID : candidateConfig.candidateLUTCodingParameters) {
        l.log(LogInfo::LogLevel::DEBUG) << "Trying LUT transformation: " << transID;

        std::vector <uint8_t> lutEnc;
        std::vector <std::vector<uint64_t>> lutStreams;
        gabac::TransformedSequenceConfiguration currentConfiguration;
        currentConfiguration.lutTransformationParameter = 0;
        currentConfiguration.lutTransformationEnabled = transID;

        lutStreams.resize(2);
        doLutTransform(transID, transformedSequence, wordsize, l, &lutEnc, &lutStreams);
        if (lutStreams[0].size() != transformedSequence.size()) {
            l.log(LogInfo::LogLevel::DEBUG) << "Lut transformed failed. Probably the symbol space is too large. Skipping. ";
            continue;
        }
        l.log(LogInfo::LogLevel::DEBUG) << "LutTransformedSequence uncompressed size: " << lutStreams[0].size() << " bytes";
        l.log(LogInfo::LogLevel::DEBUG) << "Lut table (uncompressed): " << lutStreams[1].size() << " bytes";

        getOptimumOfLutTransformedStream(
                lutStreams[0],
                wordsize,
                bestByteStream,
                lutEnc,
                l,
                candidateConfig,
                bestConfig,
                &currentConfiguration
        );

    }
}

//------------------------------------------------------------------------------

void getOptimumOfSequenceTransform(const std::vector <uint64_t>& symbols,
                                   const std::vector <uint32_t>& candidateParameters,
                                   const LogInfo& l,
                                   const CandidateConfig& candidateConfig,
                                   std::vector<unsigned char> *const bestByteStream,
                                   gabac::Configuration *const bestConfig,
                                   gabac::Configuration *const currentConfig
){
    for (auto const& p : candidateParameters) {
        l.log(LogInfo::LogLevel::DEBUG) << "Trying sequence transformation parameter: " << unsigned(p);

        // Execute sequence transform
        std::vector <std::vector<uint64_t>> transformedSequences;
        doSequenceTransform(symbols, currentConfig->sequenceTransformationId, p, l, &transformedSequences);
        l.log(LogInfo::LogLevel::DEBUG) << "Got " << transformedSequences.size() << " transformed sequences";
        for (unsigned i = 0; i < transformedSequences.size(); ++i) {
            l.log(LogInfo::LogLevel::DEBUG) << i << ": " << transformedSequences[i].size() << " bytes";
        }


        currentConfig->sequenceTransformationParameter = p;
        currentConfig->transformedSequenceConfigurations.resize(transformedSequences.size());

        // Analyze transformed sequences
        std::vector<unsigned char> completeStream;
        bool error = false;
        for (unsigned i = 0; i < transformedSequences.size(); ++i) {
            unsigned currWordSize = gabac::fixWordSizes(
                    gabac::transformationInformation[unsigned(currentConfig->sequenceTransformationId)].wordsizes,
                    currentConfig->wordSize
            )[i];
            l.log(LogInfo::LogLevel::DEBUG) << "Analyzing sequence: "
                               << gabac::transformationInformation[unsigned(currentConfig->sequenceTransformationId)].streamNames[i]
                               << "";
            std::vector<unsigned char> bestTransformedStream;
            getOptimumOfTransformedStream(
                    transformedSequences[i],
                    currWordSize,
                    l,
                    candidateConfig,
                    &bestTransformedStream,
                    &(*currentConfig).transformedSequenceConfigurations[i]
            );

            if (bestTransformedStream.empty()) {
                error = true;
                break;
            }

            l.log(LogInfo::LogLevel::TRACE) << "Transformed and compressed sequence size: " << bestTransformedStream.size();

            //appendToBytestream(bestTransformedStream, &completeStream);
            completeStream.insert(completeStream.end(), bestTransformedStream.begin(), bestTransformedStream.end());

            if ((completeStream.size() >= bestByteStream->size()) &&
                (!bestByteStream->empty())) {
                l.log(LogInfo::LogLevel::TRACE) << "Already bigger stream than current maximum (Sequence transform level): Skipping "
                                   << bestTransformedStream.size();
                error = true;
                break;
            }
        }
        if (error) {
            l.log(LogInfo::LogLevel::DEBUG)
                    << "Could not find working gabac::Configuration for this stream, or smaller stream exists. skipping: "
                    << completeStream.size();
            continue;
        }

        l.log(LogInfo::LogLevel::TRACE) << "With parameter complete transformed size: " << completeStream.size();

        // Update optimum
        if (completeStream.size() < bestByteStream->size() || bestByteStream->empty()) {
            l.log(LogInfo::LogLevel::DEBUG) << "Found new best sequence transform: "
                               << unsigned(currentConfig->sequenceTransformationId)
                               << " with size "
                               << completeStream.size();
            *bestByteStream = std::move(completeStream);
            *bestConfig = *currentConfig;
        }
    }
}

//------------------------------------------------------------------------------

int analyze(const std::vector <uint64_t>& symbols,
                                const LogInfo& l,
                                const CandidateConfig& candidateConfig,
                                std::vector <uint8_t> *const bestByteStream,
                                gabac::Configuration *const bestConfig,
                                gabac::Configuration *const currentConfiguration
){
    const std::vector <uint32_t> candidateDefaultParameters = {0};
    const std::vector <uint32_t> *params[] = {&candidateDefaultParameters,
                                              &candidateDefaultParameters,
                                              &candidateConfig.candidateMatchCodingParameters,
                                              &candidateConfig.candidateRLECodingParameters};
    for (const auto& transID : candidateConfig.candidateSequenceTransformationIds) {
        l.log(LogInfo::LogLevel::DEBUG) << "Trying sequence transformation: "
                           << gabac::transformationInformation[unsigned(transID)].name;

        currentConfiguration->sequenceTransformationId = transID;
        // Core of analysis
        getOptimumOfSequenceTransform(
                symbols,
                *(params[unsigned(transID)]),
                l,
                candidateConfig,
                bestByteStream,
                bestConfig,
                currentConfiguration
        );

        l.log(LogInfo::LogLevel::TRACE) << "Sequence transformed compressed size: " << bestByteStream->size();
    }

    return GABAC_SUCCESS;
}

}