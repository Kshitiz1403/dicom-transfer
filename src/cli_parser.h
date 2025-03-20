#pragma once

#include <string>
#include <vector>
#include <unordered_map>

enum class CommandMode {
    NONE,
    UPLOAD,
    DOWNLOAD
};

class CliParser {
public:
    CliParser(int argc, char* argv[]);
    
    bool isValid() const;
    std::string getErrorMessage() const;
    
    CommandMode getMode() const;
    std::string getSourcePath() const;
    std::string getOutputPath() const;
    std::string getStudyUid() const;
    
    // Additional options
    int getThreadCount() const;
    bool isVerbose() const;
    
private:
    bool parseArgs(int argc, char* argv[]);
    void printUsage() const;
    
    CommandMode m_mode;
    std::string m_sourcePath;
    std::string m_outputPath;
    std::string m_studyUid;
    
    int m_threadCount;
    bool m_verbose;
    
    bool m_valid;
    std::string m_errorMessage;
}; 