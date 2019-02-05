#include "gabacify/analysis.h"

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
#include "gabac/analyze.h"
#include "gabacify/exceptions.h"
#include "gabacify/helpers.h"
#include "gabacify/log.h"
#include "output_file.h"
#include "input_file.h"


namespace gabacify {

static const gabac::CandidateConfig& getCandidateConfig(){
    static const gabac::CandidateConfig config = {
            { // Wordsizes
                    1,
                       4
            },
            { // Sequence Transformations
                    gabac::SequenceTransformationId::no_transform,
                       gabac::SequenceTransformationId::equality_coding,
                          gabac::SequenceTransformationId::match_coding,
                             gabac::SequenceTransformationId::rle_coding
            },
            { // Match coding window sizes
                    32,
                       256
            },
            { // RLE Guard
                    255
            },
            { // LUT transform
                    false,
                       true
            },
            { // Diff transform
                    false//,
                    //true
            },
            { // Binarizations (unsigned)
                    gabac::BinarizationId::BI,
                       gabac::BinarizationId::TU,
                          gabac::BinarizationId::EG,
                             gabac::BinarizationId::TEG
            },
            { // Binarizations (signed)
                    gabac::BinarizationId::SEG,
                       gabac::BinarizationId::STEG
            },
            { // Binarization parameters (TEG and STEG only)
                    1, 2, 3, 5, 7, 9,
                    15, 30, 255
            },
            { // Context modes
                    // gabac::ContextSelectionId::bypass,
                    gabac::ContextSelectionId::adaptive_coding_order_0,
                       gabac::ContextSelectionId::adaptive_coding_order_1,
                          gabac::ContextSelectionId::adaptive_coding_order_2
            }
    };
    return config;
}

//------------------------------------------------------------------------------

void encode_analyze(const std::string& inputFilePath,
                    const std::string& configurationFilePath,
                    const std::string& outputFilePath,
                    const gabac::LogInfo& l
){
    gabac::Configuration bestConfig;
    std::vector<unsigned char> bestByteStream;
    for (const auto& w : getCandidateConfig().candidateWordsizes)
    {

        InputFile inputFile(inputFilePath);

        if (inputFile.size() % w != 0)
        {
            GABACIFY_LOG_INFO << "Input stream size "
                              << inputFile.size()
                              << " is not a multiple of word size "
                              << w
                              << "! Skipping word size.";
            continue;
        }

        std::vector<unsigned char> buffer(inputFile.size());
        inputFile.read(&buffer[0], 1, buffer.size());

        gabac::Configuration currentConfig;

        currentConfig.wordSize = w;

        // Generate symbol stream from byte buffer
        std::vector<uint64_t> symbols;
        generateSymbolStream(buffer, w, &symbols);
        buffer.clear();
        buffer.shrink_to_fit();


        analyze(symbols, l, getCandidateConfig(), &bestByteStream, &bestConfig, &currentConfig);

        if (bestByteStream.empty())
        {
            GABACIFY_DIE("NO CONFIG FOUND");
        }
    }

    // Write the smallest bytestream
    OutputFile outputFile(outputFilePath);
    outputFile.write(&bestByteStream[0], 1, bestByteStream.size());
    GABACIFY_LOG_INFO << "Wrote smallest bytestream of size "
                      << bestByteStream.size()
                      << " to: "
                      << outputFilePath;

    // Write the best gabac::Configuration as JSON
    std::string jsonString = bestConfig.toJsonString();
    OutputFile configurationFile(configurationFilePath);
    configurationFile.write(&jsonString[0], 1, jsonString.size());
    GABACIFY_LOG_DEBUG << "with gabac::Configuration: \n"
                       << bestConfig.toPrintableString();
    GABACIFY_LOG_INFO << "Wrote best gabac::Configuration to: " << configurationFilePath;
}

//------------------------------------------------------------------------------

}  // namespace gabacify

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
