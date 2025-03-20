#include "s3_manager.h"
#include "logger.h"

#include <aws/core/auth/AWSCredentialsProvider.h>
#include <aws/s3/model/PutObjectRequest.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <aws/s3/model/DeleteObjectRequest.h>
#include <aws/s3/model/HeadObjectRequest.h>
#include <aws/s3/model/ListObjectsV2Request.h>
#include <aws/core/utils/stream/PreallocatedStreamBuf.h>
#include <aws/core/utils/memory/AWSMemory.h>

#include <fstream>
#include <iostream>
#include <sys/stat.h>

bool S3Manager::s_awsInitialized = false;

S3Manager::S3Manager(const std::string& region) {
    if (!s_awsInitialized) {
        LOG_ERROR("AWS SDK not initialized. Call S3Manager::initializeAWS() first");
        throw std::runtime_error("AWS SDK not initialized");
    }
    
    Aws::Client::ClientConfiguration clientConfig;
    clientConfig.region = region;
    clientConfig.scheme = Aws::Http::Scheme::HTTPS;
    
    // Enable multi-part upload with multi-threading
    clientConfig.executor = Aws::MakeShared<Aws::Utils::Threading::PooledThreadExecutor>("S3Client", 25);
    
    m_s3Client = Aws::S3::S3Client(clientConfig);
    
    LOG_INFO("S3Manager initialized with region: " + region);
}

S3Manager::~S3Manager() {
    // No need to free resources as the AWS SDK handles it
}

bool S3Manager::initializeAWS() {
    if (!s_awsInitialized) {
        Aws::SDKOptions options;
        options.loggingOptions.logLevel = Aws::Utils::Logging::LogLevel::Warn;
        
        Aws::InitAPI(options);
        s_awsInitialized = true;
        
        LOG_INFO("AWS SDK initialized");
    }
    return s_awsInitialized;
}

void S3Manager::shutdownAWS() {
    if (s_awsInitialized) {
        Aws::SDKOptions options;
        Aws::ShutdownAPI(options);
        s_awsInitialized = false;
        
        LOG_INFO("AWS SDK shut down");
    }
}

bool S3Manager::uploadFile(const std::string& bucketName, 
                           const std::string& localFilePath,
                           const std::string& s3Key,
                           std::function<void(size_t)> progressCallback) {
    struct stat statbuf;
    if (stat(localFilePath.c_str(), &statbuf) != 0) {
        LOG_ERROR("File does not exist: " + localFilePath);
        return false;
    }
    
    const size_t fileSize = statbuf.st_size;
    
    std::shared_ptr<Aws::IOStream> inputData = 
        Aws::MakeShared<Aws::FStream>("S3Stream", 
                                     localFilePath.c_str(), 
                                     std::ios_base::in | std::ios_base::binary);
    
    if (!inputData->good()) {
        LOG_ERROR("Failed to open file for reading: " + localFilePath);
        return false;
    }
    
    Aws::S3::Model::PutObjectRequest putObjectRequest;
    putObjectRequest.WithBucket(bucketName)
                     .WithKey(s3Key)
                     .WithBody(inputData)
                     .WithContentLength(fileSize)
                     .WithServerSideEncryption(Aws::S3::Model::ServerSideEncryption::AES256);
    
    LOG_INFO("Uploading file: " + localFilePath + " to S3://" + bucketName + "/" + s3Key);
    
    auto putObjectOutcome = m_s3Client.PutObject(putObjectRequest);
    
    if (putObjectOutcome.IsSuccess()) {
        LOG_INFO("Successfully uploaded file to S3: " + s3Key);
        
        if (progressCallback) {
            progressCallback(fileSize);
        }
        
        return true;
    } else {
        auto error = putObjectOutcome.GetError();
        LOG_ERROR("Failed to upload file to S3: " + 
                  error.GetExceptionName() + " - " + 
                  error.GetMessage());
        return false;
    }
}

