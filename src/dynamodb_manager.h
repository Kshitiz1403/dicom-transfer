#pragma once

#include <string>
#include <vector>
#include <aws/core/Aws.h>
#include <aws/dynamodb/DynamoDBClient.h>
#include <aws/dynamodb/model/AttributeValue.h>
#include <json/json.h>

class DynamoDBManager {
public:
    DynamoDBManager(const std::string& region = "us-east-1");
    ~DynamoDBManager();
    
    // Store study metadata in DynamoDB
    bool storeStudyMetadata(const std::string& tableName,
                            const std::string& studyUid,
                            const Json::Value& metadata);
    
    // Retrieve study metadata from DynamoDB
    bool getStudyMetadata(const std::string& tableName,
                         const std::string& studyUid,
                         Json::Value& metadata);
    
    // Store file location in DynamoDB
    bool storeFileLocation(const std::string& tableName,
                          const std::string& studyUid,
                          const std::string& s3Key);
    
    // Get all file locations for a study
    std::vector<std::string> getFileLocations(const std::string& tableName,
                                            const std::string& studyUid);
    
    // Check if a table exists
    bool tableExists(const std::string& tableName);
    
    // Create table if it doesn't exist
    bool createTableIfNotExists(const std::string& tableName);
    
private:
    Aws::DynamoDB::DynamoDBClient m_dynamoClient;
    
    // Helper methods for converting between JSON and DynamoDB attribute values
    std::map<std::string, Aws::DynamoDB::Model::AttributeValue> jsonToAttributeMap(
        const Json::Value& json);
    
    Json::Value attributeMapToJson(
        const std::map<std::string, Aws::DynamoDB::Model::AttributeValue>& attributeMap);
}; 