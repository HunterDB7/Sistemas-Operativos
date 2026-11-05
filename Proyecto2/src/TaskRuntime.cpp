#include "TaskRuntime.hpp"

namespace lab {

TaskRuntime::TaskRuntime(std::size_t num_workers) {
    if (num_workers == 0) {
        num_workers = 1;
    }
    workers_.reserve(num_workers);
    for (std::size_t i = 0; i < num_workers; ++i) {
        workers_.emplace_back(&TaskRuntime::workerLoop, this);
    }
}

TaskRuntime::~TaskRuntime() {
    shutdown();
}

std::future<void> TaskRuntime::submit(std::function<void()> task) {
    auto packaged = std::make_shared<std::packaged_task<void()>>(std::move(task));
    std::future<void> completion = packaged->get_future();

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (stop_flag_) {
            return completion;
        }
        task_queue_.emplace([packaged]() { (*packaged)(); });
    }
    cv_.notify_one();
    return completion;
}

void TaskRuntime::shutdown() {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (stop_flag_) {
            return;
        }
        stop_flag_ = true;
    }
    cv_.notify_all();

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void TaskRuntime::workerLoop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            cv_.wait(lock, [this]() {
                return stop_flag_ || !task_queue_.empty();
            });

            if (stop_flag_ && task_queue_.empty()) {
                return;
            }
            task = std::move(task_queue_.front());
            task_queue_.pop();
        }
        task();
    }
}

} // namespace lab