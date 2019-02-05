#ifndef GABACIFY_DECODE_H_
#define GABACIFY_DECODE_H_


#include <string>
#include <gabac/configuration.h>


namespace gabacify {


void decode(
        const std::string& inputFilePath,
        const std::string& configurationFilePath,
        const std::string& outputFilePath,
        const gabac::LogInfo& level
);


}  // namespace gabacify


#endif  // GABACIFY_DECODE_H_
