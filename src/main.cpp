#include "cli_parser.h"
#include "dicom_processor.h"
#include "s3_manager.h"
#include "dynamodb_manager.h"
#include "thread_pool.h"
#include "logger.h"
#include "profiler.h"
#include "utils.h"

#include <iostream>
#include <string>
#include <vector>
#include <future>
#include <mutex>
#include <map>
#include <filesystem>

namespace fs = std::filesystem;

// AWS S3 bucket and DynamoDB table names
const std::string S3_BUCKET_NAME = "dicom-transfer-bucket";
const std::string DYNAMODB_TABLE_NAME = "dicom-studies";
const std::string AWS_REGION = "ap-south-1";

// Forward declarations
bool uploadMode(const std::string& sourcePath, int threadCount);
bool downloadMode(const std::string& studyUid, const std::string& outputPath, int threadCount);

int main(int argc, char* argv[]) {
    // Parse command-line arguments
    CliParser parser(argc, argv);
    
    if (!parser.isValid()) {
        std::cerr << parser.getErrorMessage() << std::endl;
        return 1;
    }
    
    // Set up logging
    Logger::getInstance().setLogFile("dicom_transfer.log");
    if (parser.isVerbose()) {
        Logger::getInstance().setLogLevel(LogLevel::DEBUG);
    }
    
    // Initialize AWS SDK
    if (!S3Manager::initializeAWS()) {
        LOG_ERROR("Failed to initialize AWS SDK");
        return 1;
    }
    
    bool success = false;
    
    // Start profiling
    Profiler::getInstance().startOperation("Total Execution");
    
    try {
        // Execute the appropriate mode
        switch (parser.getMode()) {
            case CommandMode::UPLOAD:
                success = uploadMode(parser.getSourcePath(), parser.getThreadCount());
                break;
                
            case CommandMode::DOWNLOAD:
                success = downloadMode(parser.getStudyUid(), parser.getOutputPath(), parser.getThreadCount());
                break;
                
            default:
                LOG_ERROR("Invalid command mode");
                success = false;
                break;
        }
    }
    catch (const std::exception& e) {
        LOG_ERROR("Exception in main: " + std::string(e.what()));
        success = false;
    }
    
    // End profiling and log results
    Profiler::getInstance().endOperation("Total Execution");
    LOG_INFO("\n" + Profiler::getInstance().generateReport());
    
    // Shut down AWS SDK
    S3Manager::shutdownAWS();
    
    return success ? 0 : 1;
}

bool uploadMode(const std::string& sourcePath, int threadCount) {
    LOG_INFO("Starting upload mode with source path: " + sourcePath);
    LOG_INFO("Using " + std::to_string(threadCount) + " threads");
    
    // Check if source path exists and is a directory
    if (!Utils::isDirectory(sourcePath)) {
        LOG_ERROR("Source path is not a valid directory: " + sourcePath);
        return false;
    }
    
    // Initialize components
    S3Manager s3Manager(AWS_REGION);
    DynamoDBManager dbManager(AWS_REGION);
    DicomProcessor dicomProcessor;
    ThreadPool threadPool(threadCount);
    
    // Create vector to store all upload results
    std::vector<std::future<bool>> studyUploadResults;
    
    // List all files in the source directory
    std::vector<std::string> allFiles = Utils::listFilesInDirectory(sourcePath, true);
    LOG_INFO("Found " + std::to_string(allFiles.size()) + " files to process");
    
    // Filter and group DICOM files by study
    std::vector<std::string> dicomFiles;
    for (const auto& file : allFiles) {
        if (dicomProcessor.isDicomFile(file)) {
            dicomFiles.push_back(file);
        }
    }
    LOG_INFO("Found " + std::to_string(dicomFiles.size()) + " DICOM files");
    
    auto studyGroups = dicomProcessor.groupFilesByStudy(dicomFiles);
    LOG_INFO("Grouped into " + std::to_string(studyGroups.size()) + " studies");

    // Process each study
    for (const auto& [studyUid, studyFiles] : studyGroups) {
        // Submit study processing task to thread pool
        studyUploadResults.push_back(
            threadPool.enqueue([&, studyUid, studyFiles]() {
                LOG_INFO("Processing study: " + studyUid + " with " + 
                         std::to_string(studyFiles.size()) + " files");
                
                // Process metadata first
                Json::Value metadata;
                if (!dicomProcessor.extractMetadata(studyFiles[0], metadata)) {
                    LOG_ERROR("Failed to extract metadata for study: " + studyUid);
                    return false;
                }
                
                if (!dbManager.storeStudyMetadata(DYNAMODB_TABLE_NAME, studyUid, metadata)) {
                    LOG_ERROR("Failed to store metadata in DynamoDB for study: " + studyUid);
                    return false;
                }

                // Create a separate thread pool for file uploads within this study
                ThreadPool fileUploadPool(std::min(threadCount, 4));  // Limit concurrent uploads per study
                std::vector<std::future<bool>> fileUploadResults;
                
                // Upload each file in the study
                for (const auto& file : studyFiles) {
                    std::string s3Key = Utils::generateS3Key(studyUid, file);
                    fileUploadResults.push_back(
                        fileUploadPool.enqueue([&, file, s3Key]() {
                            if (!s3Manager.uploadFile(S3_BUCKET_NAME, file, s3Key)) {
                                LOG_ERROR("Failed to upload file: " + file);
                                return false;
                            }
                            if (!dbManager.storeFileLocation(DYNAMODB_TABLE_NAME, studyUid, s3Key)) {
                                LOG_ERROR("Failed to store file location: " + s3Key);
                                return false;
                            }
                            LOG_DEBUG("Successfully uploaded: " + file);
                            return true;
                        })
                    );
                }
                
                // Wait for all file uploads in this study to complete
                bool allFilesUploaded = true;
                for (auto& future : fileUploadResults) {
                    if (!future.get()) {
                        allFilesUploaded = false;
                        LOG_ERROR("One or more files failed to upload in study: " + studyUid);
                    }
                }
                
                return allFilesUploaded;
            })
        );
    }
    
    // Wait for all studies to complete
    bool success = true;
    for (auto& future : studyUploadResults) {
        if (!future.get()) {
            success = false;
            LOG_ERROR("One or more studies failed to process");
        }
    }
    
    return success;
}

