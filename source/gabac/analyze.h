#ifndef GABAC_ANALYSE_H_
#define GABAC_ANALYSE_H_


#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */


#include <stdint.h> /* NOLINT */
#include <stdlib.h> /* NOLINT */


//int gabacc_analyze


#ifdef __cplusplus
}  // extern "C"


#include <vector>

#include "gabac/constants.h"



namespace gabac {

struct CandidateConfig
{
    std::vector<unsigned> candidateWordsizes;
    std::vector<gabac::SequenceTransformationId> candidateSequenceTransformationIds;
    std::vector<uint32_t> candidateMatchCodingParameters;
    std::vector<uint32_t> candidateRLECodingParameters;
    std::vector<bool> candidateLUTCodingParameters;
    std::vector<bool> candidateDiffParameters;
    std::vector<gabac::BinarizationId> candidateUnsignedBinarizationIds;
    std::vector<gabac::BinarizationId> candidateSignedBinarizationIds;
    std::vector<unsigned> candidateBinarizationParameters;
    std::vector<gabac::ContextSelectionId> candidateContextSelectionIds;
};

class Configuration;
struct LogInfo;

int analyze(const std::vector<uint64_t>& symbols,
                                const gabac::LogInfo& l,
                                const CandidateConfig &conf,
                                std::vector<uint8_t> *bestByteStream,
                                gabac::Configuration *bestConfig,
                                gabac::Configuration *currentConfiguration
);


}  // namespace gabac

#endif  /* __cplusplus */
#endif  /* GABAC_ANALYSE_H_ */
