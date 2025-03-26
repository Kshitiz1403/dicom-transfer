#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>

class ThreadPool {
private:
    // Order members by initialization order
    bool stop;
    size_t active_threads;
    size_t maxQueueSize;
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    mutable std::mutex queue_mutex;
    mutable std::mutex count_mutex;
    std::condition_variable condition;

public:
    explicit ThreadPool(size_t threads, size_t maxQueueSize = 1000);
    ~ThreadPool();

    // Delete copy constructor and assignment operator
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args) 
        -> std::future<typename std::result_of<F(Args...)>::type>;

    // Thread pool status methods
    size_t getActiveThreadCount() const;
    size_t getTotalThreadCount() const;
    size_t getQueueSize() const;
};

// Template method implementation (must be in header)
template<class F, class... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args) 
    -> std::future<typename std::result_of<F(Args...)>::type> {
    using return_type = typename std::result_of<F(Args...)>::type;

    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );
    
    std::future<return_type> res = task->get_future();
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        
        // Wait if queue is full
        condition.wait(lock, [this] { 
            return tasks.size() < maxQueueSize || stop; 
        });
        
        if(stop) {
            throw std::runtime_error("enqueue on stopped ThreadPool");
        }

        tasks.emplace([task](){ (*task)(); });
    }
    condition.notify_one();
    return res;
} 