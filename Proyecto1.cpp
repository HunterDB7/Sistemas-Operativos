#define _WIN32_WINNT 0x0600

#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <iostream>
#include <vector>
#include <map>
#include <string>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <conio.h>
#include <deque>

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif

using namespace std;

// Puntero a función para cargar GetSystemTimes dinámicamente si MinGW no lo declara
typedef BOOL (WINAPI *pfnGetSystemTimes)(PFILETIME, PFILETIME, PFILETIME);
pfnGetSystemTimes MyGetSystemTimes = NULL;

void InitSystemTimes() {
    HMODULE hKernel32 = GetModuleHandleA("kernel32.dll");
    if (hKernel32) {
        MyGetSystemTimes = (pfnGetSystemTimes)GetProcAddress(hKernel32, "GetSystemTimes");
    }
}

// Estructura para almacenar el historial y datos del proceso[cite: 1]
struct Process {
    DWORD pid;
    DWORD ppid;
    string name;
    string user;
    double cpu_percent;
    double mem_percent;
    SIZE_T resident_mem; // KB
    DWORD threads;
    string priority_str;
    DWORD priority_class;
    ULONGLONG total_time;
    deque<double> cpu_history; // Histórico de CPU[cite: 1]
    bool suspended;
};

// Variables globales para deltas de tiempo de CPU[cite: 1]
map<DWORD, ULONGLONG> prev_process_time;
ULONGLONG prev_sys_idle = 0, prev_sys_kernel = 0, prev_sys_user = 0;
DWORDLONG total_phys_mem = 0;

// Configuración de vista y TUI
int sort_mode = 0; // 0=CPU, 1=MEM
int view_mode = 0; // 0=Tabla, 1=Árbol
DWORD filter_pid = 0; 
string filter_name = "";
DWORD refresh_interval = 1000; // ms
string status_bar = "Ayuda: [q]Salir [s]Orden [a]Arbol [k]Matar [f]Forzar [p]Pausar [c]Seguir [u]Prio [+/-]Tiempo";

// Utilidad para convertir FILETIME a ULONGLONG
ULONGLONG FileTimeToULL(const FILETIME& ft) {
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return uli.QuadPart;
}

// Habilitar secuencias ANSI en la consola de Windows
void EnableANSI() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}

// Obtener usuario del proceso
string GetProcessUser(HANDLE hProcess) {
    HANDLE hToken;
    if (!OpenProcessToken(hProcess, TOKEN_QUERY, &hToken)) return "N/D";
    
    DWORD dwSize = 0;
    GetTokenInformation(hToken, TokenUser, NULL, 0, &dwSize);
    if (dwSize == 0) { CloseHandle(hToken); return "N/D"; }
    
    PTOKEN_USER pTokenUser = (PTOKEN_USER)malloc(dwSize);
    string user = "N/D";
    if (GetTokenInformation(hToken, TokenUser, pTokenUser, dwSize, &dwSize)) {
        SID_NAME_USE sidType;
        char name[256], domain[256];
        DWORD nameSize = 256, domainSize = 256;
        if (LookupAccountSidA(NULL, pTokenUser->User.Sid, name, &nameSize, domain, &domainSize, &sidType)) {
            user = string(name);
        }
    }
    free(pTokenUser);
    CloseHandle(hToken);
    return user;
}

// Obtener datos del sistema (Memoria y CPU base)[cite: 1]
void UpdateSystemData(double &sys_cpu_usage) {
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    GlobalMemoryStatusEx(&memInfo);
    total_phys_mem = memInfo.ullTotalPhys;

    FILETIME idleTime, kernelTime, userTime;
    if (MyGetSystemTimes) {
        MyGetSystemTimes(&idleTime, &kernelTime, &userTime);
        
        ULONGLONG sys_idle = FileTimeToULL(idleTime);
        ULONGLONG sys_kernel = FileTimeToULL(kernelTime);
        ULONGLONG sys_user = FileTimeToULL(userTime);
        
        ULONGLONG sys_delta = (sys_kernel + sys_user) - (prev_sys_kernel + prev_sys_user);
        ULONGLONG idle_delta = sys_idle - prev_sys_idle;
        
        if (sys_delta > 0) sys_cpu_usage = (sys_delta - idle_delta) * 100.0 / sys_delta;
        else sys_cpu_usage = 0.0;
        
        prev_sys_idle = sys_idle; prev_sys_kernel = sys_kernel; prev_sys_user = sys_user;
    }
}

