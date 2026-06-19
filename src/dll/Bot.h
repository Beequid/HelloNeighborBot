#pragma once
#include <windows.h>
#include <atomic>
#include <string>
#include "Config.h"
#include "Hotkeys.h"
#include "ue4/SdkAccess.h"
#include "route/RouteEngine.h"

// Top-level orchestrator. Owns every subsystem and runs the bot's main loop on
// its own thread (started from DllMain). The overlay/menu access it via g_bot.
class Bot {
public:
    void Run(HMODULE self);                 // thread entry; blocks until unload
    void RequestUnload() { unloadRequested_ = true; }

    Config&      GetConfig() { return config_; }
    SdkAccess&   Sdk()       { return sdk_; }
    RouteEngine& Route()     { return route_; }
    bool&        MenuVisible() { return menuVisible_; }

    bool ReloadConfig();                    // re-read config.json + route

private:
    void mainLoop();
    void handleHotkeys();
    void shutdown();

    HMODULE           self_ = nullptr;
    Config            config_;
    SdkAccess         sdk_;
    RouteEngine       route_;
    Hotkeys           hotkeys_;
    bool              menuVisible_ = true;
    std::atomic<bool> unloadRequested_{false};
    bool              running_ = false;
    std::string       configPath_;
};

extern Bot* g_bot;
