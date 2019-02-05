#include "gabac/encoding.h"

#include <algorithm>
#include <cassert>
#include <limits>

#include "gabac/configuration.h"
#include "gabac/constants.h"
#include "gabac/return_codes.h"
#include "gabac/writer.h"
#include "gabac.h"


// ----------------------------------------------------------------------------
// C wrapper BEGIN
// ----------------------------------------------------------------------------


int gabac_encode(
        int64_t *const symbols,
        size_t symbolsSize,
        unsigned int binarizationId,
        unsigned int *const binarizationParameters,
        size_t binarizationParametersSize,
        unsigned int contextSelectionId,
        unsigned char **const bitstream,
        size_t *const bitstreamSize
){
    if (symbols == nullptr) {
        return GABAC_FAILURE;
    }
    if (binarizationParameters == nullptr) {
        return GABAC_FAILURE;
    }
    if (bitstream == nullptr) {
        return GABAC_FAILURE;
    }
    if (bitstreamSize == nullptr) {
        return GABAC_FAILURE;
    }

    // C++-style vectors to receive input data / accumulate output data
    std::vector<int64_t> symbolsVector(
            symbols,
            (symbols + symbolsSize)
    );
    std::vector<unsigned int> binarizationParametersVector(
            binarizationParameters,
            (binarizationParameters + binarizationParametersSize)
    );
    std::vector<unsigned char> bitstreamVector;

    assert(binarizationId <= static_cast<int>(gabac::BinarizationId::STEG));
    assert(contextSelectionId <= static_cast<int>(gabac::ContextSelectionId::adaptive_coding_order_2));
    // Execute
    int rc = gabac::encode_core(
            symbolsVector,
            static_cast<gabac::BinarizationId>(binarizationId),
            binarizationParametersVector,
            static_cast<gabac::ContextSelectionId>(contextSelectionId),
            &bitstreamVector
    );
    if (rc != GABAC_SUCCESS) {
        return GABAC_FAILURE;
    }

    // Extract plain C array data from result vectors
    *bitstreamSize = bitstreamVector.size();
    *bitstream = (unsigned char *) malloc(sizeof(char) * (*bitstreamSize));
    std::copy(bitstreamVector.begin(), bitstreamVector.end(), *bitstream);

    return GABAC_SUCCESS;
}


// ----------------------------------------------------------------------------
// C wrapper END
// ----------------------------------------------------------------------------


