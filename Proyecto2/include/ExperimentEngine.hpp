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

    // Ejecuta un experimento de forma bloqueante y devuelve su resultado
    ExperimentResult runExperiment(const ExperimentConfig& config);

    // Inicia un experimento en segundo plano (ideal para que la TUI no se congele)
    bool startExperimentAsync(const ExperimentConfig& config);

    // Solicita la detencion segura e inmediata del experimento en curso
    void stopExperiment();

    // Espera a que termine el experimento en segundo plano y devuelve su resultado
    ExperimentResult waitForCompletion();

    // Repite el ultimo experimento ejecutado con su misma configuracion
    ExperimentResult repeatLastExperiment();

    // Ejecuta y compara automaticamente una prueba sin sincronizacion vs sincronizada
    std::pair<ExperimentResult, ExperimentResult> compareSyncVsUnsync(
        ExperimentConfig base_config,
        SyncMechanism target_sync = SyncMechanism::MUTEX
    );

    // Consulta el progreso en tiempo real de manera segura entre hilos
    const LiveProgress& getLiveProgress() const;

    // Obtiene el historial completo de resultados para comparaciones en la TUI
    std::vector<ExperimentResult> getResultsHistory() const;

    // Limpia el historial de ejecuciones previas
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