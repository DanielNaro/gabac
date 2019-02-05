#ifndef GABACIFY_CONFIGURATION_H_
#define GABACIFY_CONFIGURATION_H_

#include <ostream>
#include <string>
#include <vector>

#include "gabac/constants.h"


namespace gabac {


struct TransformedSequenceConfiguration
{
    bool lutTransformationEnabled;
    unsigned int lutTransformationParameter;
    bool diffCodingEnabled;
    gabac::BinarizationId binarizationId;
    std::vector<unsigned int> binarizationParameters;
    gabac::ContextSelectionId contextSelectionId;

    std::string toPrintableString() const;
};

class NullBuffer : public std::streambuf
{
 public:
    int overflow(int c){
        return c;
    }
};

class NullStream : public std::ostream
{
 public:
    NullStream() : std::ostream(&m_sb){
    }

 private:
    NullBuffer m_sb;
};

struct LogInfo {
    std::ostream *outStream;

    enum class LogLevel
    {
        TRACE,
        DEBUG,
        INFO,
        WARNING,
        ERROR,
        FATAL
    };

    LogLevel level;

    std::ostream& log(const LogLevel& l) const{
        static NullStream nullstr;
        if (int(l) >= int(level)){
            return *outStream;
        }
        return nullstr;
    }
};


class Configuration
{
 public:
    Configuration();

    explicit Configuration(
            const std::string& json
    );

    ~Configuration();

    std::string toJsonString() const;

    std::string toPrintableString() const;

    unsigned int wordSize;
    gabac::SequenceTransformationId sequenceTransformationId;
    unsigned int sequenceTransformationParameter;
    std::vector<TransformedSequenceConfiguration> transformedSequenceConfigurations;
};


}  // namespace gabacify


#endif  // GABACIFY_CONFIGURATION_H_