namespace gabac {


int encode_core(
        const std::vector<int64_t>& symbols,
        const BinarizationId& binarizationId,
        const std::vector<unsigned int>& binarizationParameters,
        const ContextSelectionId& contextSelectionId,
        std::vector<unsigned char> *const bitstream
){
    assert(bitstream != nullptr);
#ifndef NDEBUG
    const unsigned int paramSize[unsigned(BinarizationId::STEG) + 1u] = {1, 1, 0, 0, 1, 1};
#endif
    assert(binarizationParameters.size() >= paramSize[static_cast<int>(binarizationId)]);

    bitstream->clear();

    Writer writer(bitstream);
    writer.start(symbols.size());

    unsigned int previousSymbol = 0;
    unsigned int previousPreviousSymbol = 0;

    for (int64_t symbol : symbols) {
        if (contextSelectionId == ContextSelectionId::bypass) {
            writer.writeBypassValue(
                    symbol,
                    binarizationId,
                    binarizationParameters
            );
        } else if (contextSelectionId == ContextSelectionId::adaptive_coding_order_0) {
            writer.writeCabacAdaptiveValue(
                    symbol,
                    binarizationId,
                    binarizationParameters,
                    0,
                    0
            );
        } else if (contextSelectionId == ContextSelectionId::adaptive_coding_order_1) {
            writer.writeCabacAdaptiveValue(
                    symbol,
                    binarizationId,
                    binarizationParameters,
                    previousSymbol,
                    0
            );
            if (symbol < 0) {
                symbol = -symbol;
            }
            if (symbol > 3) {
                previousSymbol = 3;
            } else {
                assert(symbol <= std::numeric_limits<unsigned int>::max());
                previousSymbol = static_cast<unsigned int>(symbol);
            }
        } else if (contextSelectionId == ContextSelectionId::adaptive_coding_order_2) {
            writer.writeCabacAdaptiveValue(
                    symbol,
                    binarizationId,
                    binarizationParameters,
                    previousSymbol,
                    previousPreviousSymbol
            );
            previousPreviousSymbol = previousSymbol;
            if (symbol < 0) {
                symbol = -symbol;
            }
            if (symbol > 3) {
                previousSymbol = 3;
            } else {
                assert(symbol <= std::numeric_limits<unsigned int>::max());
                previousSymbol = static_cast<unsigned int>(symbol);
            }
        } else {
            return GABAC_FAILURE;
        }
    }

    writer.reset();

    return GABAC_SUCCESS;
}


void generateByteBuffer(
        const std::vector<uint64_t>& symbols,
        unsigned int wordSize,
        std::vector<unsigned char> * const buffer
){
    assert((wordSize == 1) || (wordSize == 2) || (wordSize == 4) || (wordSize == 8));
    assert(buffer != nullptr);

    // Prepare the (output) buffer
    buffer->clear();

    switch (wordSize)
    {
        case 1:
        {
            for (const auto& symbol : symbols)
            {
                buffer->push_back(symbol & 0xff);
            }
            break;
        }
        case 2:
        {
            for (const auto& symbol : symbols)
            {
                buffer->push_back(symbol & 0xff);
                buffer->push_back((symbol >> 8u) & 0xff);
            }
            break;
        }
        case 4:
        {
            for (const auto& symbol : symbols)
            {
                buffer->push_back(symbol & 0xff);
                buffer->push_back((symbol >> 8u) & 0xff);
                buffer->push_back((symbol >> 16u) & 0xff);
                buffer->push_back((symbol >> 24u) & 0xff);
            }
            break;
        }
        case 8:
        {
            for (const auto& symbol : symbols)
            {
                buffer->push_back(symbol & 0xff);
                buffer->push_back((symbol >> 8u) & 0xff);
                buffer->push_back((symbol >> 16u) & 0xff);
                buffer->push_back((symbol >> 24u) & 0xff);
                buffer->push_back((symbol >> 32u) & 0xff);
                buffer->push_back((symbol >> 40u) & 0xff);
                buffer->push_back((symbol >> 48u) & 0xff);
                buffer->push_back((symbol >> 56u) & 0xff);
            }
            break;
        }
        default:
        {
            // The default case can happen if assertions are disabled.
            // However, it should never happen and there is nothing we
            // could do. So we bluntly abort the process.
            abort();
        }
    }
}


//------------------------------------------------------------------------------

// Appends the size of a stream and the actual bytes to bytestream
void appendToBytestream(
        const std::vector<unsigned char>& bytes,
        std::vector<unsigned char> *const bytestream
){
    assert(bytestream != nullptr);

    // Append the size of 'bytes' to the bytestream
    std::vector<unsigned char> sizeBuffer;
    generateByteBuffer({static_cast<uint64_t>(bytes.size())}, 4, &sizeBuffer);
    bytestream->insert(bytestream->end(), sizeBuffer.begin(), sizeBuffer.end());

    // Append 'bytes' to the bytestream
    bytestream->insert(bytestream->end(), bytes.begin(), bytes.end());
}

//------------------------------------------------------------------------------

void doSequenceTransform(const std::vector<uint64_t>& sequence,
                         const gabac::SequenceTransformationId& transID,
                         uint64_t param,
                         const LogInfo& l,
                         std::vector<std::vector<uint64_t>> *const transformedSequences
){
    l.log(LogInfo::LogLevel::TRACE) << "Encoding sequence of length: " << sequence.size();

    auto id = unsigned(transID);
    l.log(LogInfo::LogLevel::DEBUG)
            << "Performing sequence transformation "
            << gabac::transformationInformation[id].name;

    transformedSequences->resize(gabac::transformationInformation[id].streamNames.size());
    gabac::transformationInformation[id].transform(sequence, param, transformedSequences);

    l.log(LogInfo::LogLevel::TRACE) << "Got " << transformedSequences->size() << " sequences";
    for (unsigned i = 0; i < transformedSequences->size(); ++i) {
        l.log(LogInfo::LogLevel::TRACE) << i << ": " << (*transformedSequences)[i].size() << " bytes";
    }
}

//------------------------------------------------------------------------------

void doLutTransform(bool enabled,
                    const std::vector<uint64_t>& transformedSequence,
                    unsigned int wordSize,
                    const LogInfo& l,
                    std::vector<unsigned char> *const bytestream,
                    std::vector<std::vector<uint64_t >> *const lutSequences
){
    if (!enabled) {
        l.log(LogInfo::LogLevel::TRACE) << "LUT transform *dis*abled";
        (*lutSequences)[0] = transformedSequence;
        //    appendToBytestream({}, bytestream);
        l.log(LogInfo::LogLevel::DEBUG)
                << "Got uncompressed stream after LUT: "
                << (*lutSequences)[0].size()
                << " bytes";
        l.log(LogInfo::LogLevel::DEBUG) << "Got table after LUT: " << (*lutSequences)[1].size() << " bytes";
        return;
    }

    l.log(LogInfo::LogLevel::TRACE) << "LUT transform *en*abled";
    const unsigned LUT_INDEX = 4;
    lutSequences->resize(gabac::transformationInformation[LUT_INDEX].streamNames.size());
    gabac::transformationInformation[LUT_INDEX].transform(transformedSequence, 0, lutSequences);

    l.log(LogInfo::LogLevel::DEBUG) << "Got uncompressed stream after LUT: " << (*lutSequences)[0].size() << " bytes";
    l.log(LogInfo::LogLevel::DEBUG) << "Got table after LUT: " << (*lutSequences)[1].size() << " bytes";

    // GABACIFY_LOG_DEBUG<<"lut size before coding: "<<inverseLutTmp
    auto *data = (int64_t *) (lutSequences->at(1).data());
    std::vector<unsigned char> inverseLutBitstream;
    gabac::encode_core(
            std::vector<int64_t>(data, data + (*lutSequences)[1].size()),
            gabac::BinarizationId::BI,
            {wordSize * 8},
            gabac::ContextSelectionId::bypass,
            &inverseLutBitstream
    );


    appendToBytestream(inverseLutBitstream, bytestream);
    l.log(LogInfo::LogLevel::TRACE) << "Wrote LUT bitstream with size: " << inverseLutBitstream.size();
}

//------------------------------------------------------------------------------

void doDiffTransform(bool enabled,
                     const std::vector<uint64_t>& lutTransformedSequence,
                     const LogInfo& l,
                     std::vector<int64_t> *const diffAndLutTransformedSequence
){
    // Diff coding
    if (enabled) {
        l.log(LogInfo::LogLevel::TRACE) << "Diff coding *en*abled";
        gabac::transformDiffCoding(lutTransformedSequence, diffAndLutTransformedSequence);
        l.log(LogInfo::LogLevel::DEBUG) << "Got uncompressed stream after diff: "
                                        << diffAndLutTransformedSequence->size()
                                        << " bytes";
        return;
    }

    diffAndLutTransformedSequence->reserve(lutTransformedSequence.size());

    l.log(LogInfo::LogLevel::TRACE) << "Diff coding *dis*abled";
    for (const auto& lutTransformedSymbol : lutTransformedSequence) {
        assert(lutTransformedSymbol <= std::numeric_limits<int64_t>::max());
        diffAndLutTransformedSequence->push_back(static_cast<int64_t>(lutTransformedSymbol));
    }
    l.log(LogInfo::LogLevel::DEBUG)
            << "Got uncompressed stream after diff: "
            << diffAndLutTransformedSequence->size()
            << " bytes";
}

//------------------------------------------------------------------------------

static void encodeStream(const TransformedSequenceConfiguration& conf,
                         const std::vector<int64_t>& diffAndLutTransformedSequence,
                         const LogInfo& l,
                         std::vector<uint8_t> *const bytestream
){
    // Encoding
    std::vector<unsigned char> bitstream;
    gabac::encode_core(
            diffAndLutTransformedSequence,
            conf.binarizationId,
            conf.binarizationParameters,
            conf.contextSelectionId,
            &bitstream
    );
    l.log(LogInfo::LogLevel::TRACE) << "Bitstream size: " << bitstream.size();
    appendToBytestream(bitstream, bytestream);
}

//------------------------------------------------------------------------------

static void encodeSingleSequence(const unsigned wordsize,
                                 const TransformedSequenceConfiguration& configuration,
                                 const LogInfo& l,
                                 std::vector<uint64_t> *const seq,
                                 std::vector<unsigned char> *const bytestream
){
    std::vector<std::vector<uint64_t>> lutTransformedSequences;
    lutTransformedSequences.resize(2);
    doLutTransform(
            configuration.lutTransformationEnabled,
            *seq,
            wordsize,
            l,
            bytestream,
            &lutTransformedSequences
    );
    seq->clear();
    seq->shrink_to_fit();

    std::vector<int64_t> diffAndLutTransformedSequence;
    doDiffTransform(
            configuration.diffCodingEnabled,
            lutTransformedSequences[0],
            l,
            &diffAndLutTransformedSequence
    );
    lutTransformedSequences[0].clear();
    lutTransformedSequences[0].shrink_to_fit();

    encodeStream(configuration, diffAndLutTransformedSequence, l, bytestream);
    diffAndLutTransformedSequence.clear();
    diffAndLutTransformedSequence.shrink_to_fit();
}

//------------------------------------------------------------------------------

int encode(
        const Configuration& configuration,
        const LogInfo& l,
        std::vector<uint64_t> *const sequence,
        std::vector<unsigned char> *const bytestream
){


    std::vector<std::vector<uint64_t>> transformedSequences;
    doSequenceTransform(
            *sequence,
            configuration.sequenceTransformationId,
            configuration.sequenceTransformationParameter,
            l,
            &transformedSequences
    );
    sequence->clear();
    sequence->shrink_to_fit();
    std::vector<unsigned> wordsizes = gabac::fixWordSizes(
            gabac::transformationInformation[unsigned(configuration.sequenceTransformationId)].wordsizes,
            configuration.wordSize
    );

    // Loop through the transformed sequences
    for (size_t i = 0; i < transformedSequences.size(); i++) {
        encodeSingleSequence(
                wordsizes[i],
                configuration.transformedSequenceConfigurations.at(i),
                l,
                &(transformedSequences[i]),
                bytestream
        );
        transformedSequences[i].clear();
        transformedSequences[i].shrink_to_fit();
    }

    return GABAC_SUCCESS;
}

//------------------------------------------------------------------------------

}  // namespace gabac

//------------------------------------------------------------------------------