#include "thread_pool.h"

ThreadPool::ThreadPool(size_t numThreads) : stop(false), active_threads(0) {
    for (size_t i = 0; i < numThreads; ++i) {
        workers.emplace_back([this] {
            while (true) {
                std::function<void()> task;
                
                {
                    std::unique_lock<std::mutex> lock(this->queue_mutex);
                    
                    // Wait until there's a task or the pool is stopped
                    this->condition.wait(lock, [this] {
                        return this->stop || !this->tasks.empty();
                    });
                    
                    // Exit if the pool is stopped and there are no more tasks
                    if (this->stop && this->tasks.empty()) {
                        return;
                    }
                    
                    // Get the task from the front of the queue
                    task = std::move(this->tasks.front());
                    this->tasks.pop();
                }
                
                // Execute the task
                task();
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        stop = true;
    }
    
    // Notify all threads to check the stop flag
    condition.notify_all();
    
    // Join all worker threads
    for (std::thread &worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

size_t ThreadPool::getActiveThreadCount() const {
    std::unique_lock<std::mutex> lock(count_mutex);
    return active_threads;
}

size_t ThreadPool::getTotalThreadCount() const {
    return workers.size();
}

size_t ThreadPool::getQueueSize() const {
    std::unique_lock<std::mutex> lock(queue_mutex);
    return tasks.size();
} 