#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>

class ThreadPool {
public:
    ThreadPool(size_t numThreads);
    ~ThreadPool();
    
    // Add a task to the pool and get a future for its result
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args) 
        -> std::future<typename std::invoke_result<F, Args...>::type>;
    
    // Get the number of active threads
    size_t getActiveThreadCount() const;
    
    // Get the total number of threads in the pool
    size_t getTotalThreadCount() const;
    
    // Get the current queue size
    size_t getQueueSize() const;
    
private:
    // Worker threads
    std::vector<std::thread> workers;
    
    // Task queue
    std::queue<std::function<void()>> tasks;
    
    // Synchronization
    mutable std::mutex queue_mutex;
    std::condition_variable condition;
    
    // Thread pool state
    bool stop;
    
    // Track active threads
    mutable std::mutex count_mutex;
    size_t active_threads;
};

// Template implementation in header
template<class F, class... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args) 
    -> std::future<typename std::invoke_result<F, Args...>::type> {
    
    using return_type = typename std::invoke_result<F, Args...>::type;
    
    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );
    
    std::future<return_type> result = task->get_future();
    
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        
        // Don't allow enqueueing after stopping the pool
        if (stop) {
            throw std::runtime_error("enqueue on stopped ThreadPool");
        }
        
        tasks.emplace([task, this]() {
            {
                std::unique_lock<std::mutex> lock(count_mutex);
                active_threads++;
            }
            
            (*task)();
            
            {
                std::unique_lock<std::mutex> lock(count_mutex);
                active_threads--;
            }
        });
    }
    
    condition.notify_one();
    return result;
} 