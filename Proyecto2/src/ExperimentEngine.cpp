#include "ExperimentEngine.hpp"
#include "TaskRuntime.hpp"
#include "Workloads.hpp"

#include <chrono>
#include <future>
#include <semaphore.h>
#include <sys/mman.h>
#include <unistd.h>

namespace lab {

ExperimentEngine::ExperimentEngine() = default;

ExperimentEngine::~ExperimentEngine() {
    stopExperiment();
    if (async_runner_.joinable()) {
        async_runner_.join();
    }
}

const LiveProgress& ExperimentEngine::getLiveProgress() const {
    return live_progress_;
}

std::vector<ExperimentResult> ExperimentEngine::getResultsHistory() const {
    std::lock_guard<std::mutex> lock(engine_mutex_);
    return history_;
}

void ExperimentEngine::clearHistory() {
    std::lock_guard<std::mutex> lock(engine_mutex_);
    history_.clear();
}

void ExperimentEngine::stopExperiment() {
    stop_requested_.store(true, std::memory_order_relaxed);
}

bool ExperimentEngine::startExperimentAsync(const ExperimentConfig& config) {
    if (live_progress_.status.load(std::memory_order_relaxed) == ExperimentStatus::RUNNING) {
        return false;
    }

    if (async_runner_.joinable()) {
        async_runner_.join();
    }

    async_runner_ = std::thread([this, config]() {
        latest_async_result_ = executeInternal(config);
    });
    return true;
}

ExperimentResult ExperimentEngine::waitForCompletion() {
    if (async_runner_.joinable()) {
        async_runner_.join();
    }
    return latest_async_result_;
}

ExperimentResult ExperimentEngine::runExperiment(const ExperimentConfig& config) {
    if (live_progress_.status.load(std::memory_order_relaxed) == ExperimentStatus::RUNNING) {
        ExperimentResult busy_res;
        busy_res.final_status = ExperimentStatus::FAILED;
        busy_res.notes = "Ya existe un experimento en ejecucion.";
        return busy_res;
    }
    return executeInternal(config);
}

ExperimentResult ExperimentEngine::repeatLastExperiment() {
    ExperimentConfig cfg;
    {
        std::lock_guard<std::mutex> lock(engine_mutex_);
        if (!has_last_config_) {
            ExperimentResult empty_res;
            empty_res.final_status = ExperimentStatus::FAILED;
            empty_res.notes = "No hay ningun experimento previo registrado para repetir.";
            return empty_res;
        }
        cfg = last_config_;
    }
    return runExperiment(cfg);
}

std::pair<ExperimentResult, ExperimentResult> ExperimentEngine::compareSyncVsUnsync(
    ExperimentConfig base_config,
    SyncMechanism target_sync
) {
    base_config.workload_type = WorkloadType::SHARED_DATA_SYNC;

    ExperimentConfig unsync_cfg = base_config;
    unsync_cfg.sync_mech = SyncMechanism::NONE;
    ExperimentResult unsync_result = runExperiment(unsync_cfg);

    ExperimentConfig sync_cfg = base_config;
    sync_cfg.sync_mech = (target_sync == SyncMechanism::NONE) ? SyncMechanism::MUTEX : target_sync;
    ExperimentResult sync_result = runExperiment(sync_cfg);

    return {unsync_result, sync_result};
}

void ExperimentEngine::dispatchWorkUnits(
    const ExperimentConfig& config,
    std::size_t total_units,
    const std::function<void(std::size_t)>& unit_fn
) {
    if (config.exec_model == ExecutionModel::NATIVE_OS_THREADS) {
        std::vector<std::thread> threads;
        threads.reserve(total_units);
        for (std::size_t i = 0; i < total_units; ++i) {
            threads.emplace_back(unit_fn, i);
        }
        for (auto& t : threads) {
            if (t.joinable()) {
                t.join();
            }
        }
    } else {
        TaskRuntime runtime(config.num_threads);
        std::vector<std::future<void>> futures;
        futures.reserve(total_units);
        for (std::size_t i = 0; i < total_units; ++i) {
            futures.push_back(runtime.submit([unit_fn, i]() {
                unit_fn(i);
            }));
        }
        for (auto& f : futures) {
            f.wait();
        }
        runtime.shutdown();
    }
}

ExperimentResult ExperimentEngine::executeInternal(const ExperimentConfig& config) {
    stop_requested_.store(false, std::memory_order_relaxed);
    live_progress_.status.store(ExperimentStatus::RUNNING, std::memory_order_relaxed);
    live_progress_.completed_units.store(0, std::memory_order_relaxed);
    live_progress_.pages_reserved.store(0, std::memory_order_relaxed);
    live_progress_.pages_touched.store(0, std::memory_order_relaxed);
    live_progress_.elapsed_ms.store(0.0, std::memory_order_relaxed);

    ExperimentResult result;
    result.config = config;

    {
        std::lock_guard<std::mutex> lock(engine_mutex_);
        last_config_ = config;
        has_last_config_ = true;
        result.run_id = next_run_id_++;
    }

    const std::size_t workers_count = (config.num_threads > 0) ? config.num_threads : 1;
    const std::size_t units_count = (config.exec_model == ExecutionModel::RUNTIME_TASKS)
        ? ((config.num_tasks >= workers_count) ? config.num_tasks : workers_count)
        : workers_count;

    auto start_time = std::chrono::steady_clock::now();

    if (config.workload_type == WorkloadType::CPU_BOUND) {
        std::vector<std::uint64_t> unit_ops(units_count, 0);
        const std::uint64_t total_target_iters = config.iterations_per_worker * workers_count;
        const std::uint64_t iters_per_unit = total_target_iters / units_count;

        dispatchWorkUnits(config, units_count, [&](std::size_t idx) {
            unit_ops[idx] = Workloads::runCpuUnit(
                iters_per_unit,
                stop_requested_,
                live_progress_
            );
        });

        for (std::uint64_t ops : unit_ops) {
            result.total_operations += ops;
        }
        result.notes = "Carga intensiva de CPU finalizada.";

    } else if (config.workload_type == WorkloadType::SHARED_DATA_SYNC) {
        volatile std::uint64_t shared_counter = 0;
        std::mutex sync_mutex;
        sem_t sync_sem;
        sem_init(&sync_sem, 0, 1);

        std::vector<std::uint64_t> unit_ops(units_count, 0);
        const std::uint64_t total_target_iters = config.iterations_per_worker * workers_count;
        const std::uint64_t iters_per_unit = total_target_iters / units_count;

        dispatchWorkUnits(config, units_count, [&](std::size_t idx) {
            unit_ops[idx] = Workloads::runSharedDataUnit(
                iters_per_unit,
                config.sync_mech,
                shared_counter,
                sync_mutex,
                sync_sem,
                stop_requested_,
                live_progress_
            );
        });

        sem_destroy(&sync_sem);

        for (std::uint64_t ops : unit_ops) {
            result.total_operations += ops;
        }
        result.expected_shared_value = result.total_operations;
        result.actual_shared_value = shared_counter;
        result.lost_updates = static_cast<std::int64_t>(result.expected_shared_value) -
                              static_cast<std::int64_t>(result.actual_shared_value);
        result.notes = "Prueba de concurrencia sobre dato compartido finalizada.";

    } else if (config.workload_type == WorkloadType::MEMORY_PRESSURE) {
        long sys_page_size = sysconf(_SC_PAGESIZE);
        result.page_size_bytes = (sys_page_size > 0) ? static_cast<std::size_t>(sys_page_size) : 4096;

        // Limite de seguridad de 4096 MB para no agotar la memoria del equipo
        std::size_t safe_mb = (config.virtual_mem_mb > 4096) ? 4096 : config.virtual_mem_mb;
        if (safe_mb == 0) {
            safe_mb = 16;
        }

        result.virtual_bytes_reserved = safe_mb * 1024ULL * 1024ULL;
        result.pages_reserved = result.virtual_bytes_reserved / result.page_size_bytes;

        // Reserva de memoria virtual pura mediante mmap anonimo
        void* mapped = mmap(
            nullptr,
            result.virtual_bytes_reserved,
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS,
            -1,
            0
        );

        if (mapped == MAP_FAILED) {
            result.final_status = ExperimentStatus::FAILED;
            result.notes = "Fallo al reservar memoria virtual con mmap.";
            live_progress_.status.store(ExperimentStatus::FAILED, std::memory_order_relaxed);
            return result;
        }

        live_progress_.pages_reserved.store(result.pages_reserved, std::memory_order_relaxed);

        std::size_t pct = (config.touch_percentage > 100) ? 100 : config.touch_percentage;
        std::size_t target_pages_to_touch = (result.pages_reserved * pct) / 100;
        std::size_t pages_per_unit = target_pages_to_touch / units_count;

        std::vector<std::uint64_t> checksums(units_count, 0);
        auto* byte_ptr = static_cast<std::uint8_t*>(mapped);

        dispatchWorkUnits(config, units_count, [&](std::size_t idx) {
            std::size_t start_page = idx * pages_per_unit;
            std::size_t end_page = (idx == units_count - 1)
                ? target_pages_to_touch
                : start_page + pages_per_unit;

            checksums[idx] = Workloads::touchMemoryPagesUnit(
                byte_ptr,
                start_page,
                end_page,
                result.page_size_bytes,
                config.memory_passes,
                stop_requested_,
                live_progress_
            );
        });

        // Retencion controlada para permitir que la TUI observe la RAM residente y el SWAP
        std::size_t waited_ms = 0;
        while (waited_ms < config.hold_memory_ms &&
               !stop_requested_.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            waited_ms += 50;
        }

        munmap(mapped, result.virtual_bytes_reserved);

        for (std::uint64_t cs : checksums) {
            result.memory_checksum += cs;
        }
        result.pages_touched = live_progress_.pages_touched.load(std::memory_order_relaxed);
        result.total_operations = live_progress_.completed_units.load(std::memory_order_relaxed);
        result.notes = "Reserva de memoria virtual y acceso a paginas completado.";
    }

    auto end_time = std::chrono::steady_clock::now();
    result.duration_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
    live_progress_.elapsed_ms.store(result.duration_ms, std::memory_order_relaxed);

    if (result.duration_ms > 0.0) {
        result.operations_per_sec = (static_cast<double>(result.total_operations) * 1000.0) / result.duration_ms;
    }

    if (stop_requested_.load(std::memory_order_relaxed)) {
        result.final_status = ExperimentStatus::STOPPED_BY_USER;
        result.notes += " [Detenido de forma segura por el usuario]";
    } else {
        result.final_status = ExperimentStatus::COMPLETED;
    }

    live_progress_.status.store(result.final_status, std::memory_order_relaxed);

    {
        std::lock_guard<std::mutex> lock(engine_mutex_);
        history_.push_back(result);
    }

    return result;
}

} // namespace lab