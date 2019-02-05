#include "gabacify/decode.h"

#include <cassert>
#include <vector>
#include <gabac/configuration.h>

#include "gabac/constants.h"
#include "gabac/diff_coding.h"
#include "gabac/decoding.h"

#include "gabac/configuration.h"
#include "gabacify/exceptions.h"
#include "gabacify/helpers.h"
#include "gabacify/input_file.h"
#include "gabacify/log.h"
#include "gabacify/output_file.h"


namespace gabacify {

//------------------------------------------------------------------------------

void decode(
        const std::string& inputFilePath,
        const std::string& configurationFilePath,
        const std::string& outputFilePath,
        const gabac::LogInfo& l
){
    assert(!inputFilePath.empty());
    assert(!configurationFilePath.empty());
    assert(!outputFilePath.empty());

    // Read in the entire input file
    InputFile inputFile(inputFilePath);
    size_t bytestreamSize = inputFile.size();
    std::vector<unsigned char> bytestream(bytestreamSize);
    inputFile.read(&bytestream[0], 1, bytestreamSize);

    // Read the entire configuration file as a string and convert the JSON
    // input string to the internal GABAC configuration
    InputFile configurationFile(configurationFilePath);
    std::string jsonInput("\0", configurationFile.size());
    configurationFile.read(&jsonInput[0], 1, jsonInput.size());
    gabac::Configuration configuration(jsonInput);

    // Decode with the given configuration
    std::vector<uint64_t> symbols;
    gabac::decode(&bytestream, configuration, l,  &symbols);

    // Generate byte buffer from symbol stream
    std::vector<unsigned char> buffer;
    gabac::generateByteBuffer(symbols, configuration.wordSize, &buffer);
    symbols.clear();
    symbols.shrink_to_fit();

    // Write the bytestream
    OutputFile outputFile(outputFilePath);
    outputFile.write(&buffer[0], 1, buffer.size());
    GABACIFY_LOG_INFO << "Wrote buffer of size " << buffer.size() << " to: " << outputFilePath;
}

//------------------------------------------------------------------------------

}  // namespace gabacify

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------