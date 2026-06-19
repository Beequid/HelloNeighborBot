#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>

// How to locate a global (e.g. GWorld) in the game module.
struct ResolveSpec {
    std::string mode = "pattern";   // "rva" | "pattern"
    uintptr_t   rva = 0;            // used when mode == "rva"
    std::string pattern;           // IDA-style, used when mode == "pattern"
    int         pattern_offset = 0; // byte offset to the displacement / target
    int         instruction_length = 7;
    bool        rip_relative = true;
};

// Cheat-Engine pointer chains from the GWorld variable address (see Pattern.h).
struct PlayerSpec {
    std::vector<uintptr_t> location_chain;
    std::vector<uintptr_t> velocity_chain;
    std::vector<uintptr_t> rotation_chain;
};

struct Settings {
    bool        enable_overlay = true;
    bool        enable_console = true;
    float       move_speed = 1500.f;       // cm/s for "walk" mode
    float       arrival_tolerance = 150.f; // cm
    std::string default_mode = "teleport"; // "teleport" | "walk"
    float       teleport_step = 250.f;     // max cm moved per tick in "walk"
    bool        loop_route = false;
    int         tick_interval_ms = 8;
    std::unordered_map<std::string, int> hotkeys; // name -> VK code

    int Hotkey(const std::string& name, int fallback) const;
};

struct Config {
    std::string process_name = "HelloNeighbor-Win64-Shipping.exe";
    std::string module_name  = "HelloNeighbor-Win64-Shipping.exe";
    ResolveSpec gworld;
    PlayerSpec  player;
    Settings    settings;
    std::string route_path;

    // Parse a JSON config file into `out`. Returns false (and logs) on error.
    static bool Load(const std::string& path, Config& out);

    // Locate config.json: next to the DLL, then game dir, then %HNBOT_CONFIG%.
    // Returns the resolved absolute path, or "" if none found.
    static std::string FindConfigFile(void* dllModule);

    // "VK_F5" / "F5" / "0x74" -> virtual-key code (returns fallback if unknown).
    static int ParseVk(const std::string& name, int fallback = 0);
};
