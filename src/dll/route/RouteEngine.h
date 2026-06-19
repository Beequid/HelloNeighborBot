#pragma once
#include <string>
#include <vector>
#include "ue4/UE4.h"

class SdkAccess;

enum class MoveMode { Teleport, Walk };
enum class RouteState { Idle, Running, Paused, Finished };

struct Waypoint {
    std::string  name;
    ue4::FVector pos;
    float        tolerance = 150.f;
    MoveMode     mode = MoveMode::Teleport;
    int          wait_ms = 0;
    std::string  action;   // optional, e.g. "split"
};

// Executes a list of waypoints by writing the player's location each tick.
// "Teleport" snaps instantly; "Walk" glides at move_speed (a TAS-style mover).
class RouteEngine {
public:
    bool LoadRoute(const std::string& path);
    bool SaveRoute(const std::string& path) const;

    void SetWaypoints(std::vector<Waypoint> wps);
    const std::vector<Waypoint>& Waypoints() const { return waypoints_; }
    std::vector<Waypoint>& Waypoints() { return waypoints_; }

    void Start();
    void Stop();
    void Pause();   // Running <-> Paused
    void Reset();   // -> Idle, index 0, timer/splits cleared
    void CaptureWaypoint(const ue4::FVector& pos, MoveMode mode, float tolerance);

    // Advance the run by one tick. dtMs = ms since the previous Tick.
    void Tick(SdkAccess& sdk, double dtMs);
    // Immediately place the player at the current waypoint.
    void TeleportToCurrent(SdkAccess& sdk);

    RouteState State() const { return state_; }
    size_t CurrentIndex() const { return index_; }
    double ElapsedMs() const;
    const std::vector<double>& Splits() const { return splits_; }

    void SetDefaults(float moveSpeed, float teleportStep, MoveMode defaultMode, bool loop);
    void SetName(const std::string& n) { name_ = n; }
    const std::string& Name() const { return name_; }

private:
    void recordSplit();

    std::string           name_ = "unnamed route";
    std::vector<Waypoint> waypoints_;
    std::vector<double>   splits_;
    RouteState            state_ = RouteState::Idle;
    size_t                index_ = 0;
    double                startTick_ = 0.0; // ms (GetTickCount64)
    double                pauseAccum_ = 0.0;
    double                pauseStart_ = 0.0;
    double                waitUntil_ = 0.0; // ms timestamp to hold at a waypoint

    float    moveSpeed_ = 1500.f;
    float    teleportStep_ = 250.f;
    MoveMode defaultMode_ = MoveMode::Teleport;
    bool     loop_ = false;
};
