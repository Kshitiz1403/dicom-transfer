#include "dynamodb_manager.h"
#include "logger.h"

#include <aws/dynamodb/model/PutItemRequest.h>
#include <aws/dynamodb/model/GetItemRequest.h>
#include <aws/dynamodb/model/QueryRequest.h>
#include <aws/dynamodb/model/DescribeTableRequest.h>
#include <aws/dynamodb/model/CreateTableRequest.h>
#include <aws/dynamodb/model/UpdateItemRequest.h>

DynamoDBManager::DynamoDBManager(const std::string& region) {
    Aws::Client::ClientConfiguration clientConfig;
    clientConfig.region = region;
    
    // Use placement new to initialize the member directly
    new (&m_dynamoClient) Aws::DynamoDB::DynamoDBClient(clientConfig);
    
    LOG_INFO("DynamoDBManager initialized with region: " + region);
}

DynamoDBManager::~DynamoDBManager() {
    // No need to free resources as the AWS SDK handles it
}

bool DynamoDBManager::storeStudyMetadata(const std::string& tableName,
                                        const std::string& studyUid,
                                        const Json::Value& metadata) {
    if (!tableExists(tableName)) {
        LOG_WARNING("Table does not exist: " + tableName);
        if (!createTableIfNotExists(tableName)) {
            LOG_ERROR("Failed to create table: " + tableName);
            return false;
        }
    }
    
    // Ensure the studyUid is part of the metadata
    Json::Value metadataWithId = metadata;
    metadataWithId["StudyInstanceUID"] = studyUid;
    
    auto attributeMap = jsonToAttributeMap(metadataWithId);
    
    Aws::DynamoDB::Model::PutItemRequest putItemRequest;
    putItemRequest.SetTableName(tableName);
    putItemRequest.SetItem(attributeMap);
    
    LOG_INFO("Storing metadata in DynamoDB for study: " + studyUid);
    
    auto putItemOutcome = m_dynamoClient.PutItem(putItemRequest);
    
    if (putItemOutcome.IsSuccess()) {
        LOG_INFO("Successfully stored metadata for study: " + studyUid);
        return true;
    } else {
        auto error = putItemOutcome.GetError();
        LOG_ERROR("Failed to store metadata in DynamoDB: " + 
                 error.GetExceptionName() + " - " + 
                 error.GetMessage());
        return false;
    }
}

bool DynamoDBManager::getStudyMetadata(const std::string& tableName,
                                     const std::string& studyUid,
                                     Json::Value& metadata) {
    Aws::DynamoDB::Model::GetItemRequest getItemRequest;
    
    // Set up key with the study UID
    Aws::Map<Aws::String, Aws::DynamoDB::Model::AttributeValue> key;
    key["StudyInstanceUID"].SetS(studyUid);
    
    getItemRequest.SetTableName(tableName);
    getItemRequest.SetKey(key);
    
    LOG_INFO("Retrieving metadata from DynamoDB for study: " + studyUid);
    
    auto getItemOutcome = m_dynamoClient.GetItem(getItemRequest);
    
    if (getItemOutcome.IsSuccess()) {
        const auto& item = getItemOutcome.GetResult().GetItem();
        
        if (!item.empty()) {
            metadata = attributeMapToJson(item);
            LOG_INFO("Successfully retrieved metadata for study: " + studyUid);
            return true;
        } else {
            LOG_WARNING("No metadata found for study: " + studyUid);
            return false;
        }
    } else {
        auto error = getItemOutcome.GetError();
        LOG_ERROR("Failed to retrieve metadata from DynamoDB: " + 
                 error.GetExceptionName() + " - " + 
                 error.GetMessage());
        return false;
    }
}

