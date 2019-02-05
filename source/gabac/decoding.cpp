#include "gabac/decoding.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <limits>

#include "gabac/configuration.h"
#include "gabac/constants.h"
#include "gabac/diff_coding.h"
#include "gabac/reader.h"
#include "gabac/return_codes.h"


// ----------------------------------------------------------------------------
// C wrapper BEGIN
// ----------------------------------------------------------------------------


int gabac_decode(
        unsigned char *const bitstream,
        size_t bitstreamSize,
        unsigned int binarizationId,
        unsigned int *const binarizationParameters,
        size_t binarizationParametersSize,
        unsigned int contextSelectionId,
        int64_t **const symbols,
        size_t *const symbolsSize
){
    if (bitstream == nullptr)
    {
        return GABAC_FAILURE;
    }
    if (binarizationParameters == nullptr)
    {
        return GABAC_FAILURE;
    }
    if (symbols == nullptr)
    {
        return GABAC_FAILURE;
    }
    if (symbolsSize == nullptr)
    {
        return GABAC_FAILURE;
    }

    // C++-style vectors to receive input data / accumulate output data
    std::vector<unsigned char> bitstreamVector(
            bitstream,
            (bitstream + bitstreamSize)
    );
    std::vector<unsigned int> binarizationParametersVector(
            binarizationParameters,
            (binarizationParameters + binarizationParametersSize)
    );
    std::vector<int64_t> symbolsVector;

    assert(binarizationId <= static_cast<int>(gabac::BinarizationId::STEG));
    assert(contextSelectionId <= static_cast<int>(gabac::ContextSelectionId::adaptive_coding_order_2));

    // Execute
    int rc = gabac::decode_core(
            bitstreamVector,
            static_cast<gabac::BinarizationId>(binarizationId),
            binarizationParametersVector,
            static_cast<gabac::ContextSelectionId>(contextSelectionId),
            &symbolsVector
    );
    if (rc != GABAC_SUCCESS)
    {
        return GABAC_FAILURE;
    }

    // Extract plain C array data from result vectors
    *symbolsSize = symbolsVector.size();
    *symbols = static_cast<int64_t*>(malloc(sizeof(int64_t) * (*symbolsSize)));
    std::copy(symbolsVector.begin(), symbolsVector.end(), *symbols);

    return GABAC_SUCCESS;
}


// ----------------------------------------------------------------------------
// C wrapper END
// ----------------------------------------------------------------------------


