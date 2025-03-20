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
const std::string AWS_REGION = "us-east-1";

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
    
    // Create instances of required services
    S3Manager s3Manager(AWS_REGION);
    DynamoDBManager dbManager(AWS_REGION);
    DicomProcessor dicomProcessor;
    ThreadPool threadPool(threadCount);
    
    // List all files in the source directory
    std::vector<std::string> allFiles = Utils::listFilesInDirectory(sourcePath, true);
    LOG_INFO("Found " + std::to_string(allFiles.size()) + " files in source directory");
    
    // Identify DICOM files
    std::vector<std::string> dicomFiles;
    std::vector<std::string> nonDicomFiles;
    
    Profiler::getInstance().startOperation("DICOM Identification");
    for (const auto& filepath : allFiles) {
        if (dicomProcessor.isDicomFile(filepath)) {
            dicomFiles.push_back(filepath);
        } else {
            nonDicomFiles.push_back(filepath);
        }
    }
    Profiler::getInstance().endOperation("DICOM Identification");
    
    LOG_INFO("Found " + std::to_string(dicomFiles.size()) + " DICOM files and " + 
             std::to_string(nonDicomFiles.size()) + " non-DICOM files");
    
    // Group DICOM files by study
    auto studyGroups = dicomProcessor.groupFilesByStudy(dicomFiles);
    LOG_INFO("Grouped DICOM files into " + std::to_string(studyGroups.size()) + " studies");
    
    // Process each study
    std::vector<std::future<bool>> studyUploadResults;
    
    for (const auto& [studyUid, studyFiles] : studyGroups) {
        studyUploadResults.push_back(
            threadPool.enqueue([&, studyUid, studyFiles]() {
                LOG_INFO("Processing study: " + studyUid + " with " + 
                         std::to_string(studyFiles.size()) + " files");
                
                // Create temp directory for JSON metadata
                std::string tempDir = Utils::joinPath(sourcePath, "temp_" + studyUid);
                if (!Utils::createDirectoryIfNotExists(tempDir)) {
                    LOG_ERROR("Failed to create temp directory for study: " + studyUid);
                    return false;
                }
                
                // Generate JSON metadata file
                std::string jsonFilePath = Utils::joinPath(tempDir, studyUid + ".json");
                if (!dicomProcessor.generateMetadataJson(studyFiles, jsonFilePath)) {
                    LOG_ERROR("Failed to generate metadata JSON for study: " + studyUid);
                    return false;
                }
                
                // Store metadata in DynamoDB
                Json::Value metadata;
                {
                    std::ifstream jsonFile(jsonFilePath);
                    Json::Reader reader;
                    reader.parse(jsonFile, metadata);
                }
                
                if (!dbManager.storeStudyMetadata(DYNAMODB_TABLE_NAME, studyUid, metadata)) {
                    LOG_ERROR("Failed to store metadata in DynamoDB for study: " + studyUid);
                    return false;
                }
                
                // Upload each file in the study to S3
                std::vector<std::future<bool>> fileUploadResults;
                
                // First upload the metadata JSON file
                std::string jsonS3Key = "studies/" + studyUid + "/" + Utils::getFileName(jsonFilePath);
                fileUploadResults.push_back(
                    threadPool.enqueue([&, jsonFilePath, jsonS3Key]() {
                        Profiler::getInstance().startOperation("S3 Upload");
                        
                        bool uploadSuccess = s3Manager.uploadFile(
                            S3_BUCKET_NAME,
                            jsonFilePath,
                            jsonS3Key,
                            [](size_t bytes) {
                                Profiler::getInstance().logTransferSize("S3 Upload", bytes);
                            }
                        );
                        
                        Profiler::getInstance().endOperation("S3 Upload");
                        
                        if (uploadSuccess) {
                            if (!dbManager.storeFileLocation(DYNAMODB_TABLE_NAME, studyUid, jsonS3Key)) {
                                LOG_ERROR("Failed to store file location in DynamoDB: " + jsonS3Key);
                                return false;
                            }
                            
                            LOG_INFO("Successfully uploaded metadata JSON: " + jsonS3Key);
                            return true;
                        } else {
                            LOG_ERROR("Failed to upload metadata JSON: " + jsonFilePath);
                            return false;
                        }
                    })
                );
                
                // Then upload all DICOM files
                for (const auto& filepath : studyFiles) {
                    std::string s3Key = Utils::generateS3Key(studyUid, filepath);
                    
                    fileUploadResults.push_back(
                        threadPool.enqueue([&, filepath, s3Key]() {
                            Profiler::getInstance().startOperation("S3 Upload");
                            
                            bool uploadSuccess = s3Manager.uploadFile(
                                S3_BUCKET_NAME,
                                filepath,
                                s3Key,
                                [](size_t bytes) {
                                    Profiler::getInstance().logTransferSize("S3 Upload", bytes);
                                }
                            );
                            
                            Profiler::getInstance().endOperation("S3 Upload");
                            
                            if (uploadSuccess) {
                                if (!dbManager.storeFileLocation(DYNAMODB_TABLE_NAME, studyUid, s3Key)) {
                                    LOG_ERROR("Failed to store file location in DynamoDB: " + s3Key);
                                    return false;
                                }
                                
                                // Successfully uploaded, now delete the local file
                                if (Utils::deleteFile(filepath)) {
                                    LOG_INFO("Successfully uploaded and deleted local file: " + filepath);
                                } else {
                                    LOG_WARNING("Uploaded file but failed to delete local file: " + filepath);
                                }
                                
                                return true;
                            } else {
                                LOG_ERROR("Failed to upload file: " + filepath);
                                return false;
                            }
                        })
                    );
                }
                
                // Wait for all file uploads to complete
                bool allFilesUploaded = true;
                for (auto& future : fileUploadResults) {
                    allFilesUploaded &= future.get();
                }
                
                // Clean up temp directory
                if (Utils::fileExists(jsonFilePath)) {
                    Utils::deleteFile(jsonFilePath);
                }
                fs::remove(tempDir);
                
                return allFilesUploaded;
            })
        );
    }
    
    // Process non-DICOM files
    std::vector<std::future<bool>> nonDicomUploadResults;
    
    // Use "other" as the root folder for non-DICOM files
    const std::string otherFolderId = "other_" + Utils::generateUuid();
    
    for (const auto& filepath : nonDicomFiles) {
        std::string s3Key = "other/" + otherFolderId + "/" + Utils::getFileName(filepath);
        
        nonDicomUploadResults.push_back(
            threadPool.enqueue([&, filepath, s3Key]() {
                Profiler::getInstance().startOperation("S3 Upload Non-DICOM");
                
                bool uploadSuccess = s3Manager.uploadFile(
                    S3_BUCKET_NAME,
                    filepath,
                    s3Key,
                    [](size_t bytes) {
                        Profiler::getInstance().logTransferSize("S3 Upload Non-DICOM", bytes);
                    }
                );
                
                Profiler::getInstance().endOperation("S3 Upload Non-DICOM");
                
                if (uploadSuccess) {
                    // No need to store locations in DynamoDB for non-DICOM files
                    
                    // Successfully uploaded, now delete the local file
                    if (Utils::deleteFile(filepath)) {
                        LOG_INFO("Successfully uploaded and deleted local non-DICOM file: " + filepath);
                    } else {
                        LOG_WARNING("Uploaded but failed to delete local non-DICOM file: " + filepath);
                    }
                    
                    return true;
                } else {
                    LOG_ERROR("Failed to upload non-DICOM file: " + filepath);
                    return false;
                }
            })
        );
    }
    
    // Wait for all study uploads to complete
    bool allStudiesUploaded = true;
    for (auto& future : studyUploadResults) {
        allStudiesUploaded &= future.get();
    }
    
    // Wait for all non-DICOM file uploads to complete
    bool allNonDicomUploaded = true;
    for (auto& future : nonDicomUploadResults) {
        allNonDicomUploaded &= future.get();
    }
    
    if (allStudiesUploaded && allNonDicomUploaded) {
        LOG_INFO("Upload mode completed successfully");
        return true;
    } else {
        LOG_ERROR("Upload mode completed with errors");
        return false;
    }
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