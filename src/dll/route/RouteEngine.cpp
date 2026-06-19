#include "route/RouteEngine.h"

#include <algorithm>
#include <fstream>
#include <string>

#include <windows.h>
#include <nlohmann/json.hpp>

#include "ue4/SdkAccess.h"
#include "Logger.h"

using nlohmann::json;

namespace {

// Current time in milliseconds (monotonic since boot).
double nowMs() {
    return static_cast<double>(GetTickCount64());
}

MoveMode ModeFromString(const std::string& s, MoveMode fallback) {
    if (s == "teleport") return MoveMode::Teleport;
    if (s == "walk")     return MoveMode::Walk;
    return fallback;
}

const char* ModeToString(MoveMode m) {
    switch (m) {
        case MoveMode::Walk:     return "walk";
        case MoveMode::Teleport: return "teleport";
    }
    return "teleport";
}

} // namespace

bool RouteEngine::LoadRoute(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        LOG_ERROR("RouteEngine: failed to open route file '%s'", path.c_str());
        return false;
    }

    json doc;
    try {
        in >> doc;
    } catch (const std::exception& e) {
        LOG_ERROR("RouteEngine: failed to parse route '%s': %s", path.c_str(), e.what());
        return false;
    } catch (...) {
        LOG_ERROR("RouteEngine: failed to parse route '%s': unknown error", path.c_str());
        return false;
    }

    try {
        std::string name = doc.value("name", std::string("unnamed route"));
        bool loop = doc.value("loop", loop_);

        std::vector<Waypoint> wps;
        if (doc.contains("waypoints") && doc["waypoints"].is_array()) {
            const json& arr = doc["waypoints"];
            wps.reserve(arr.size());
            for (const auto& jw : arr) {
                if (!jw.is_object()) continue;

                Waypoint wp;
                wp.name = jw.value("name", std::string());
                wp.pos.X = jw.value("x", 0.0f);
                wp.pos.Y = jw.value("y", 0.0f);
                wp.pos.Z = jw.value("z", 0.0f);
                wp.tolerance = jw.value("tolerance", 150.0f);
                wp.mode = ModeFromString(jw.value("mode", std::string()), defaultMode_);
                wp.wait_ms = jw.value("wait_ms", 0);
                wp.action = jw.value("action", std::string());

                wps.push_back(std::move(wp));
            }
        }

        name_ = std::move(name);
        loop_ = loop;
        waypoints_ = std::move(wps);
        Reset();
    } catch (const std::exception& e) {
        LOG_ERROR("RouteEngine: error reading route '%s': %s", path.c_str(), e.what());
        return false;
    } catch (...) {
        LOG_ERROR("RouteEngine: error reading route '%s': unknown error", path.c_str());
        return false;
    }

    LOG_INFO("RouteEngine: loaded route '%s' (%zu waypoints, loop=%s)",
             name_.c_str(), waypoints_.size(), loop_ ? "true" : "false");
    return true;
}

bool RouteEngine::SaveRoute(const std::string& path) const {
    json doc;
    doc["name"] = name_;
    doc["loop"] = loop_;

    json arr = json::array();
    for (const Waypoint& wp : waypoints_) {
        json jw;
        jw["name"]      = wp.name;
        jw["x"]         = wp.pos.X;
        jw["y"]         = wp.pos.Y;
        jw["z"]         = wp.pos.Z;
        jw["mode"]      = ModeToString(wp.mode);
        jw["tolerance"] = wp.tolerance;
        jw["wait_ms"]   = wp.wait_ms;
        jw["action"]    = wp.action;
        arr.push_back(std::move(jw));
    }
    doc["waypoints"] = std::move(arr);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        LOG_ERROR("RouteEngine: failed to open '%s' for writing", path.c_str());
        return false;
    }

    try {
        out << doc.dump(2);
    } catch (const std::exception& e) {
        LOG_ERROR("RouteEngine: failed to serialize route '%s': %s", path.c_str(), e.what());
        return false;
    } catch (...) {
        LOG_ERROR("RouteEngine: failed to serialize route '%s': unknown error", path.c_str());
        return false;
    }

    if (!out.good()) {
        LOG_ERROR("RouteEngine: failed to write route '%s'", path.c_str());
        return false;
    }

    LOG_INFO("RouteEngine: saved route '%s' (%zu waypoints)", path.c_str(), waypoints_.size());
    return true;
}