// Obtener instantánea de procesos (ToolHelp32)[cite: 1]
vector<Process> GetProcesses(ULONGLONG sys_time_delta) {
    vector<Process> procs;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return procs;

    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32);
    if (!Process32First(hSnap, &pe32)) { CloseHandle(hSnap); return procs; }

    do {
        Process p;
        p.pid = pe32.th32ProcessID;
        p.ppid = pe32.th32ParentProcessID;
        p.name = pe32.szExeFile;
        p.threads = pe32.cntThreads;
        p.cpu_percent = 0.0;
        p.mem_percent = 0.0;
        p.resident_mem = 0;
        p.priority_str = "Normal";
        p.suspended = false;

        HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, p.pid);
        if (hProc) {
            // Memoria
            PROCESS_MEMORY_COUNTERS pmc;
            if (GetProcessMemoryInfo(hProc, &pmc, sizeof(pmc))) {
                p.resident_mem = pmc.WorkingSetSize / 1024;
                p.mem_percent = (double)pmc.WorkingSetSize / total_phys_mem * 100.0;
            }

            // CPU Manual[cite: 1]
            FILETIME cTime, eTime, kTime, uTime;
            if (GetProcessTimes(hProc, &cTime, &eTime, &kTime, &uTime)) {
                p.total_time = FileTimeToULL(kTime) + FileTimeToULL(uTime);
                ULONGLONG proc_delta = p.total_time - prev_process_time[p.pid];
                if (sys_time_delta > 0) {
                    p.cpu_percent = (double)proc_delta / sys_time_delta * 100.0;
                }
                prev_process_time[p.pid] = p.total_time;
            }
            
            // Prioridad[cite: 1]
            DWORD prio = GetPriorityClass(hProc);
            p.priority_class = prio;
            if (prio == HIGH_PRIORITY_CLASS) p.priority_str = "Alta";
            else if (prio == IDLE_PRIORITY_CLASS) p.priority_str = "Baja";
            else if (prio == REALTIME_PRIORITY_CLASS) p.priority_str = "R-Time";
            
            p.user = GetProcessUser(hProc);
            CloseHandle(hProc);
        } else {
            p.user = "Acceso Denegado"; // Mensaje sin caerse cuando no hay permiso[cite: 1]
        }
        
        // Histórico[cite: 1]
        if (p.cpu_history.size() > 5) p.cpu_history.pop_front();
        p.cpu_history.push_back(p.cpu_percent);
        
        procs.push_back(p);
    } while (Process32Next(hSnap, &pe32));

    CloseHandle(hSnap);
    return procs;
}

// Renderizar historial como minigráfico[cite: 1]
string GetSparkline(const deque<double>& hist) {
    string spark = "";
    for (double v : hist) {
        if (v < 1.0) spark += "_";
        else if (v < 10.0) spark += "-";
        else if (v < 50.0) spark += "o";
        else spark += "O";
    }
    return spark;
}

