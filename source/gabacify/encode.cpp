#include "gabacify/encode.h"

#include <algorithm>
#include <cassert>
#include <functional>
#include <iomanip>
#include <limits>
#include <utility>
#include <vector>

#include "gabac/configuration.h"
#include "gabac/encoding.h"

#include "gabacify/analysis.h"
#include "gabacify/input_file.h"
#include "gabacify/helpers.h"
#include "gabacify/output_file.h"


namespace gabacify {

//------------------------------------------------------------------------------

void encode_plain(const std::string& inputFilePath,
                  const std::string& configurationFilePath,
                  const std::string& outputFilePath,
                  const gabac::LogInfo& l
){
    // Read in the entire input file
    InputFile inputFile(inputFilePath);
    std::vector<unsigned char> buffer(inputFile.size());
    inputFile.read(&buffer[0], 1, buffer.size());
    // Read the entire configuration file as a string and convert the JSON
    // input string to the internal GABAC configuration
    InputFile configurationFile(configurationFilePath);
    std::string jsonInput("\0", configurationFile.size());
    configurationFile.read(&jsonInput[0], 1, jsonInput.size());
    gabac::Configuration configuration(jsonInput);

    // Generate symbol stream from byte buffer
    std::vector<uint64_t> symbols;
    generateSymbolStream(buffer, configuration.wordSize, &symbols);
    buffer.clear();
    buffer.shrink_to_fit();

    gabac::encode(configuration, l, &symbols, &buffer);
    symbols.clear();
    symbols.shrink_to_fit();

    // Write the bytestream
    OutputFile outputFile(outputFilePath);
    outputFile.write(&buffer[0], 1, buffer.size());
    GABACIFY_LOG_INFO << "Wrote bytestream of size " << buffer.size() << " to: " << outputFilePath;
    buffer.clear();
    buffer.shrink_to_fit();
}

//------------------------------------------------------------------------------

void encode(
        const std::string& inputFilePath,
        bool analyze,
        const std::string& configurationFilePath,
        const std::string& outputFilePath,
        const gabac::LogInfo& l
){
    assert(!inputFilePath.empty());
    assert(!configurationFilePath.empty());
    assert(!outputFilePath.empty());

    if (analyze)
    {
        encode_analyze(inputFilePath, configurationFilePath, outputFilePath, l);
        return;
    }
    encode_plain(inputFilePath, configurationFilePath, outputFilePath, l);
}

//------------------------------------------------------------------------------

}  // namespace gabacify

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
