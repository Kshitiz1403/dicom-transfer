#include "thread_pool.h"

ThreadPool::ThreadPool(size_t threads, size_t maxQueueSize)
    : stop(false),
      active_threads(0),
      maxQueueSize(maxQueueSize) {
    for(size_t i = 0; i < threads; ++i) {
        workers.emplace_back([this] {
            while(true) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(this->queue_mutex);
                    this->condition.wait(lock, [this] { 
                        return this->stop || !this->tasks.empty(); 
                    });
                    if(this->stop && this->tasks.empty()) {
                        return;
                    }
                    task = std::move(this->tasks.front());
                    this->tasks.pop();
                }
                
                {
                    std::unique_lock<std::mutex> lock(count_mutex);
                    active_threads++;
                }
                
                task();
                
                {
                    std::unique_lock<std::mutex> lock(count_mutex);
                    active_threads--;
                }
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        stop = true;
    }
    condition.notify_all();
    for(std::thread &worker: workers) {
        worker.join();
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