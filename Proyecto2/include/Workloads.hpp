#ifndef WORKLOADS_HPP
#define WORKLOADS_HPP

#include "ExperimentTypes.hpp"
#include <atomic>
#include <mutex>
#include <semaphore.h>

namespace lab {

class Workloads {
public:
    // Ejecuta carga intensiva de CPU para un trabajador
    static std::uint64_t runCpuUnit(
        std::uint64_t iterations,
        const std::atomic<bool>& stop_requested,
        LiveProgress& progress
    );

    // Ejecuta acceso concurrente a variable compartida con o sin sincronizacion
    static std::uint64_t runSharedDataUnit(
        std::uint64_t iterations,
        SyncMechanism sync_mech,
        volatile std::uint64_t& shared_counter,
        std::mutex& mtx,
        sem_t& sem,
        const std::atomic<bool>& stop_requested,
        LiveProgress& progress
    );

    // Ejecuta reserva de memoria virtual con mmap y toca paginas en un rango asignado
    static std::uint64_t touchMemoryPagesUnit(
        std::uint8_t* base_ptr,
        std::size_t start_page,
        std::size_t end_page,
        std::size_t page_size,
        std::size_t passes,
        const std::atomic<bool>& stop_requested,
        LiveProgress& progress
    );
};

} // namespace lab

#endif // WORKLOADS_HPP