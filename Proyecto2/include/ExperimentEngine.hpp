// Declara el motor que corre las pruebas (iniciar, detener, repetir y ver el progreso)

#ifndef EXPERIMENT_ENGINE_HPP
#define EXPERIMENT_ENGINE_HPP

#include "ExperimentTypes.hpp"
#include <atomic>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace lab {

class ExperimentEngine {
public:
    ExperimentEngine();
    ~ExperimentEngine();

    ExperimentEngine(const ExperimentEngine&) = delete;
    ExperimentEngine& operator=(const ExperimentEngine&) = delete;

    // Corre la prueba y espera a que termine
    ExperimentResult runExperiment(const ExperimentConfig& config);

    // Inicia la prueba en segundo plano
    bool startExperimentAsync(const ExperimentConfig& config);

    // Pide detener la prueba que esta corriendo
    void stopExperiment();

    // Espera a que termine la prueba en segundo plano
    ExperimentResult waitForCompletion();

    // Repite la ultima prueba
    ExperimentResult repeatLastExperiment();

    // Corre la prueba sin sincronizar y luego con sincronizacion
    std::pair<ExperimentResult, ExperimentResult> compareSyncVsUnsync(
        ExperimentConfig base_config,
        SyncMechanism target_sync = SyncMechanism::MUTEX
    );

    // Progreso en vivo
    const LiveProgress& getLiveProgress() const;

    // Historial de resultados
    std::vector<ExperimentResult> getResultsHistory() const;

    // Borra el historial
    void clearHistory();

private:
    ExperimentResult executeInternal(const ExperimentConfig& config);
    void dispatchWorkUnits(
        const ExperimentConfig& config,
        std::size_t total_units,
        const std::function<void(std::size_t)>& unit_fn
    );

    std::atomic<bool> stop_requested_{false};
    LiveProgress live_progress_;

    std::thread async_runner_;
    ExperimentResult latest_async_result_;

    mutable std::mutex engine_mutex_;
    ExperimentConfig last_config_;
    bool has_last_config_ = false;
    std::uint64_t next_run_id_ = 1;
    std::vector<ExperimentResult> history_;
};

} // namespace lab

#endif // EXPERIMENT_ENGINE_HPP