bool DynamoDBManager::storeFileLocation(const std::string& tableName,
                                      const std::string& studyUid,
                                      const std::string& s3Key) {
    Aws::DynamoDB::Model::UpdateItemRequest updateItemRequest;
    
    // Set up key with the study UID
    Aws::Map<Aws::String, Aws::DynamoDB::Model::AttributeValue> key;
    key["StudyInstanceUID"].SetS(studyUid);
    
    // Create expression to add to the file locations list
    Aws::String updateExpression = "ADD FileLocations :s3key";
    
    // Set up expression attribute values
    Aws::Map<Aws::String, Aws::DynamoDB::Model::AttributeValue> expressionAttributeValues;
    Aws::DynamoDB::Model::AttributeValue s3KeyValue;
    Aws::Vector<Aws::String> stringSet;
    stringSet.push_back(s3Key.c_str());
    s3KeyValue.SetSS(stringSet);
    expressionAttributeValues[":s3key"] = s3KeyValue;
    
    updateItemRequest.SetTableName(tableName);
    updateItemRequest.SetKey(key);
    updateItemRequest.SetUpdateExpression(updateExpression);
    updateItemRequest.SetExpressionAttributeValues(expressionAttributeValues);
    
    LOG_INFO("Storing file location in DynamoDB for study: " + studyUid + ", S3 key: " + s3Key);
    
    auto updateItemOutcome = m_dynamoClient.UpdateItem(updateItemRequest);
    
    if (updateItemOutcome.IsSuccess()) {
        LOG_INFO("Successfully stored file location for study: " + studyUid);
        return true;
    } else {
        auto error = updateItemOutcome.GetError();
        LOG_ERROR("Failed to store file location in DynamoDB: " + 
                 error.GetExceptionName() + " - " + 
                 error.GetMessage());
        return false;
    }
}

std::vector<std::string> DynamoDBManager::getFileLocations(const std::string& tableName,
                                                         const std::string& studyUid) {
    std::vector<std::string> fileLocations;
    
    Aws::DynamoDB::Model::GetItemRequest getItemRequest;
    
    // Set up key with the study UID
    Aws::Map<Aws::String, Aws::DynamoDB::Model::AttributeValue> key;
    key["StudyInstanceUID"].SetS(studyUid);
    
    getItemRequest.SetTableName(tableName);
    getItemRequest.SetKey(key);
    getItemRequest.AddAttributesToGet("FileLocations");
    
    LOG_INFO("Retrieving file locations from DynamoDB for study: " + studyUid);
    
    auto getItemOutcome = m_dynamoClient.GetItem(getItemRequest);
    
    if (getItemOutcome.IsSuccess()) {
        const auto& item = getItemOutcome.GetResult().GetItem();
        
        if (!item.empty() && item.count("FileLocations") > 0) {
            const auto& locations = item.at("FileLocations").GetSS();
            for (const auto& location : locations) {
                fileLocations.push_back(location);
            }
            LOG_INFO("Retrieved " + std::to_string(fileLocations.size()) + 
                    " file locations for study: " + studyUid);
        } else {
            LOG_WARNING("No file locations found for study: " + studyUid);
        }
    } else {
        auto error = getItemOutcome.GetError();
        LOG_ERROR("Failed to retrieve file locations from DynamoDB: " + 
                 error.GetExceptionName() + " - " + 
                 error.GetMessage());
    }
    
    return fileLocations;
}

bool DynamoDBManager::tableExists(const std::string& tableName) {
    Aws::DynamoDB::Model::DescribeTableRequest describeTableRequest;
    describeTableRequest.SetTableName(tableName);
    
    auto describeTableOutcome = m_dynamoClient.DescribeTable(describeTableRequest);
    
    return describeTableOutcome.IsSuccess();
}

bool DynamoDBManager::createTableIfNotExists(const std::string& tableName) {
    if (tableExists(tableName)) {
        return true;
    }
    
    LOG_INFO("Creating DynamoDB table: " + tableName);
    
    Aws::DynamoDB::Model::CreateTableRequest createTableRequest;
    createTableRequest.SetTableName(tableName);
    
    // Define attribute definitions
    Aws::Vector<Aws::DynamoDB::Model::AttributeDefinition> attributeDefinitions;
    Aws::DynamoDB::Model::AttributeDefinition studyUidAttribute;
    studyUidAttribute.SetAttributeName("StudyInstanceUID");
    studyUidAttribute.SetAttributeType(Aws::DynamoDB::Model::ScalarAttributeType::S);
    attributeDefinitions.push_back(studyUidAttribute);
    
    createTableRequest.SetAttributeDefinitions(attributeDefinitions);
    
    // Define key schema
    Aws::Vector<Aws::DynamoDB::Model::KeySchemaElement> keySchema;
    Aws::DynamoDB::Model::KeySchemaElement studyUidKeyElement;
    studyUidKeyElement.SetAttributeName("StudyInstanceUID");
    studyUidKeyElement.SetKeyType(Aws::DynamoDB::Model::KeyType::HASH);
    keySchema.push_back(studyUidKeyElement);
    
    createTableRequest.SetKeySchema(keySchema);
    
    // Set provisioned throughput
    Aws::DynamoDB::Model::ProvisionedThroughput provisionedThroughput;
    provisionedThroughput.SetReadCapacityUnits(5);
    provisionedThroughput.SetWriteCapacityUnits(5);
    createTableRequest.SetProvisionedThroughput(provisionedThroughput);
    
    auto createTableOutcome = m_dynamoClient.CreateTable(createTableRequest);
    
    if (createTableOutcome.IsSuccess()) {
        LOG_INFO("Successfully created DynamoDB table: " + tableName);
        
        // Wait for the table to become active
        bool tableActive = false;
        int attempts = 0;
        const int maxAttempts = 30; // Wait up to 30 seconds
        
        while (!tableActive && attempts < maxAttempts) {
            Aws::DynamoDB::Model::DescribeTableRequest describeTableRequest;
            describeTableRequest.SetTableName(tableName);
            
            auto describeTableOutcome = m_dynamoClient.DescribeTable(describeTableRequest);
            
            if (describeTableOutcome.IsSuccess()) {
                auto status = describeTableOutcome.GetResult().GetTable().GetTableStatus();
                if (status == Aws::DynamoDB::Model::TableStatus::ACTIVE) {
                    tableActive = true;
                }
            }
            
            if (!tableActive) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                attempts++;
            }
        }
        
        if (tableActive) {
            LOG_INFO("DynamoDB table is now active: " + tableName);
            return true;
        } else {
            LOG_ERROR("Timed out waiting for DynamoDB table to become active: " + tableName);
            return false;
        }
    } else {
        auto error = createTableOutcome.GetError();
        LOG_ERROR("Failed to create DynamoDB table: " + 
                 error.GetExceptionName() + " - " + 
                 error.GetMessage());
        return false;
    }
}

