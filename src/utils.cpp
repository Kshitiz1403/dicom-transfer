#include "utils.h"
#include "logger.h"

#include <algorithm>
#include <sstream>
#include <random>
#include <iomanip>
#include <fstream>
#include <sys/stat.h>

// File system operations
bool Utils::createDirectoryIfNotExists(const std::string& path) {
    try {
        if (!fs::exists(path)) {
            return fs::create_directories(path);
        }
        return true;
    } catch (const fs::filesystem_error& e) {
        LOG_ERROR("Failed to create directory: " + path + " - " + e.what());
        return false;
    }
}

bool Utils::fileExists(const std::string& filepath) {
    return fs::exists(filepath);
}

bool Utils::isDirectory(const std::string& path) {
    return fs::is_directory(path);
}

std::vector<std::string> Utils::listFilesInDirectory(const std::string& dirPath, bool recursive) {
    std::vector<std::string> files;
    
    try {
        if (recursive) {
            for (const auto& entry : fs::recursive_directory_iterator(dirPath)) {
                if (entry.is_regular_file()) {
                    files.push_back(entry.path().string());
                }
            }
        } else {
            for (const auto& entry : fs::directory_iterator(dirPath)) {
                if (entry.is_regular_file()) {
                    files.push_back(entry.path().string());
                }
            }
        }
    } catch (const fs::filesystem_error& e) {
        LOG_ERROR("Error listing files in directory: " + dirPath + " - " + e.what());
    }
    
    return files;
}

std::string Utils::getFileExtension(const std::string& filepath) {
    return fs::path(filepath).extension().string();
}

std::string Utils::getFileName(const std::string& filepath) {
    return fs::path(filepath).filename().string();
}

std::string Utils::getParentPath(const std::string& filepath) {
    return fs::path(filepath).parent_path().string();
}

size_t Utils::getFileSize(const std::string& filepath) {
    try {
        return fs::file_size(filepath);
    } catch (const fs::filesystem_error& e) {
        LOG_ERROR("Error getting file size: " + filepath + " - " + e.what());
        return 0;
    }
}

bool Utils::deleteFile(const std::string& filepath) {
    try {
        return fs::remove(filepath);
    } catch (const fs::filesystem_error& e) {
        LOG_ERROR("Error deleting file: " + filepath + " - " + e.what());
        return false;
    }
}

// String operations
std::string Utils::trim(const std::string& str) {
    auto start = str.find_first_not_of(" \t\n\r\f\v");
    if (start == std::string::npos) {
        return "";
    }
    
    auto end = str.find_last_not_of(" \t\n\r\f\v");
    return str.substr(start, end - start + 1);
}

std::vector<std::string> Utils::split(const std::string& str, char delimiter) {
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string token;
    
    while (std::getline(ss, token, delimiter)) {
        tokens.push_back(token);
    }
    
    return tokens;
}

std::string Utils::generateUuid() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    static std::uniform_int_distribution<> dis2(8, 11);
    
    std::stringstream ss;
    ss << std::hex;
    
    for (int i = 0; i < 8; i++) {
        ss << dis(gen);
    }
    ss << "-";
    
    for (int i = 0; i < 4; i++) {
        ss << dis(gen);
    }
    ss << "-4"; // Version 4 UUID
    
    for (int i = 0; i < 3; i++) {
        ss << dis(gen);
    }
    ss << "-";
    
    ss << dis2(gen);
    for (int i = 0; i < 3; i++) {
        ss << dis(gen);
    }
    ss << "-";
    
    for (int i = 0; i < 12; i++) {
        ss << dis(gen);
    }
    
    return ss.str();
}

std::string Utils::bytesToHumanReadable(size_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unitIndex = 0;
    double size = static_cast<double>(bytes);
    
    while (size >= 1024.0 && unitIndex < 4) {
        size /= 1024.0;
        unitIndex++;
    }
    
    std::stringstream ss;
    ss << std::fixed << std::setprecision(2) << size << " " << units[unitIndex];
    return ss.str();
}

// Path operations
std::string Utils::joinPath(const std::string& base, const std::string& relative) {
    fs::path basePath(base);
    fs::path relativePath(relative);
    return (basePath / relativePath).string();
}

std::string Utils::normalizePath(const std::string& path) {
    return fs::path(path).lexically_normal().string();
}

// S3 key generation
std::string Utils::generateS3Key(const std::string& studyUid, const std::string& filepath) {
    std::string filename = getFileName(filepath);
    return "studies/" + studyUid + "/" + filename;
} 