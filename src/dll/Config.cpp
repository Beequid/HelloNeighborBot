#include "Config.h"
#include "Logger.h"

#include <nlohmann/json.hpp>

#include <windows.h>
#include <shlwapi.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

#pragma comment(lib, "Shlwapi.lib")

using json = nlohmann::json;

namespace {

// Parse a single config value that may be a JSON string ("0x180" / "384") or a
// JSON number into a uintptr_t. Anything else (null/bool/etc.) yields 0.
uintptr_t parseUintptr(const json& v) {
    if (v.is_string()) {
        const std::string s = v.get<std::string>();
        if (s.empty()) {
            return 0;
        }
        try {
            // base 0 => "0x.." is hex, leading 0 is octal, otherwise decimal.
            return static_cast<uintptr_t>(std::stoull(s, nullptr, 0));
        } catch (const std::exception&) {
            return 0;
        }
    }
    if (v.is_number_unsigned()) {
        return static_cast<uintptr_t>(v.get<unsigned long long>());
    }
    if (v.is_number_integer()) {
        return static_cast<uintptr_t>(v.get<long long>());
    }
    if (v.is_number_float()) {
        return static_cast<uintptr_t>(v.get<double>());
    }
    return 0;
}

// Parse an array of offsets into a pointer chain. Non-array input -> empty.
std::vector<uintptr_t> parseChain(const json& arr) {
    std::vector<uintptr_t> chain;
    if (!arr.is_array()) {
        return chain;
    }
    chain.reserve(arr.size());
    for (const auto& el : arr) {
        chain.push_back(parseUintptr(el));
    }
    return chain;
}

// Directory portion of a full path (no trailing slash), or "" if none.
std::string DirOf(const std::string& fullPath) {
    const size_t pos = fullPath.find_last_of("\\/");
    if (pos == std::string::npos) {
        return "";
    }
    return fullPath.substr(0, pos);
}

bool FileExists(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    if (PathFileExistsA(path.c_str())) {
        return true;
    }
    // Fallback: PathFileExists can be picky; try opening directly.
    std::ifstream f(path, std::ios::binary);
    return f.good();
}

std::string ToUpperAscii(const std::string& in) {
    std::string out = in;
    for (char& c : out) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return out;
}

} // namespace

int Settings::Hotkey(const std::string& name, int fallback) const {
    return hotkeys.count(name) ? hotkeys.at(name) : fallback;
}

bool Config::Load(const std::string& path, Config& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in.good()) {
        LOG_ERROR("Config::Load: cannot open '%s'", path.c_str());
        return false;
    }

    std::stringstream ss;
    ss << in.rdbuf();
    const std::string text = ss.str();

    json root;
    try {
        root = json::parse(text);
    } catch (const std::exception& e) {
        LOG_ERROR("Config::Load: JSON parse error in '%s': %s", path.c_str(), e.what());
        return false;
    }

    if (!root.is_object()) {
        LOG_ERROR("Config::Load: top-level JSON in '%s' is not an object", path.c_str());
        return false;
    }

    // Top-level strings (keep struct defaults when missing).
    out.process_name = root.value("process_name", out.process_name);
    out.module_name  = root.value("module_name", out.module_name);
    out.route_path   = root.value("route_path", out.route_path);

    // resolve.gworld
    if (root.contains("resolve") && root["resolve"].is_object()) {
        const json& resolve = root["resolve"];
        if (resolve.contains("gworld") && resolve["gworld"].is_object()) {
            const json& gw = resolve["gworld"];
            out.gworld.mode = gw.value("mode", out.gworld.mode);
            if (gw.contains("rva")) {
                out.gworld.rva = parseUintptr(gw["rva"]);
            }
            out.gworld.pattern = gw.value("pattern", out.gworld.pattern);
            out.gworld.pattern_offset = gw.value("pattern_offset", out.gworld.pattern_offset);
            out.gworld.instruction_length =
                gw.value("instruction_length", out.gworld.instruction_length);
            out.gworld.rip_relative = gw.value("rip_relative", out.gworld.rip_relative);
        }
    }

    // player.{location,velocity,rotation}_chain
    if (root.contains("player") && root["player"].is_object()) {
        const json& player = root["player"];
        if (player.contains("location_chain")) {
            out.player.location_chain = parseChain(player["location_chain"]);
        }
        if (player.contains("velocity_chain")) {
            out.player.velocity_chain = parseChain(player["velocity_chain"]);
        }
        if (player.contains("rotation_chain")) {
            out.player.rotation_chain = parseChain(player["rotation_chain"]);
        }
    }

    // settings.*
    if (root.contains("settings") && root["settings"].is_object()) {
        const json& s = root["settings"];
        out.settings.enable_overlay   = s.value("enable_overlay", out.settings.enable_overlay);
        out.settings.enable_console   = s.value("enable_console", out.settings.enable_console);
        out.settings.move_speed       = s.value("move_speed", out.settings.move_speed);
        out.settings.arrival_tolerance = s.value("arrival_tolerance", out.settings.arrival_tolerance);
        out.settings.default_mode     = s.value("default_mode", out.settings.default_mode);
        out.settings.teleport_step    = s.value("teleport_step", out.settings.teleport_step);
        out.settings.loop_route       = s.value("loop_route", out.settings.loop_route);
        out.settings.tick_interval_ms = s.value("tick_interval_ms", out.settings.tick_interval_ms);

        if (s.contains("hotkeys") && s["hotkeys"].is_object()) {
            for (auto it = s["hotkeys"].begin(); it != s["hotkeys"].end(); ++it) {
                const std::string& key = it.key();
                if (key == "_comment") {
                    continue;
                }
                const json& val = it.value();
                if (val.is_string()) {
                    out.settings.hotkeys[key] = ParseVk(val.get<std::string>(), 0);
                } else if (val.is_number_integer() || val.is_number_unsigned()) {
                    out.settings.hotkeys[key] = static_cast<int>(val.get<long long>());
                }
            }
        }
    }

    LOG_INFO("Config::Load: loaded '%s'", path.c_str());
    return true;
}