// Acciones sobre procesos[cite: 1]
void ControlProcess(DWORD pid, int action, int extra = 0) {
    if (action == 1) { // Terminar[cite: 1]
        HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
        if (h) { 
            TerminateProcess(h, 0); 
            CloseHandle(h); 
            status_bar = "Proceso terminado: " + to_string(pid); 
        } else {
            status_bar = "Error: Sin permisos para matar PID " + to_string(pid) + "."; //[cite: 1]
        }
    }
    else if (action == 2 || action == 3) { // Suspender / Reanudar[cite: 1]
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        THREADENTRY32 te32;
        te32.dwSize = sizeof(THREADENTRY32);
        if (Thread32First(hSnap, &te32)) {
            do {
                if (te32.th32OwnerProcessID == pid) {
                    HANDLE hThread = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te32.th32ThreadID);
                    if (hThread) {
                        if (action == 2) SuspendThread(hThread);
                        else ResumeThread(hThread);
                        CloseHandle(hThread);
                    }
                }
            } while (Thread32Next(hSnap, &te32));
        }
        CloseHandle(hSnap);
        status_bar = (action == 2 ? "Proceso suspendido: " : "Proceso reanudado: ") + to_string(pid);
    }
    else if (action == 4) { // Cambiar prioridad[cite: 1]
        HANDLE h = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pid);
        if (h) { 
            SetPriorityClass(h, extra); 
            CloseHandle(h); 
            status_bar = "Prioridad cambiada en PID " + to_string(pid); 
        } else {
            status_bar = "Error al cambiar prioridad (Acceso denegado).";
        }
    }
}

// Vista de árbol recursiva[cite: 1]
void PrintTree(DWORD pid, int depth, const map<DWORD, vector<Process>>& ppid_map, stringstream& screen, int& lines_drawn, int max_lines) {
    if (lines_drawn >= max_lines) return;
    if (ppid_map.find(pid) != ppid_map.end()) {
        for (const Process& p : ppid_map.at(pid)) {
            if (lines_drawn >= max_lines) break;
            screen << string(depth * 2, ' ') << "|- " << p.name << " (" << p.pid << ") CPU: " << fixed << setprecision(1) << p.cpu_percent << "%\n";
            lines_drawn++;
            PrintTree(p.pid, depth + 1, ppid_map, screen, lines_drawn, max_lines);
        }
    }
}

