// injector.cpp — standalone console injector for HelloNeighorBot.
//
// Usage: injector.exe [process_name] [dll_path]
//   process_name  defaults to "HelloNeighbor-Win64-Shipping.exe"
//   dll_path      defaults to "HelloNeighorBot.dll" resolved next to this exe
//
// Injects the DLL into the target process via the classic
// VirtualAllocEx + WriteProcessMemory + CreateRemoteThread(LoadLibraryA) method.

#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>

namespace {

// Defaults.
const char* kDefaultProcessName = "HelloNeighbor-Win64-Shipping.exe";
const char* kDefaultDllName     = "HelloNeighorBot.dll";

// Case-insensitive comparison of two NUL-terminated strings.
bool EqualsIgnoreCase(const char* a, const char* b) {
    return _stricmp(a, b) == 0;
}

// Return the directory containing this injector executable, including a
// trailing backslash. On failure returns an empty (current-dir) string.
void GetExecutableDir(char* out, DWORD outSize) {
    out[0] = '\0';
    char modulePath[MAX_PATH] = {0};
    DWORD len = GetModuleFileNameA(NULL, modulePath, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return;
    }
    // Strip the file name, keep everything up to and including the last slash.
    char* lastSlash = NULL;
    for (char* p = modulePath; *p; ++p) {
        if (*p == '\\' || *p == '/') {
            lastSlash = p;
        }
    }
    if (lastSlash) {
        *(lastSlash + 1) = '\0';
        // Copy into out (truncate safely if needed).
        DWORD i = 0;
        for (char* p = modulePath; *p && i + 1 < outSize; ++p, ++i) {
            out[i] = *p;
        }
        out[i] = '\0';
    }
}

// Find the PID of a process by (case-insensitive) executable name.
// Returns 0 if not found.
DWORD FindProcessId(const char* processName) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }

    PROCESSENTRY32 entry;
    ZeroMemory(&entry, sizeof(entry));
    entry.dwSize = sizeof(PROCESSENTRY32);

    DWORD pid = 0;
    if (Process32First(snapshot, &entry)) {
        do {
            if (EqualsIgnoreCase(entry.szExeFile, processName)) {
                pid = entry.th32ProcessID;
                break;
            }
        } while (Process32Next(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return pid;
}

} // namespace

int main(int argc, char** argv) {
    const char* processName = (argc > 1) ? argv[1] : kDefaultProcessName;

    // Resolve the DLL path argument (or default next to this exe).
    char dllArg[MAX_PATH] = {0};
    if (argc > 2) {
        // Copy the user-supplied path.
        int i = 0;
        for (; argv[2][i] && i + 1 < MAX_PATH; ++i) {
            dllArg[i] = argv[2][i];
        }
        dllArg[i] = '\0';
    } else {
        char exeDir[MAX_PATH] = {0};
        GetExecutableDir(exeDir, MAX_PATH);
        // exeDir + default dll name.
        int i = 0;
        for (; exeDir[i] && i + 1 < MAX_PATH; ++i) {
            dllArg[i] = exeDir[i];
        }
        for (int j = 0; kDefaultDllName[j] && i + 1 < MAX_PATH; ++j, ++i) {
            dllArg[i] = kDefaultDllName[j];
        }
        dllArg[i] = '\0';
    }

    // Resolve to an absolute path.
    char dllPath[MAX_PATH] = {0};
    DWORD fullLen = GetFullPathNameA(dllArg, MAX_PATH, dllPath, NULL);
    if (fullLen == 0 || fullLen >= MAX_PATH) {
        printf("[injector] ERROR: could not resolve DLL path '%s' (GetFullPathNameA failed, err=%lu)\n",
               dllArg, GetLastError());
        return 1;
    }

    // Verify the DLL file exists.
    DWORD attribs = GetFileAttributesA(dllPath);
    if (attribs == INVALID_FILE_ATTRIBUTES || (attribs & FILE_ATTRIBUTE_DIRECTORY)) {
        printf("[injector] ERROR: DLL not found at '%s'\n", dllPath);
        return 1;
    }

    printf("[injector] Target process : %s\n", processName);
    printf("[injector] DLL to inject  : %s\n", dllPath);

    // Find the target PID.
    DWORD pid = FindProcessId(processName);
    if (pid == 0) {
        printf("[injector] ERROR: process '%s' is not running.\n", processName);
        return 1;
    }
    printf("[injector] Found process '%s' with PID %lu\n", processName, pid);

    // Open the target process with full access.
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (hProcess == NULL) {
        printf("[injector] ERROR: OpenProcess failed for PID %lu (err=%lu). "
               "Try running the injector as administrator.\n",
               pid, GetLastError());
        return 1;
    }

    // Allocate memory in the remote process for the DLL path string.
    SIZE_T pathLen = strlen(dllPath) + 1; // include NUL terminator
    LPVOID remoteMem = VirtualAllocEx(hProcess, NULL, pathLen,
                                      MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (remoteMem == NULL) {
        printf("[injector] ERROR: VirtualAllocEx failed (err=%lu)\n", GetLastError());
        CloseHandle(hProcess);
        return 1;
    }

    // Write the DLL path into the remote memory.
    SIZE_T bytesWritten = 0;
    if (!WriteProcessMemory(hProcess, remoteMem, dllPath, pathLen, &bytesWritten) ||
        bytesWritten != pathLen) {
        printf("[injector] ERROR: WriteProcessMemory failed (err=%lu)\n", GetLastError());
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return 1;
    }

    // Resolve the address of LoadLibraryA in kernel32. kernel32 is mapped at the
    // same base address in every process on the system, so this address is valid
    // in the remote process too.
    HMODULE hKernel32 = GetModuleHandleA("kernel32.dll");
    if (hKernel32 == NULL) {
        printf("[injector] ERROR: GetModuleHandleA(\"kernel32.dll\") failed (err=%lu)\n",
               GetLastError());
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return 1;
    }
    FARPROC pLoadLib = GetProcAddress(hKernel32, "LoadLibraryA");
    if (pLoadLib == NULL) {
        printf("[injector] ERROR: GetProcAddress(\"LoadLibraryA\") failed (err=%lu)\n",
               GetLastError());
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return 1;
    }

    // Create a remote thread that calls LoadLibraryA(remoteMem).
    DWORD remoteThreadId = 0;
    HANDLE hThread = CreateRemoteThread(
        hProcess, NULL, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(pLoadLib),
        remoteMem, 0, &remoteThreadId);
    if (hThread == NULL) {
        printf("[injector] ERROR: CreateRemoteThread failed (err=%lu)\n", GetLastError());
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return 1;
    }

    // Wait for LoadLibraryA to finish.
    WaitForSingleObject(hThread, INFINITE);

    // The thread's exit code is the HMODULE returned by LoadLibraryA (low 32 bits
    // on x64). Zero means LoadLibrary failed inside the target.
    DWORD exitCode = 0;
    BOOL gotExit = GetExitCodeThread(hThread, &exitCode);

    // Clean up.
    CloseHandle(hThread);
    VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
    CloseHandle(hProcess);

    if (!gotExit) {
        printf("[injector] WARNING: GetExitCodeThread failed (err=%lu); "
               "injection status unknown for PID %lu.\n",
               GetLastError(), pid);
        return 1;
    }

    if (exitCode == 0) {
        printf("[injector] ERROR: LoadLibraryA failed inside PID %lu "
               "(remote returned NULL). DLL was NOT loaded.\n", pid);
        return 1;
    }

    printf("[injector] SUCCESS: DLL injected into PID %lu (remote HMODULE low32 = 0x%08lX).\n",
           pid, exitCode);
    return 0;
}
