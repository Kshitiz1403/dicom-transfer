#include "cli_parser.h"
#include <iostream>
#include <thread>

CliParser::CliParser(int argc, char* argv[]) 
    : m_mode(CommandMode::NONE),
      m_threadCount(std::thread::hardware_concurrency()),
      m_verbose(false),
      m_valid(false) {
    
    m_valid = parseArgs(argc, argv);
}

bool CliParser::parseArgs(int argc, char* argv[]) {
    if (argc < 2) {
        m_errorMessage = "Not enough arguments provided";
        printUsage();
        return false;
    }
    
    std::string arg1 = argv[1];
    
    if (arg1 == "--upload") {
        m_mode = CommandMode::UPLOAD;
        
        if (argc < 3) {
            m_errorMessage = "Upload mode requires source folder path";
            printUsage();
            return false;
        }
        
        m_sourcePath = argv[2];
    }
    else if (arg1 == "--download") {
        m_mode = CommandMode::DOWNLOAD;
        
        if (argc < 3) {
            m_errorMessage = "Download mode requires study UID";
            printUsage();
            return false;
        }
        
        m_studyUid = argv[2];
        
        // Check for output folder flag
        bool outputFolderProvided = false;
        for (int i = 3; i < argc; i++) {
            std::string arg = argv[i];
            
            if (arg == "--output") {
                if (i + 1 < argc) {
                    m_outputPath = argv[i + 1];
                    outputFolderProvided = true;
                    i++; // Skip the next argument as it's the output path
                } else {
                    m_errorMessage = "Output flag requires a path";
                    printUsage();
                    return false;
                }
            }
        }
        
        if (!outputFolderProvided) {
            m_errorMessage = "Download mode requires --output flag with path";
            printUsage();
            return false;
        }
    }
    else if (arg1 == "--help" || arg1 == "-h") {
        printUsage();
        return false;
    }
    else {
        m_errorMessage = "Invalid command: " + arg1;
        printUsage();
        return false;
    }
    
    // Parse additional options
    for (int i = 3; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "--threads") {
            if (i + 1 < argc) {
                try {
                    m_threadCount = std::stoi(argv[i + 1]);
                    if (m_threadCount <= 0) {
                        m_threadCount = std::thread::hardware_concurrency();
                    }
                } catch (...) {
                    m_errorMessage = "Invalid thread count";
                    return false;
                }
                i++; // Skip the next argument as it's the thread count
            } else {
                m_errorMessage = "Thread flag requires a number";
                return false;
            }
        }
        else if (arg == "--verbose" || arg == "-v") {
            m_verbose = true;
        }
        else if (arg == "--output") {
            // Already handled for download mode
            if (m_mode != CommandMode::DOWNLOAD) {
                m_errorMessage = "Output flag is only valid in download mode";
                return false;
            }
            i++; // Skip the next argument as it's the output path
        }
        else if (arg.substr(0, 2) == "--") {
            m_errorMessage = "Unknown option: " + arg;
            return false;
        }
    }
    
    return true;
}

void CliParser::printUsage() const {
    std::cout << "DICOM Transfer Utility" << std::endl;
    std::cout << "Usage:" << std::endl;
    std::cout << "  dicom_transfer --upload <path-to-folder> [options]" << std::endl;
    std::cout << "  dicom_transfer --download <study-uid> --output <path-to-folder> [options]" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --threads <count>    Number of threads to use (default: " 
              << std::thread::hardware_concurrency() << ")" << std::endl;
    std::cout << "  --verbose, -v        Enable verbose logging" << std::endl;
    std::cout << "  --help, -h           Display this help message" << std::endl;
}

bool CliParser::isValid() const {
    return m_valid;
}

std::string CliParser::getErrorMessage() const {
    return m_errorMessage;
}

CommandMode CliParser::getMode() const {
    return m_mode;
}

std::string CliParser::getSourcePath() const {
    return m_sourcePath;
}

std::string CliParser::getOutputPath() const {
    return m_outputPath;
}

std::string CliParser::getStudyUid() const {
    return m_studyUid;
}

int CliParser::getThreadCount() const {
    return m_threadCount;
}

bool CliParser::isVerbose() const {
    return m_verbose;
} 