std::string Config::FindConfigFile(void* dllModule) {
    std::vector<std::string> candidates;

    // 1 & 2: relative to the DLL's own directory.
    {
        char buf[MAX_PATH] = {0};
        const DWORD n = GetModuleFileNameA(reinterpret_cast<HMODULE>(dllModule), buf, MAX_PATH);
        if (n > 0 && n < MAX_PATH) {
            const std::string dllDir = DirOf(std::string(buf, n));
            if (!dllDir.empty()) {
                candidates.push_back(dllDir + "\\config.json");
                candidates.push_back(dllDir + "\\config\\config.json");
            }
        }
    }

    // 3 & 4: relative to the main executable's directory.
    {
        char buf[MAX_PATH] = {0};
        const DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
        if (n > 0 && n < MAX_PATH) {
            const std::string exeDir = DirOf(std::string(buf, n));
            if (!exeDir.empty()) {
                candidates.push_back(exeDir + "\\config.json");
                candidates.push_back(exeDir + "\\config\\config.json");
            }
        }
    }

    // 5: %HNBOT_CONFIG% (may be a full path to the file).
    {
        char buf[MAX_PATH] = {0};
        const DWORD n = GetEnvironmentVariableA("HNBOT_CONFIG", buf, MAX_PATH);
        if (n > 0 && n < MAX_PATH) {
            candidates.push_back(std::string(buf, n));
        }
    }

    for (const std::string& c : candidates) {
        if (FileExists(c)) {
            return c;
        }
    }

    return "";
}

int Config::ParseVk(const std::string& name, int fallback) {
    if (name.empty()) {
        return fallback;
    }

    std::string s = ToUpperAscii(name);

    // Trim leading/trailing whitespace.
    const size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return fallback;
    }
    const size_t last = s.find_last_not_of(" \t\r\n");
    s = s.substr(first, last - first + 1);

    // Strip optional "VK_" prefix.
    if (s.size() > 3 && s.compare(0, 3, "VK_") == 0) {
        s = s.substr(3);
    }

    if (s.empty()) {
        return fallback;
    }

    // Hex literal "0x.."
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'X')) {
        try {
            return static_cast<int>(std::stoul(s, nullptr, 16));
        } catch (const std::exception&) {
            return fallback;
        }
    }

    // F1..F24
    if (s.size() >= 2 && s[0] == 'F' &&
        std::isdigit(static_cast<unsigned char>(s[1]))) {
        bool allDigits = true;
        for (size_t i = 1; i < s.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(s[i]))) {
                allDigits = false;
                break;
            }
        }
        if (allDigits) {
            try {
                const int fn = std::stoi(s.substr(1));
                if (fn >= 1 && fn <= 24) {
                    return VK_F1 + (fn - 1);
                }
            } catch (const std::exception&) {
                // fall through to named/other handling
            }
        }
    }

    // Named keys.
    if (s == "INSERT")  return VK_INSERT;
    if (s == "DELETE")  return VK_DELETE;
    if (s == "HOME")    return VK_HOME;
    if (s == "END")     return VK_END;
    if (s == "PRIOR" || s == "PAGEUP")   return VK_PRIOR;
    if (s == "NEXT"  || s == "PAGEDOWN") return VK_NEXT;
    if (s == "SPACE")   return VK_SPACE;
    if (s == "RETURN" || s == "ENTER")   return VK_RETURN;
    if (s == "ESCAPE" || s == "ESC")     return VK_ESCAPE;
    if (s == "TAB")     return VK_TAB;
    if (s == "SHIFT")   return VK_SHIFT;
    if (s == "CONTROL" || s == "CTRL")   return VK_CONTROL;
    if (s == "MENU" || s == "ALT")       return VK_MENU;
    if (s == "LEFT")    return VK_LEFT;
    if (s == "RIGHT")   return VK_RIGHT;
    if (s == "UP")      return VK_UP;
    if (s == "DOWN")    return VK_DOWN;
    if (s == "BACK" || s == "BACKSPACE") return VK_BACK;

    // Single A-Z / 0-9 -> ASCII (which equals the VK code for these).
    if (s.size() == 1) {
        const char c = s[0];
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            return static_cast<int>(static_cast<unsigned char>(c));
        }
    }

    // Bare decimal number.
    {
        bool allDigits = true;
        for (char c : s) {
            if (!std::isdigit(static_cast<unsigned char>(c))) {
                allDigits = false;
                break;
            }
        }
        if (allDigits) {
            try {
                return static_cast<int>(std::stoul(s, nullptr, 10));
            } catch (const std::exception&) {
                return fallback;
            }
        }
    }

    return fallback;
}
