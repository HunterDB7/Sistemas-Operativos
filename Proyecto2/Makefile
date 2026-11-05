CXX = g++
CXXFLAGS = -std=c++17 -Wall -Iinclude -pthread

FUENTES = src/Tui.cpp src/Monitor.cpp src/Limites.cpp \
          src/ExperimentEngine.cpp src/TaskRuntime.cpp src/Workloads.cpp

laboratorio: $(FUENTES)
	$(CXX) $(CXXFLAGS) $(FUENTES) -o laboratorio -lncurses

clean:
	rm -f laboratorio
