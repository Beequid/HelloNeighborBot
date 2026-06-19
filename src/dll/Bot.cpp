#include "Bot.h"
#include "Logger.h"
#include "ui/Overlay.h"
#include "mem/Pattern.h"
#include <windows.h>

// g_bot itself is defined in dllmain.cpp (Bot.h declares it extern).

void Bot::Run(HMODULE self) {
    self_ = self;

    // Bootstrap a console immediately so any early errors are visible, even
    // before we've loaded the config that says whether one is wanted.
    logger::Init(true);

    configPath_ = Config::FindConfigFile(self);
    if (configPath_.empty()) {
        LOG_WARN("no config.json found, using defaults");
    } else {
        if (Config::Load(configPath_, config_)) {
            LOG_INFO("loaded config: %s", configPath_.c_str());
        } else {
            LOG_ERROR("failed to load config '%s', using defaults",
                      configPath_.c_str());
        }
    }

    // Keep the console regardless of config.settings.enable_console: it was
    // already allocated for early errors and Init() is idempotent.
    logger::Init(true);

    if (sdk_.Init(config_)) {
        LOG_INFO("SDK ready: GWorld var @ 0x%llX",
                 (unsigned long long)sdk_.GWorldVarAddr());
    } else {
        LOG_WARN("SDK not ready (GWorld unresolved); running headless until fixed");
    }

    const MoveMode defMode =
        (config_.settings.default_mode == "walk") ? MoveMode::Walk
                                                  : MoveMode::Teleport;
    route_.SetDefaults(config_.settings.move_speed,
                       config_.settings.teleport_step,
                       defMode,
                       config_.settings.loop_route);

    if (!config_.route_path.empty()) {
        if (route_.LoadRoute(config_.route_path)) {
            LOG_INFO("loaded route '%s' (%zu waypoints)",
                     route_.Name().c_str(), route_.Waypoints().size());
        } else {
            LOG_WARN("failed to load route: %s", config_.route_path.c_str());
        }
    }

    menuVisible_ = true;

    if (config_.settings.enable_overlay) {
        if (overlay::Init(this)) {
            LOG_INFO("overlay initialized");
        } else {
            LOG_WARN("overlay init failed; continuing headless");
        }
    }

    running_ = true;
    mainLoop();
    shutdown();
}

void Bot::mainLoop() {
    ULONGLONG last = GetTickCount64();
    while (!unloadRequested_) {
        ULONGLONG now = GetTickCount64();
        double dt = (double)(now - last);
        last = now;

        handleHotkeys();

        if (sdk_.IsReady()) {
            route_.Tick(sdk_, dt);
        }

        Sleep(config_.settings.tick_interval_ms > 0
                  ? (DWORD)config_.settings.tick_interval_ms
                  : 8);
    }
}

void Bot::handleHotkeys() {
    // Resolve each configurable hotkey from the config, falling back to the
    // documented default virtual-key when unset.
    auto K = [&](const char* name, const char* defaultName) -> int {
        return config_.settings.Hotkey(name, Config::ParseVk(defaultName));
    };

    const int vkToggleMenu  = K("toggle_menu",       "VK_INSERT");
    const int vkStart       = K("start",             "VK_F5");
    const int vkStop        = K("stop",              "VK_F6");
    const int vkPause       = K("pause",             "VK_F7");
    const int vkCapture     = K("capture_waypoint",  "VK_F8");
    const int vkTeleportNxt = K("teleport_next",     "VK_F9");
    const int vkReload      = K("reload_config",     "VK_F10");
    const int vkUnload      = K("unload",            "VK_END");

    if (hotkeys_.Pressed(vkToggleMenu)) {
        menuVisible_ = !menuVisible_;
        LOG_INFO("hotkey: toggle menu -> %s", menuVisible_ ? "shown" : "hidden");
    }

    if (hotkeys_.Pressed(vkStart)) {
        route_.Start();
        LOG_INFO("hotkey: start route");
    }

    if (hotkeys_.Pressed(vkStop)) {
        route_.Stop();
        LOG_INFO("hotkey: stop route");
    }

    if (hotkeys_.Pressed(vkPause)) {
        route_.Pause();
        LOG_INFO("hotkey: pause/resume route");
    }

    if (hotkeys_.Pressed(vkCapture)) {
        ue4::FVector loc;
        if (sdk_.GetLocation(loc)) {
            const MoveMode mode =
                (config_.settings.default_mode == "walk") ? MoveMode::Walk
                                                          : MoveMode::Teleport;
            route_.CaptureWaypoint(loc, mode, config_.settings.arrival_tolerance);
            LOG_INFO("hotkey: captured waypoint @ %s", loc.ToString().c_str());
        } else {
            LOG_WARN("hotkey: capture failed (no location)");
        }
    }

    if (hotkeys_.Pressed(vkTeleportNxt)) {
        route_.TeleportToCurrent(sdk_);
        LOG_INFO("hotkey: teleport to current waypoint");
    }

    if (hotkeys_.Pressed(vkReload)) {
        LOG_INFO("hotkey: reload config");
        ReloadConfig();
    }

    if (hotkeys_.Pressed(vkUnload)) {
        LOG_INFO("hotkey: unload requested");
        RequestUnload();
    }
}

bool Bot::ReloadConfig() {
    if (configPath_.empty()) {
        LOG_WARN("reload config: no config path");
        return false;
    }

    Config tmp;
    if (!Config::Load(configPath_, tmp)) {
        LOG_ERROR("reload config: failed to parse '%s'", configPath_.c_str());
        return false;
    }

    config_ = tmp;

    sdk_.Init(config_);

    const MoveMode defMode =
        (config_.settings.default_mode == "walk") ? MoveMode::Walk
                                                  : MoveMode::Teleport;
    route_.SetDefaults(config_.settings.move_speed,
                       config_.settings.teleport_step,
                       defMode,
                       config_.settings.loop_route);

    if (!config_.route_path.empty()) {
        route_.LoadRoute(config_.route_path);
    }

    LOG_INFO("config reloaded from %s", configPath_.c_str());
    return true;
}

void Bot::shutdown() {
    LOG_INFO("unloading");

    if (overlay::IsInitialized()) {
        overlay::Shutdown();
    }

    running_ = false;
    logger::Shutdown();

    FreeLibraryAndExitThread(self_, 0);
}