bool downloadMode(const std::string& studyUid, const std::string& outputPath, int threadCount) {
    LOG_INFO("Starting download mode for study: " + studyUid);
    LOG_INFO("Output path: " + outputPath);
    LOG_INFO("Using " + std::to_string(threadCount) + " threads");
    
    // Create output directory if it doesn't exist
    if (!Utils::createDirectoryIfNotExists(outputPath)) {
        LOG_ERROR("Failed to create output directory: " + outputPath);
        return false;
    }
    
    // Create instances of required services
    S3Manager s3Manager(AWS_REGION);
    DynamoDBManager dbManager(AWS_REGION);
    ThreadPool threadPool(threadCount);
    
    // Retrieve metadata from DynamoDB
    Json::Value studyMetadata;
    if (!dbManager.getStudyMetadata(DYNAMODB_TABLE_NAME, studyUid, studyMetadata)) {
        LOG_ERROR("Failed to retrieve metadata for study: " + studyUid);
        return false;
    }
    
    LOG_INFO("Retrieved metadata for study: " + studyUid);
    
    // Get all file locations for the study
    std::vector<std::string> fileLocations = dbManager.getFileLocations(DYNAMODB_TABLE_NAME, studyUid);
    
    if (fileLocations.empty()) {
        LOG_ERROR("No files found for study: " + studyUid);
        return false;
    }
    
    LOG_INFO("Found " + std::to_string(fileLocations.size()) + " files for study: " + studyUid);
    
    // Download all files for the study
    std::vector<std::future<bool>> downloadResults;
    
    for (const auto& s3Key : fileLocations) {
        downloadResults.push_back(
            threadPool.enqueue([&, s3Key]() {
                // Generate local file path
                std::string filename = Utils::getFileName(s3Key);
                std::string localFilePath = Utils::joinPath(outputPath, filename);
                
                // Download the file from S3
                Profiler::getInstance().startOperation("S3 Download");
                
                bool downloadSuccess = s3Manager.downloadFile(
                    S3_BUCKET_NAME, 
                    s3Key, 
                    localFilePath,
                    [](size_t bytes) {
                        Profiler::getInstance().logTransferSize("S3 Download", bytes);
                    }
                );
                
                Profiler::getInstance().endOperation("S3 Download");
                
                if (!downloadSuccess) {
                    LOG_ERROR("Failed to download file from S3: " + s3Key);
                    return false;
                }
                
                LOG_INFO("Successfully downloaded file: " + s3Key);
                return true;
            })
        );
    }
    
    // Wait for all downloads to complete
    bool allFilesDownloaded = true;
    for (auto& future : downloadResults) {
        allFilesDownloaded &= future.get();
    }
    
    if (allFilesDownloaded) {
        LOG_INFO("Download mode completed successfully");
        return true;
    } else {
        LOG_ERROR("Download mode completed with errors");
        return false;
    }
} 