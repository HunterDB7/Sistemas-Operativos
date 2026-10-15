# Monitor de procesos

Monitor de procesos para la terminal. Lee todo de /proc, por eso solo funciona en Linux.

Archivos:
- procesos.cpp: lee /proc y calcula CPU, memoria, orden y filtro
- tui.cpp: la pantalla, hilos, kill/nice y el main (incluye a procesos.cpp)

## Preparar (solo la primera vez)
En Windows usamos WSL:

```bash
wsl
sudo apt update && sudo apt install -y g++ git
```

## Ir a la carpeta
```bash
git clone https://github.com/HunterDB7/Sistemas-Operativos.git
cd Sistemas-Operativos
```

Si ya lo tienen bajado en Windows, desde WSL el C: queda en /mnt/c/
```bash
cd /mnt/c/Users/TU_USUARIO/ruta/al/repositorio/Sistemas-Operativos
```

## Compilar
Solo se compila tui.cpp
```bash
g++ -std=c++17 tui.cpp -o monitor
```

## Ejecutar
```bash
./monitor
```

Con sudo se puede actuar sobre procesos de otros usuarios y bajar el nice a negativo:
```bash
sudo ./monitor
```

## Teclas
- q: salir
- flechas, RePag, AvPag: mover la seleccion
- + y -: cambiar el refresco (de 0.5 a 10 segundos)
- c, m, p, e: ordenar por CPU, memoria, PID o tiempo de CPU
- /: filtrar (Enter para aceptar, Esc para cancelar, vacio quita el filtro)
- t: vista de arbol
- h: ver los hilos del proceso (h o Esc para volver)
- k: terminar el proceso
- K: terminar por la fuerza, pide confirmar con s o n
- s: suspender
- r: reanudar
- n: ver y cambiar el nice (-20 a 19)

## Para probar
En otra terminal:
```bash
sleep 1000 &
```
En el monitor: / , escribir sleep, Enter y probar s, r, n, h y K.

Para ver el %CPU subir a casi 100%:
```bash
yes > /dev/null
```
Se para con Ctrl+C.
