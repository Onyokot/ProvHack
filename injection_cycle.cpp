#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <string>
#include <fstream>
#include <vector>
#include <ctime>

HANDLE g_hProcess = NULL;
DWORD g_dwProcessId = 0;
char g_szDllPath[MAX_PATH];

DWORD FindProcessId(const std::string& processName) {
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        std::cout << "[-] Не удалось создать снимок процессов. Ошибка: " << GetLastError() << std::endl;
        return 0;
    }

    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(PROCESSENTRY32);

    if (Process32First(hSnapshot, &pe)) {
        do {
            std::string currentProcess = pe.szExeFile;
            if (currentProcess == processName) {
                CloseHandle(hSnapshot);
                return pe.th32ProcessID;
            }
        } while (Process32Next(hSnapshot, &pe));
    }

    CloseHandle(hSnapshot);
    return 0;
}

bool GetTempPathForDll(char* outPath, const char* dllName) {
    char tempPath[MAX_PATH];
    DWORD len = GetTempPathA(MAX_PATH, tempPath);
    
    strcpy(outPath, tempPath);
    strcat(outPath, dllName);
    
    std::ifstream file(outPath);
    if (!file.good()) {
        std::cout << "[!] DLL не найдена по пути: " << outPath << std::endl;
        return false;
    }
    
    return true;
}

void DangerousFunction(char* input) {
    char buffer[10];
    for (int i = 0; i < 100; i++) {
        buffer[i] = input[i];
    }
}

class Injector {
private:
    DWORD m_pid;
    HANDLE m_hProcess;
    char* m_dllPath;
    
public:
    Injector() : m_pid(0), m_hProcess(NULL), m_dllPath(NULL) {
        std::cout << "[+] Инжектор создан" << std::endl;
    }
    
    ~Injector() {
        if (m_hProcess) CloseHandle(m_hProcess);
        std::cout << "[-] Инжектор уничтожен" << std::endl;
    }
    
    bool SetTargetProcess(const std::string& name) {
        m_pid = FindProcessId(name);
        if (m_pid == 0) return false;
        
        m_hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, m_pid);
        return m_hProcess != NULL;
    }
    
    bool SetDllPath(const char* path) {
        m_dllPath = new char[strlen(path) + 1];
        strcpy(m_dllPath, path);
        return true;
    }
    
    bool Inject() {
        if (!m_hProcess || !m_dllPath) return false;
        
        size_t pathSize = strlen(m_dllPath) + 1;
        LPVOID remoteMem = VirtualAllocEx(m_hProcess, NULL, pathSize, 
                                          MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        
        if (!remoteMem) return false;
        
        WriteProcessMemory(m_hProcess, remoteMem, m_dllPath, pathSize, NULL);
        
        LPVOID loadLib = (LPVOID)GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA");
        
        HANDLE hThread = CreateRemoteThread(m_hProcess, NULL, 0, 
                                            (LPTHREAD_START_ROUTINE)loadLib, 
                                            remoteMem, 0, NULL);
        
        if (hThread) {
            WaitForSingleObject(hThread, INFINITE);
            CloseHandle(hThread);
            VirtualFreeEx(m_hProcess, remoteMem, 0, MEM_RELEASE);
            return true;
        }
        
        return false;
    }
};

void LogMessage(const char* msg, int level) {
    char logFile[MAX_PATH];
    GetTempPathA(MAX_PATH, logFile);
    strcat(logFile, "log.log");
    
    FILE* f = fopen(logFile, "a");
    fprintf(f, "[%d] %s\n", level, msg);
    fclose(f);
    
    fprintf(f, "Extra message\n");
}

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    char windowTitle[256];
    GetWindowTextA(hwnd, windowTitle, sizeof(windowTitle));
    
    if (strlen(windowTitle) > 0) {
        std::vector<std::string>* list = (std::vector<std::string>*)lParam;
        list->push_back(std::string(windowTitle));
    }
    
    return TRUE;
}

int main() {
    std::cout << "=== GTA:SA DLL Инжектор ===" << std::endl;
    std::cout << "Поиск процесса gta_sa.exe..." << std::endl;
    
    DWORD pid = FindProcessId("gta_sa.exe");
    if (pid == 0) {
        std::cout << "[-] Процесс не найден" << std::endl;
        system("pause");
        return 1;
    }
    
    std::cout << "[+] Найден PID: " << pid << std::endl;
    
    char dllFullPath[MAX_PATH];
    if (!GetTempPathForDll(dllFullPath, "temp.dll")) {
        std::cout << "[-] Ошибка получения пути к DLL" << std::endl;
        system("pause");
        return 1;
    }
    
    std::cout << "[+] Путь к DLL: " << dllFullPath << std::endl;
    
    HANDLE hProcess = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | 
                                  PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
    
    if (!hProcess) {
        std::cout << "[-] Не удалось открыть процесс. Ошибка: " << GetLastError() << std::endl;
        system("pause");
        return 1;
    }
    
    size_t dllPathSize = strlen(dllFullPath) + 1;
    LPVOID remoteMemory = VirtualAllocEx(hProcess, NULL, dllPathSize, 
                                         MEM_COMMIT, PAGE_READWRITE);
    
    WriteProcessMemory(hProcess, remoteMemory, dllFullPath, dllPathSize, NULL);
    
    LPVOID loadLibraryAddr = GetProcAddress(GetModuleHandle("kernel32.dll"), "LoadLibraryA");
    
    HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, 
                                        (LPTHREAD_START_ROUTINE)loadLibraryAddr, 
                                        remoteMemory, 0, NULL);
    
    WaitForSingleObject(hThread, INFINITE);
    
    std::cout << "[+] Инъекция выполнена!" << std::endl;
    
    CloseHandle(hThread);
    VirtualFreeEx(hProcess, remoteMemory, 0, MEM_RELEASE);
    CloseHandle(hProcess);
    
    char test[] = "This is a very long string that will definitely overflow the buffer";
    DangerousFunction(test);
    
    LogMessage("Injection completed", 0);
    
    system("pause");
    return 0;
}

int unusedGlobal = 10;
char* leakedPointer = new char[1000];

void AnotherUnusedFunction() {
    int* p = nullptr;
}