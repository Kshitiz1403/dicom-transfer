#pragma once

#include <string>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

namespace Utils {
    // File system operations
    bool createDirectoryIfNotExists(const std::string& path);
    bool fileExists(const std::string& filepath);
    bool isDirectory(const std::string& path);
    std::vector<std::string> listFilesInDirectory(const std::string& dirPath, bool recursive = false);
    std::string getFileExtension(const std::string& filepath);
    std::string getFileName(const std::string& filepath);
    std::string getParentPath(const std::string& filepath);
    size_t getFileSize(const std::string& filepath);
    bool deleteFile(const std::string& filepath);
    
    // String operations
    std::string trim(const std::string& str);
    std::vector<std::string> split(const std::string& str, char delimiter);
    std::string generateUuid();
    std::string bytesToHumanReadable(size_t bytes);
    
    // Path operations
    std::string joinPath(const std::string& base, const std::string& relative);
    std::string normalizePath(const std::string& path);
    
    // S3 key generation
    std::string generateS3Key(const std::string& studyUid, const std::string& filepath);
} 