bool S3Manager::downloadFile(const std::string& bucketName,
                           const std::string& s3Key,
                           const std::string& localFilePath,
                           std::function<void(size_t)> progressCallback) {
    Aws::S3::Model::GetObjectRequest getObjectRequest;
    getObjectRequest.WithBucket(bucketName)
                     .WithKey(s3Key);
    
    LOG_INFO("Downloading file from S3://" + bucketName + "/" + s3Key + " to " + localFilePath);
    
    auto getObjectOutcome = m_s3Client.GetObject(getObjectRequest);
    
    if (getObjectOutcome.IsSuccess()) {
        std::ofstream outputFile(localFilePath, std::ios::binary);
        if (!outputFile.is_open()) {
            LOG_ERROR("Failed to open local file for writing: " + localFilePath);
            return false;
        }
        
        auto& stream = getObjectOutcome.GetResult().GetBody();
        const size_t fileSize = getObjectOutcome.GetResult().GetContentLength();
        
        outputFile << stream.rdbuf();
        outputFile.close();
        
        if (progressCallback) {
            progressCallback(fileSize);
        }
        
        LOG_INFO("Successfully downloaded file from S3: " + s3Key);
        return true;
    } else {
        auto error = getObjectOutcome.GetError();
        LOG_ERROR("Failed to download file from S3: " + 
                  error.GetExceptionName() + " - " + 
                  error.GetMessage());
        return false;
    }
}

bool S3Manager::doesObjectExist(const std::string& bucketName, const std::string& s3Key) {
    Aws::S3::Model::HeadObjectRequest headObjectRequest;
    headObjectRequest.WithBucket(bucketName)
                      .WithKey(s3Key);
    
    auto headObjectOutcome = m_s3Client.HeadObject(headObjectRequest);
    
    return headObjectOutcome.IsSuccess();
}

bool S3Manager::deleteObject(const std::string& bucketName, const std::string& s3Key) {
    Aws::S3::Model::DeleteObjectRequest deleteObjectRequest;
    deleteObjectRequest.WithBucket(bucketName)
                        .WithKey(s3Key);
    
    LOG_INFO("Deleting object from S3: " + bucketName + "/" + s3Key);
    
    auto deleteObjectOutcome = m_s3Client.DeleteObject(deleteObjectRequest);
    
    if (deleteObjectOutcome.IsSuccess()) {
        LOG_INFO("Successfully deleted object from S3: " + s3Key);
        return true;
    } else {
        auto error = deleteObjectOutcome.GetError();
        LOG_ERROR("Failed to delete object from S3: " + 
                  error.GetExceptionName() + " - " + 
                  error.GetMessage());
        return false;
    }
}

std::vector<std::string> S3Manager::listObjects(const std::string& bucketName, 
                                              const std::string& prefix) {
    std::vector<std::string> keys;
    Aws::S3::Model::ListObjectsV2Request listObjectsRequest;
    listObjectsRequest.WithBucket(bucketName);
    
    if (!prefix.empty()) {
        listObjectsRequest.WithPrefix(prefix);
    }
    
    bool truncated = true;
    while (truncated) {
        auto listObjectsOutcome = m_s3Client.ListObjectsV2(listObjectsRequest);
        
        if (listObjectsOutcome.IsSuccess()) {
            const auto& objects = listObjectsOutcome.GetResult().GetContents();
            for (const auto& object : objects) {
                keys.push_back(object.GetKey());
            }
            
            truncated = listObjectsOutcome.GetResult().GetIsTruncated();
            if (truncated) {
                listObjectsRequest.SetContinuationToken(
                    listObjectsOutcome.GetResult().GetNextContinuationToken());
            }
        } else {
            auto error = listObjectsOutcome.GetError();
            LOG_ERROR("Failed to list objects from S3: " + 
                      error.GetExceptionName() + " - " + 
                      error.GetMessage());
            break;
        }
    }
    
    return keys;
} 