#ifndef TASK_RUNTIME_HPP
#define TASK_RUNTIME_HPP

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace lab {

class TaskRuntime {
public:
    explicit TaskRuntime(std::size_t num_workers);
    ~TaskRuntime();

    TaskRuntime(const TaskRuntime&) = delete;
    TaskRuntime& operator=(const TaskRuntime&) = delete;

    // Agrega una tarea a la cola y devuelve un future para esperar a que termine
    std::future<void> submit(std::function<void()> task);

    // Detiene el pool y espera a los hilos
    void shutdown();

private:
    void workerLoop();

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> task_queue_;
    std::mutex queue_mutex_;
    std::condition_variable cv_;
    bool stop_flag_ = false;
};

} // namespace lab

#endif // TASK_RUNTIME_HPP