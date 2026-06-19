#include "Logger.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace logger {
namespace {

constexpr size_t kMaxRingLines = 200;

std::mutex g_mutex;
bool g_initialized = false;
bool g_consoleAllocated = false;
std::ofstream g_file;
std::deque<std::string> g_ring;

// stdout/stderr handles we redirected to CONOUT$ (so we can close them on shutdown).
FILE* g_stdoutStream = nullptr;
FILE* g_stderrStream = nullptr;

const char* LevelTag(Level level) {
    switch (level) {
        case Level::Warn:  return "WARN";
        case Level::Error: return "ERROR";
        case Level::Info:
        default:           return "INFO";
    }
}

// Builds "HH:MM:SS" for the current local time into the provided buffer.
void FormatTime(char* out, size_t outSize) {
    if (outSize == 0) {
        return;
    }
    out[0] = '\0';
    std::time_t t = std::time(nullptr);
    std::tm tmStruct{};
    if (localtime_s(&tmStruct, &t) == 0) {
        std::strftime(out, outSize, "%H:%M:%S", &tmStruct);
    }
}

// Attempts to open the log file at "<dir>\\HelloNeighorBot.log". Returns true on success.
bool TryOpenLogIn(const std::string& dir) {
    if (dir.empty()) {
        return false;
    }
    std::string path = dir;
    char last = path.back();
    if (last != '\\' && last != '/') {
        path += '\\';
    }
    path += "HelloNeighorBot.log";
    g_file.open(path, std::ios::out | std::ios::app);
    return g_file.is_open();
}

// Returns the directory containing the currently loaded module (this DLL), or "" on failure.
std::string ModuleDirectory() {
    HMODULE module = nullptr;
    if (!GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&ModuleDirectory),
            &module)) {
        return std::string();
    }
    char buffer[MAX_PATH];
    DWORD len = GetModuleFileNameA(module, buffer, static_cast<DWORD>(sizeof(buffer)));
    if (len == 0 || len >= sizeof(buffer)) {
        return std::string();
    }
    std::string fullPath(buffer, len);
    size_t slash = fullPath.find_last_of("\\/");
    if (slash == std::string::npos) {
        return std::string();
    }
    return fullPath.substr(0, slash);
}

// Returns the directory of the host process executable (the game dir), or "" on failure.
std::string GameDirectory() {
    char buffer[MAX_PATH];
    DWORD len = GetModuleFileNameA(nullptr, buffer, static_cast<DWORD>(sizeof(buffer)));
    if (len == 0 || len >= sizeof(buffer)) {
        return std::string();
    }
    std::string fullPath(buffer, len);
    size_t slash = fullPath.find_last_of("\\/");
    if (slash == std::string::npos) {
        return std::string();
    }
    return fullPath.substr(0, slash);
}

// Returns the %TEMP% directory (without trailing separator), or "" on failure.
std::string TempDirectory() {
    char buffer[MAX_PATH + 1];
    DWORD len = GetTempPathA(static_cast<DWORD>(sizeof(buffer)), buffer);
    if (len == 0 || len > MAX_PATH) {
        return std::string();
    }
    std::string path(buffer, len);
    while (!path.empty() && (path.back() == '\\' || path.back() == '/')) {
        path.pop_back();
    }
    return path;
}

} // namespace

void Init(bool allocConsole) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_initialized) {
        return;
    }

    if (allocConsole) {
        // Only allocate if this process does not already own a console.
        if (GetConsoleWindow() == nullptr) {
            if (AllocConsole()) {
                g_consoleAllocated = true;
                // Redirect standard streams to the new console.
                freopen_s(&g_stdoutStream, "CONOUT$", "w", stdout);
                freopen_s(&g_stderrStream, "CONOUT$", "w", stderr);
            }
        }
        SetConsoleTitleA("HelloNeighorBot");
    }

    // Open the log file: prefer the module dir, then the game dir, then %TEMP%.
    if (!TryOpenLogIn(ModuleDirectory())) {
        if (!TryOpenLogIn(GameDirectory())) {
            TryOpenLogIn(TempDirectory());
        }
    }

    g_initialized = true;
}

void Shutdown() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_initialized) {
        return;
    }

    if (g_file.is_open()) {
        g_file.flush();
        g_file.close();
    }

    if (g_consoleAllocated) {
        if (g_stdoutStream != nullptr) {
            std::fclose(g_stdoutStream);
            g_stdoutStream = nullptr;
        }
        if (g_stderrStream != nullptr) {
            std::fclose(g_stderrStream);
            g_stderrStream = nullptr;
        }
        FreeConsole();
        g_consoleAllocated = false;
    }

    g_ring.clear();
    g_initialized = false;
}

void Logf(Level level, const char* fmt, ...) {
    if (fmt == nullptr) {
        return;
    }

    // Format the user message into a fixed buffer.
    char message[2048];
    va_list args;
    va_start(args, fmt);
    int written = std::vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);
    if (written < 0) {
        message[0] = '\0';
    } else if (static_cast<size_t>(written) >= sizeof(message)) {
        message[sizeof(message) - 1] = '\0';
    }

    // Build the prefixed line: "[HH:MM:SS][LEVEL] message".
    char timeBuf[16];
    FormatTime(timeBuf, sizeof(timeBuf));

    std::string line;
    line.reserve(std::strlen(message) + 32);
    line += '[';
    line += timeBuf;
    line += "][";
    line += LevelTag(level);
    line += "] ";
    line += message;

    std::lock_guard<std::mutex> lock(g_mutex);

    // Console / stdout.
    std::fputs(line.c_str(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);

    // File.
    if (g_file.is_open()) {
        g_file << line << '\n';
        g_file.flush();
    }

    // Bounded ring buffer (oldest -> newest).
    g_ring.push_back(std::move(line));
    while (g_ring.size() > kMaxRingLines) {
        g_ring.pop_front();
    }
}

std::vector<std::string> Tail(size_t maxLines) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (maxLines == 0 || g_ring.empty()) {
        return std::vector<std::string>();
    }

    size_t count = g_ring.size();
    size_t take = (maxLines < count) ? maxLines : count;
    size_t start = count - take;

    std::vector<std::string> out;
    out.reserve(take);
    for (size_t i = start; i < count; ++i) {
        out.push_back(g_ring[i]);
    }
    return out;
}

} // namespace logger