void RouteEngine::SetWaypoints(std::vector<Waypoint> wps) {
    waypoints_ = std::move(wps);
    Reset();
}

void RouteEngine::Start() {
    if (waypoints_.empty()) {
        LOG_WARN("RouteEngine: cannot start, no waypoints");
        return;
    }
    state_ = RouteState::Running;
    index_ = 0;
    startTick_ = nowMs();
    pauseAccum_ = 0.0;
    pauseStart_ = 0.0;
    waitUntil_ = 0.0;
    splits_.clear();
    LOG_INFO("RouteEngine: started route '%s'", name_.c_str());
}

void RouteEngine::Stop() {
    state_ = RouteState::Idle;
    LOG_INFO("RouteEngine: stopped");
}

void RouteEngine::Pause() {
    if (state_ == RouteState::Running) {
        state_ = RouteState::Paused;
        pauseStart_ = nowMs();
        LOG_INFO("RouteEngine: paused");
    } else if (state_ == RouteState::Paused) {
        pauseAccum_ += nowMs() - pauseStart_;
        state_ = RouteState::Running;
        LOG_INFO("RouteEngine: resumed");
    }
}

void RouteEngine::Reset() {
    state_ = RouteState::Idle;
    index_ = 0;
    splits_.clear();
    startTick_ = 0.0;
    pauseAccum_ = 0.0;
    pauseStart_ = 0.0;
    waitUntil_ = 0.0;
}

void RouteEngine::CaptureWaypoint(const ue4::FVector& pos, MoveMode mode, float tolerance) {
    Waypoint wp;
    wp.name = "wp" + std::to_string(waypoints_.size());
    wp.pos = pos;
    wp.tolerance = tolerance;
    wp.mode = mode;
    wp.wait_ms = 0;
    wp.action.clear();
    waypoints_.push_back(std::move(wp));
    LOG_INFO("RouteEngine: captured waypoint '%s' at %s",
             waypoints_.back().name.c_str(), pos.ToString().c_str());
}

void RouteEngine::Tick(SdkAccess& sdk, double dtMs) {
    if (state_ != RouteState::Running) return;

    if (index_ >= waypoints_.size()) {
        state_ = RouteState::Finished;
        return;
    }

    const double now = nowMs();
    if (now < waitUntil_) return;

    ue4::FVector cur;
    if (!sdk.GetLocation(cur)) return;

    const Waypoint& wp = waypoints_[index_];

    if (wp.mode == MoveMode::Teleport) {
        sdk.SetLocation(wp.pos);

        waitUntil_ = now + wp.wait_ms;
        recordSplit();
        ++index_;
        if (index_ >= waypoints_.size()) {
            if (loop_) {
                index_ = 0;
                splits_.clear();   // fresh splits each lap
            } else {
                state_ = RouteState::Finished;
            }
        }
        return;
    }

    // Walk mode: glide toward the target each tick.
    ue4::FVector dir = wp.pos - cur;
    const float dist = dir.Size();

    if (dist <= wp.tolerance) {
        waitUntil_ = now + wp.wait_ms;
        recordSplit();
        ++index_;
        if (index_ >= waypoints_.size()) {
            if (loop_) {
                index_ = 0;
                splits_.clear();   // fresh splits each lap
            } else {
                state_ = RouteState::Finished;
            }
        }
        return;
    }

    double step = std::min(static_cast<double>(moveSpeed_) * (dtMs / 1000.0),
                           static_cast<double>(teleportStep_));
    ue4::FVector next = cur + dir.Normalized() * static_cast<float>(step);
    sdk.SetLocation(next);
}

void RouteEngine::TeleportToCurrent(SdkAccess& sdk) {
    if (index_ < waypoints_.size()) {
        sdk.SetLocation(waypoints_[index_].pos);
    }
}

double RouteEngine::ElapsedMs() const {
    if (state_ == RouteState::Idle) return 0.0;

    double base = nowMs() - startTick_ - pauseAccum_;
    if (state_ == RouteState::Paused) {
        base -= (nowMs() - pauseStart_);
    }
    return base > 0.0 ? base : 0.0;
}

void RouteEngine::recordSplit() {
    splits_.push_back(ElapsedMs());
}

void RouteEngine::SetDefaults(float moveSpeed, float teleportStep, MoveMode defaultMode, bool loop) {
    moveSpeed_ = moveSpeed;
    teleportStep_ = teleportStep;
    defaultMode_ = defaultMode;
    loop_ = loop;
}
