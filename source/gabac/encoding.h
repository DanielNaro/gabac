#ifndef GABAC_ENCODING_H_
#define GABAC_ENCODING_H_


#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */


#include <stdint.h> /* NOLINT */
#include <stdlib.h> /* NOLINT */


int gabac_encode(
        int64_t *symbols,
        size_t symbolsSize,
        unsigned int binarizationId,
        unsigned int *binarizationParameters,
        size_t binarizationParametersSize,
        unsigned int contextSelectionId,
        unsigned char **bitstream,
        size_t *bitstreamSize
);


#ifdef __cplusplus
}  // extern "C"


#include <vector>

#include "gabac/constants.h"


namespace gabac {

class Configuration;
struct LogInfo;

// Appends the size of a stream and the actual bytes to bytestream
void appendToBytestream(
        const std::vector<unsigned char>& bytes,
        std::vector<unsigned char> *const bytestream
);

void generateByteBuffer(
        const std::vector<uint64_t>& symbols,
        unsigned int wordSize,
        std::vector<unsigned char> * const buffer
);

int encode_core(
        const std::vector<int64_t>& symbols,
        const BinarizationId& binarizationId,
        const std::vector<unsigned int>& binarizationParameters,
        const ContextSelectionId& contextSelectionId,
        std::vector<unsigned char> *bitstream
);

int encode(
        const Configuration& configuration,
        const LogInfo& l,
        std::vector<uint64_t> *sequence,
        std::vector<unsigned char> *bytestream
);

void doDiffTransform(bool enabled,
                     const std::vector<uint64_t>& lutTransformedSequence,
                     const LogInfo& l,
                     std::vector<int64_t> *const diffAndLutTransformedSequence
);

void doLutTransform(bool enabled,
                    const std::vector<uint64_t>& transformedSequence,
                    unsigned int wordSize,
                    const LogInfo& l,
                    std::vector<unsigned char> *const bytestream,
                    std::vector<std::vector<uint64_t >> *const lutSequences
);

void doSequenceTransform(const std::vector<uint64_t>& sequence,
                         const gabac::SequenceTransformationId& transID,
                         uint64_t param,
                         const LogInfo& l,
                         std::vector<std::vector<uint64_t>> *const transformedSequences
);

}  // namespace gabac

#endif  /* __cplusplus */
#endif  /* GABAC_ENCODING_H_ */
