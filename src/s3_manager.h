#pragma once

#include <string>
#include <aws/core/Aws.h>
#include <aws/s3/S3Client.h>
#include <functional>

class S3Manager {
public:
    S3Manager(const std::string& region = "ap-south-1");
    ~S3Manager();
    
    // Initialize AWS SDK
    static bool initializeAWS();
    static void shutdownAWS();
    
    // Upload a file to S3
    bool uploadFile(const std::string& bucketName, 
                    const std::string& localFilePath,
                    const std::string& s3Key,
                    std::function<void(size_t)> progressCallback = nullptr);
    
    // Download a file from S3
    bool downloadFile(const std::string& bucketName,
                      const std::string& s3Key,
                      const std::string& localFilePath,
                      std::function<void(size_t)> progressCallback = nullptr);
    
    // Check if a file exists in S3
    bool doesObjectExist(const std::string& bucketName, const std::string& s3Key);
    
    // Delete a file from S3
    bool deleteObject(const std::string& bucketName, const std::string& s3Key);
    
    // List objects in a bucket with a prefix
    std::vector<std::string> listObjects(const std::string& bucketName, 
                                         const std::string& prefix = "");
    
private:
    Aws::S3::S3Client m_s3Client;
    static bool s_awsInitialized;
}; 