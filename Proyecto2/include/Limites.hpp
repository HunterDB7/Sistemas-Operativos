#ifndef LIMITES_HPP
#define LIMITES_HPP

#include <cstddef>
#include <string>

// Cada funcion devuelve true si el limite se aplico (o si no se pidio, o sea 0).
// En "mensaje" queda un texto que dice que paso. Si empieza con "NO",
// el limite no se pudo aplicar.

// Limite de memoria virtual extra (usa setrlimit)
bool aplicarLimiteMemoria(std::size_t limite_mb, std::string& mensaje);
void quitarLimiteMemoria();

// Limite de CPU en porcentaje de un nucleo (usa cgroups, hay que usar sudo)
bool aplicarLimiteCpu(std::size_t porcentaje, std::string& mensaje);
void quitarLimiteCpu();

#endif
