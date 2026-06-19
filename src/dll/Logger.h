#pragma once
#include <vector>
#include <string>

namespace logger {

enum class Level { Info, Warn, Error };

// Allocates a console (if allocConsole) and opens the log file. Idempotent.
void Init(bool allocConsole);
void Shutdown();

// printf-style logging to console + file. Thread-safe.
void Logf(Level level, const char* fmt, ...);

// Snapshot of the most recent log lines, for the in-game overlay.
std::vector<std::string> Tail(size_t maxLines);

} // namespace logger

#define LOG_INFO(...)  ::logger::Logf(::logger::Level::Info,  __VA_ARGS__)
#define LOG_WARN(...)  ::logger::Logf(::logger::Level::Warn,  __VA_ARGS__)
#define LOG_ERROR(...) ::logger::Logf(::logger::Level::Error, __VA_ARGS__)