int main() {
    InitSystemTimes();
    EnableANSI();
    bool running = true;
    double sys_cpu_usage = 0.0;
    
    while (running) {
        FILETIME kTime, uTime, iTime;
        ULONGLONG sys_delta = 0;
        if (MyGetSystemTimes) {
            MyGetSystemTimes(&iTime, &kTime, &uTime);
            ULONGLONG sys_total = FileTimeToULL(kTime) + FileTimeToULL(uTime);
            sys_delta = sys_total - (prev_sys_kernel + prev_sys_user);
        }
        
        UpdateSystemData(sys_cpu_usage);
        vector<Process> procs = GetProcesses(sys_delta);
        
        // Ordenamiento[cite: 1]
        if (sort_mode == 0) sort(procs.begin(), procs.end(), [](const Process& a, const Process& b){ return a.cpu_percent > b.cpu_percent; });
        else sort(procs.begin(), procs.end(), [](const Process& a, const Process& b){ return a.resident_mem > b.resident_mem; });

        // Filtrado por nombre[cite: 1]
        if (!filter_name.empty()) {
            procs.erase(remove_if(procs.begin(), procs.end(), [](const Process& p) { 
                return p.name.find(filter_name) == string::npos; 
            }), procs.end());
        }

        CONSOLE_SCREEN_BUFFER_INFO csbi;
        GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
        int rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        int cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;

        stringstream screen;
       system("cls");
        
        // Resumen superior[cite: 1]
        MEMORYSTATUSEX mem; mem.dwLength = sizeof(MEMORYSTATUSEX); GlobalMemoryStatusEx(&mem);
        screen << "\033[1;32m=== MONITOR DE PROCESOS (WIN32 API) ===\033[0m\n";
        screen << "Procesos totales: " << procs.size() << " | Refresco: " << refresh_interval << "ms\n";
        screen << "CPU Sistema: " << fixed << setprecision(2) << sys_cpu_usage << "% \n";
        screen << "Memoria Fisica: " << mem.ullTotalPhys / (1024*1024) << " MB Total | " 
               << (mem.ullTotalPhys - mem.ullAvailPhys) / (1024*1024) << " MB Usada | "
               << mem.ullAvailPhys / (1024*1024) << " MB Disponible\n";
        screen << string(cols > 0 ? cols : 80, '-') << "\n";

        int max_rows = rows - 8;

        if (view_mode == 0) { // Tabla principal[cite: 1]
            screen << left << setw(8) << "PID" << setw(6) << "PPID" << setw(15) << "USUARIO" 
                   << setw(8) << "%CPU" << setw(10) << "MEM(KB)" << setw(6) << "HILOS" 
                   << setw(8) << "PRIO" << setw(10) << "HISTORICO" << "COMANDO\n";
            
            for (int i = 0; i < min((int)procs.size(), max_rows); ++i) {
                Process p = procs[i];
                if (p.resident_mem == 0) screen << "\033[31m"; // Resaltar accesos denegados o suspendidos[cite: 1]
                
                int name_len = cols > 75 ? cols - 75 : 15;
                screen << left << setw(8) << p.pid << setw(6) << p.ppid 
                       << setw(15) << p.user.substr(0, 14)
                       << fixed << setprecision(1) << setw(8) << p.cpu_percent 
                       << setw(10) << p.resident_mem << setw(6) << p.threads 
                       << setw(8) << p.priority_str << setw(10) << GetSparkline(p.cpu_history)
                       << p.name.substr(0, name_len) << "\033[0m\n";
            }
        } 
        else if (view_mode == 1) { // Vista de árbol[cite: 1]
            map<DWORD, vector<Process>> ppid_map;
            for (const auto& p : procs) ppid_map[p.ppid].push_back(p);
            
            int lines_drawn = 0;
            for (const auto& p : procs) {
                bool has_parent = false;
                for (const auto& check : procs) { if (check.pid == p.ppid) { has_parent = true; break; } }
                if (!has_parent && lines_drawn < max_rows) {
                    screen << p.name << " (" << p.pid << ")\n";
                    lines_drawn++;
                    PrintTree(p.pid, 1, ppid_map, screen, lines_drawn, max_rows);
                }
            }
        }

        screen << "\033[" << rows << ";1H" << "\033[1;36m" << status_bar << "\033[0m";
        cout << screen.str() << flush;

        // Gestión asíncrona de entrada en caliente (Hotkeys)[cite: 1]
        DWORD start_wait = GetTickCount();
        while ((GetTickCount() - start_wait) < refresh_interval) {
            if (_kbhit()) {
                char c = _getch();
                if (c == 'q') { running = false; break; }
                else if (c == 's') { sort_mode = !sort_mode; status_bar = "Modo de ordenamiento cambiado."; }
                else if (c == 'a') { view_mode = (view_mode == 1) ? 0 : 1; }
                else if (c == '+') { refresh_interval += 500; status_bar = "Refresco: " + to_string(refresh_interval) + "ms"; }
                else if (c == '-') { refresh_interval = (refresh_interval > 500) ? refresh_interval - 500 : 500; status_bar = "Refresco: " + to_string(refresh_interval) + "ms"; }
                else if (c == 'k' || c == 'f' || c == 'p' || c == 'c' || c == 'u') { // Acciones[cite: 1]
                    cout << "\033[" << rows << ";1H\033[2KIngrese PID objetivo: ";
                    DWORD t_pid; cin >> t_pid;
                    if (c == 'f') { // Confirmación aparte para terminar por fuerza[cite: 1]
                        cout << "\nForzar cierre? (s/n): "; char conf; cin >> conf;
                        if (conf != 's') { status_bar = "Cancelado."; continue; }
                    }
                    
                    if (c == 'k' || c == 'f') ControlProcess(t_pid, 1);
                    else if (c == 'p') ControlProcess(t_pid, 2);
                    else if (c == 'c') ControlProcess(t_pid, 3);
                    else if (c == 'u') {
                        cout << "\nClase de prioridad (32=Normal, 128=Alta, 64=Baja): ";
                        DWORD prio_class; cin >> prio_class;
                        ControlProcess(t_pid, 4, prio_class);
                    }
                }
            }
            Sleep(50);
        }
    }
    cout << "\033[2J\033[H";
    return 0;
}