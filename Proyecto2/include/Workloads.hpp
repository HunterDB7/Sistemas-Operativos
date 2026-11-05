#ifndef WORKLOADS_HPP
#define WORKLOADS_HPP

#include "ExperimentTypes.hpp"
#include <atomic>
#include <mutex>
#include <semaphore.h>

namespace lab {

class Workloads {
public:
    // Carga de CPU para un hilo
    static std::uint64_t runCpuUnit(
        std::uint64_t iterations,
        const std::atomic<bool>& stop_requested,
        LiveProgress& progress
    );

    // Suma a una variable compartida (con o sin sincronizacion)
    static std::uint64_t runSharedDataUnit(
        std::uint64_t iterations,
        SyncMechanism sync_mech,
        volatile std::uint64_t& shared_counter,
        std::mutex& mtx,
        sem_t& sem,
        const std::atomic<bool>& stop_requested,
        LiveProgress& progress
    );

    // Escribe en un rango de paginas de la memoria reservada con mmap
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