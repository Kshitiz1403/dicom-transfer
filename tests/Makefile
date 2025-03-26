CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -pthread -I../src -I/usr/include/jsoncpp
LDFLAGS = -lgtest -lgtest_main -pthread -laws-cpp-sdk-s3 -laws-cpp-sdk-core -ljsoncpp

TEST_SRCS = s3_manager_test.cpp \
            s3_benchmark_test.cpp \
            s3_core_test.cpp \
            ../src/s3_manager.cpp \
            ../src/utils.cpp \
            ../src/logger.cpp \
            ../src/thread_pool.cpp \
            ../src/profiler.cpp

TEST_TARGET = run_tests

.PHONY: all clean test

all: $(TEST_TARGET)

$(TEST_TARGET): $(TEST_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

test: $(TEST_TARGET)
	./$(TEST_TARGET)

clean:
	rm -f $(TEST_TARGET) 