std::map<std::string, Aws::DynamoDB::Model::AttributeValue> DynamoDBManager::jsonToAttributeMap(
    const Json::Value& json) {
    std::map<std::string, Aws::DynamoDB::Model::AttributeValue> attributeMap;
    
    // Process JSON object fields
    for (const auto& key : json.getMemberNames()) {
        const Json::Value& value = json[key];
        
        if (value.isString()) {
            attributeMap[key].SetS(value.asString());
        }
        else if (value.isInt()) {
            attributeMap[key].SetN(std::to_string(value.asInt()));
        }
        else if (value.isDouble()) {
            attributeMap[key].SetN(std::to_string(value.asDouble()));
        }
        else if (value.isBool()) {
            attributeMap[key].SetBool(value.asBool());
        }
        else if (value.isArray()) {
            // Convert array to StringSet if all elements are strings
            bool allStrings = true;
            Aws::Vector<Aws::String> stringSet;
            
            for (const auto& item : value) {
                if (item.isString()) {
                    stringSet.push_back(item.asString());
                } else {
                    allStrings = false;
                    break;
                }
            }
            
            if (allStrings && !stringSet.empty()) {
                attributeMap[key].SetSS(stringSet);
            }
            else {
                // Convert to JSON string if mixed types
                Json::FastWriter writer;
                attributeMap[key].SetS(writer.write(value));
            }
        }
        else if (value.isObject()) {
            // Convert nested object to JSON string
            Json::FastWriter writer;
            attributeMap[key].SetS(writer.write(value));
        }
        else if (value.isNull()) {
            attributeMap[key].SetNull(true);
        }
    }
    
    return attributeMap;
}

Json::Value DynamoDBManager::attributeMapToJson(
    const std::map<std::string, Aws::DynamoDB::Model::AttributeValue>& attributeMap) {
    Json::Value json;
    
    for (const auto& pair : attributeMap) {
        const auto& key = pair.first;
        const auto& attr = pair.second;
        
        if (attr.GetType() == Aws::DynamoDB::Model::ValueType::STRING) {
            json[key] = attr.GetS();
        }
        else if (attr.GetType() == Aws::DynamoDB::Model::ValueType::NUMBER) {
            json[key] = attr.GetN();
        }
        else if (attr.GetType() == Aws::DynamoDB::Model::ValueType::BOOL) {
            json[key] = attr.GetBool();
        }
        else if (attr.GetType() == Aws::DynamoDB::Model::ValueType::STRING_SET) {
            Json::Value array(Json::arrayValue);
            for (const auto& str : attr.GetSS()) {
                array.append(str);
            }
            json[key] = array;
        }
        else if (attr.GetType() == Aws::DynamoDB::Model::ValueType::NUMBER_SET) {
            Json::Value array(Json::arrayValue);
            for (const auto& num : attr.GetNS()) {
                array.append(num);
            }
            json[key] = array;
        }
        // Add other types as needed
    }
    
    return json;
} 