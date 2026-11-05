// Declara las funciones para medir la memoria y los fallos de pagina

#ifndef MONITOR_HPP
#define MONITOR_HPP

// Datos de memoria que se leen de Linux (/proc y getrusage)
struct InfoMemoria {
    // Del programa
    long vmsize_kb = 0;        // memoria virtual
    long rss_kb = 0;           // memoria residente (RAM que usa de verdad)
    long rss_pico_kb = 0;      // maximo de RAM residente
    long swap_kb = 0;          // swap que usa el programa
    long fallos_menores = 0;   // fallos de pagina sin leer del disco
    long fallos_mayores = 0;   // fallos de pagina que leen del disco

    // Del sistema
    long ram_total_kb = 0;
    long ram_libre_kb = 0;
    long swap_total_kb = 0;    // si es 0, el sistema no tiene SWAP
    long swap_libre_kb = 0;
};

InfoMemoria leerInfoMemoria();

// Reinicia el maximo de RAM residente para medir solo la prueba
void reiniciarPicoRss();

#endif
