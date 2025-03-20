# DICOM Transfer Utility Makefile

CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -pthread -I/usr/include/jsoncpp -I/usr/local/include
LDFLAGS = -ldcmdata -ldcmimgle -lofstd -laws-cpp-sdk-s3 -laws-cpp-sdk-dynamodb -laws-cpp-sdk-core -ljsoncpp -L/usr/local/lib

# Source files
SRCS = src/main.cpp \
       src/cli_parser.cpp \
       src/dicom_processor.cpp \
       src/s3_manager.cpp \
       src/dynamodb_manager.cpp \
       src/thread_pool.cpp \
       src/logger.cpp \
       src/profiler.cpp \
       src/utils.cpp

# Object files
OBJS = $(SRCS:.cpp=.o)

# Executable name
TARGET = dicom_transfer

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

# Dependencies
src/main.o: src/cli_parser.h src/dicom_processor.h src/s3_manager.h src/dynamodb_manager.h src/thread_pool.h src/logger.h src/profiler.h src/utils.h
src/cli_parser.o: src/cli_parser.h
src/dicom_processor.o: src/dicom_processor.h src/logger.h
src/s3_manager.o: src/s3_manager.h src/logger.h
src/dynamodb_manager.o: src/dynamodb_manager.h src/logger.h
src/thread_pool.o: src/thread_pool.h
src/logger.o: src/logger.h
src/profiler.o: src/profiler.h
src/utils.o: src/utils.h 