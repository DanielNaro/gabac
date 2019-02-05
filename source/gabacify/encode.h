#ifndef GABACIFY_ENCODE_H_
#define GABACIFY_ENCODE_H_

#include <functional>
#include <string>
#include <vector>
#include <gabac/constants.h>
#include "gabac/configuration.h"

namespace gabacify {


void encode(
        const std::string& inputFilePath,
        bool analyze,
        const std::string& configurationFilePath,
        const std::string& outputFilePath,
        const gabac::LogInfo& l
);


}  // namespace gabacify


#endif  // GABACIFY_ENCODE_H_