namespace gabac {


int decode_core(
        const std::vector<unsigned char>& bitstream,
        const BinarizationId& binarizationId,
        const std::vector<unsigned int>& binarizationParameters,
        const ContextSelectionId& contextSelectionId,
        std::vector<int64_t> *const symbols
){
    if (symbols == nullptr)
    {
        return GABAC_FAILURE;
    }

    Reader reader(bitstream);
    size_t symbolsSize = reader.start();

    // symbols->clear();
    symbols->resize(symbolsSize);

    int64_t symbol = 0;
    unsigned int previousSymbol = 0;
    unsigned int previousPreviousSymbol = 0;

    for (size_t i = 0; i < symbolsSize; i++)
    {
        if (contextSelectionId == ContextSelectionId::bypass)
        {
            symbol = reader.readBypassValue(
                    binarizationId,
                    binarizationParameters
            );
            (*symbols)[i] = symbol;
        }
        else if (contextSelectionId
                 == ContextSelectionId::adaptive_coding_order_0)
        {
            symbol = reader.readAdaptiveCabacValue(
                    binarizationId,
                    binarizationParameters,
                    0,
                    0
            );
            (*symbols)[i] = symbol;
        }
        else if (contextSelectionId
                 == ContextSelectionId::adaptive_coding_order_1)
        {
            symbol = reader.readAdaptiveCabacValue(
                    binarizationId,
                    binarizationParameters,
                    previousSymbol,
                    0
            );
            (*symbols)[i] = symbol;
            if (symbol < 0)
            {
                symbol = -symbol;
            }
            if (symbol > 3)
            {
                previousSymbol = 3;
            }
            else
            {
                assert(symbol <= std::numeric_limits<unsigned int>::max());
                previousSymbol = static_cast<unsigned int>(symbol);
            }
        }
        else if (contextSelectionId
                 == ContextSelectionId::adaptive_coding_order_2)
        {
            symbol = reader.readAdaptiveCabacValue(
                    binarizationId,
                    binarizationParameters,
                    previousSymbol,
                    previousPreviousSymbol
            );
            (*symbols)[i] = symbol;
            previousPreviousSymbol = previousSymbol;
            if (symbol < 0)
            {
                symbol = -symbol;
            }
            if (symbol > 3)
            {
                previousSymbol = 3;
            }
            else
            {
                assert(symbol <= std::numeric_limits<unsigned int>::max());
                previousSymbol = static_cast<unsigned int>(symbol);
            }
        }
        else
        {
            return GABAC_FAILURE;
        }
    }

    reader.reset();

    return GABAC_SUCCESS;
}



//------------------------------------------------------------------------------

static size_t extractFromBytestream(
        const std::vector<unsigned char>& bytestream,
        size_t bytestreamPosition,
        std::vector<unsigned char> *const bytes
){
    assert(bytes != nullptr);

    // Set up our output 'bytes'
    bytes->clear();

    uint32_t chunkSize = 0;
    memcpy(&chunkSize, bytestream.data() + bytestreamPosition, sizeof(uint32_t));
    bytestreamPosition += sizeof(uint32_t);

    // Get the next 'chunkSize' bytes from the bytestream
    for (size_t i = 0; i < chunkSize; i++)
    {
        bytes->push_back(bytestream.at(bytestreamPosition++));
    }

    return bytestreamPosition;
}

//------------------------------------------------------------------------------

static void decodeInverseLUT(const std::vector<unsigned char>& bytestream,
                             unsigned wordSize,
                             const LogInfo& l,
                             size_t *const bytestreamPosition,
                             std::vector<uint64_t> *const inverseLut
){
    // Decode the inverse LUT
    std::vector<unsigned char> inverseLutBitstream;
    *bytestreamPosition = extractFromBytestream(bytestream, *bytestreamPosition, &inverseLutBitstream);
    l.log(LogInfo::LogLevel::TRACE) << "Read LUT bitstream with size: " << inverseLutBitstream.size();
    std::vector<int64_t> inverseLutTmp;
    gabac::decode_core(
            inverseLutBitstream,
            gabac::BinarizationId::BI,
            {wordSize * 8},
            gabac::ContextSelectionId::bypass,
            &inverseLutTmp
    );

    inverseLut->reserve(inverseLutTmp.size());

    for (const auto& inverseLutTmpEntry : inverseLutTmp)
    {
        assert(inverseLutTmpEntry >= 0);
        inverseLut->push_back(static_cast<uint64_t>(inverseLutTmpEntry));
    }
}

//------------------------------------------------------------------------------

static void doDiffCoding(const std::vector<int64_t>& diffAndLutTransformedSequence,
                         bool enabled,
                         const LogInfo& l,
                         std::vector<uint64_t> *const lutTransformedSequence
){
    // Diff coding
    if (enabled)
    {
        l.log(LogInfo::LogLevel::TRACE) << "Diff coding *en*abled";
        gabac::inverseTransformDiffCoding(diffAndLutTransformedSequence, lutTransformedSequence);
        return;
    }


    l.log(LogInfo::LogLevel::TRACE) << "Diff coding *dis*abled";
    lutTransformedSequence->reserve(diffAndLutTransformedSequence.size());
    for (const auto& diffAndLutTransformedSymbol : diffAndLutTransformedSequence)
    {
        assert(diffAndLutTransformedSymbol >= 0);
        lutTransformedSequence->push_back(static_cast<uint64_t>(diffAndLutTransformedSymbol));
    }
}

//------------------------------------------------------------------------------

static void doLUTCoding(const std::vector<std::vector<uint64_t>>& lutSequences,
                        bool enabled,
                        const LogInfo& l,
                        std::vector<uint64_t> *const transformedSequence
){
    if (enabled)
    {
        l.log(LogInfo::LogLevel::TRACE) << "LUT transform *en*abled";

        // Do the inverse LUT transform
        const unsigned LUT_INDEX = 4;
        gabac::transformationInformation[LUT_INDEX].inverseTransform(lutSequences, 0, transformedSequence);
        return;
    }

    l.log(LogInfo::LogLevel::TRACE) << "LUT transform *dis*abled";
    *transformedSequence = lutSequences[0]; // TODO: std::move() (currently not possible because of const)
}

//------------------------------------------------------------------------------

static void doEntropyCoding(const std::vector<unsigned char>& bytestream,
                            const gabac::TransformedSequenceConfiguration& transformedSequenceConfiguration,
                            const LogInfo& l,
                            size_t *const bytestreamPosition,
                            std::vector<int64_t> *const diffAndLutTransformedSequence
){
    // Extract encoded diff-and-LUT-transformed sequence (i.e. a
    // bitstream) from the bytestream
    std::vector<unsigned char> bitstream;
    *bytestreamPosition = extractFromBytestream(bytestream, *bytestreamPosition, &bitstream);
    l.log(LogInfo::LogLevel::TRACE) << "Bitstream size: " << bitstream.size();

    // Decoding
    gabac::decode_core(
            bitstream,
            transformedSequenceConfiguration.binarizationId,
            transformedSequenceConfiguration.binarizationParameters,
            transformedSequenceConfiguration.contextSelectionId,
            diffAndLutTransformedSequence
    );
}

//------------------------------------------------------------------------------

int decode(
        std::vector<unsigned char>* bytestream,
        const gabac::Configuration& configuration,
        const LogInfo& l,
        std::vector<uint64_t> *const sequence
){
    assert(sequence != nullptr);

    sequence->clear();

    // Set up for the inverse sequence transformation
    size_t numTransformedSequences =
            gabac::transformationInformation[unsigned(configuration.sequenceTransformationId)].wordsizes.size();

    // Loop through the transformed sequences
    std::vector<std::vector<uint64_t>> transformedSequences;
    size_t bytestreamPosition = 0;
    for (size_t i = 0; i < numTransformedSequences; i++)
    {
        l.log(LogInfo::LogLevel::TRACE) << "Processing transformed sequence: " << i;
        auto transformedSequenceConfiguration = configuration.transformedSequenceConfigurations.at(i);

        unsigned int wordSize =
                gabac::fixWordSizes(
                        gabac::transformationInformation[unsigned(configuration.sequenceTransformationId)].wordsizes,
                        configuration.wordSize
                )[i];

        std::vector<uint64_t> inverseLut;
        if (transformedSequenceConfiguration.lutTransformationEnabled)
        {
            decodeInverseLUT(*bytestream, wordSize, l, &bytestreamPosition, &inverseLut);
        }

        std::vector<int64_t> diffAndLutTransformedSequence;
        doEntropyCoding(
                *bytestream,
                configuration.transformedSequenceConfigurations[i],
                l,
                &bytestreamPosition,
                &diffAndLutTransformedSequence
        );

        std::vector<std::vector<uint64_t>> lutTransformedSequences(2);
        doDiffCoding(
                diffAndLutTransformedSequence,
                configuration.transformedSequenceConfigurations[i].diffCodingEnabled,
                l,
                &(lutTransformedSequences[0])
        );
        diffAndLutTransformedSequence.clear();
        diffAndLutTransformedSequence.shrink_to_fit();

        lutTransformedSequences[1] = std::move(inverseLut);

        // LUT transform
        std::vector<uint64_t> transformedSequence;
        doLUTCoding(
                lutTransformedSequences,
                configuration.transformedSequenceConfigurations[i].lutTransformationEnabled,
                l,
                &transformedSequence
        );

        lutTransformedSequences.clear();
        lutTransformedSequences.shrink_to_fit();


        transformedSequences.push_back(std::move(transformedSequence));
    }

    bytestream->clear();
    bytestream->shrink_to_fit();

    gabac::transformationInformation[unsigned(configuration.sequenceTransformationId)].inverseTransform(
            transformedSequences,
            configuration.sequenceTransformationParameter,
            sequence
    );
    l.log(LogInfo::LogLevel::TRACE) << "Decoded sequence of length: " << sequence->size();

    return GABAC_SUCCESS;
}


}  // namespace gabac
