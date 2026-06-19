// ImGui menu contents, drawn each frame from the Present hook in Overlay.cpp.
#include "ui/Menu.h"

#include "Bot.h"
#include "Logger.h"

#include "imgui.h"

#include <cfloat>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

namespace {

// Map a config "default_mode" string to a MoveMode. Anything that isn't an
// explicit "walk" (case-insensitive) is treated as Teleport.
MoveMode ModeFromString(const std::string& s) {
    if (s.size() == 4 &&
        (s[0] == 'w' || s[0] == 'W') &&
        (s[1] == 'a' || s[1] == 'A') &&
        (s[2] == 'l' || s[2] == 'L') &&
        (s[3] == 'k' || s[3] == 'K')) {
        return MoveMode::Walk;
    }
    return MoveMode::Teleport;
}

const char* StateName(RouteState st) {
    switch (st) {
        case RouteState::Idle:     return "Idle";
        case RouteState::Running:  return "Running";
        case RouteState::Paused:   return "Paused";
        case RouteState::Finished: return "Finished";
    }
    return "Unknown";
}

} // namespace

namespace menu {

void Render(Bot& bot) {
    SdkAccess&   sdk    = bot.Sdk();
    RouteEngine& route  = bot.Route();
    Config&      config = bot.GetConfig();

    ImGui::Begin("HelloNeighorBot", &bot.MenuVisible());

    // --- Status -----------------------------------------------------------
    const bool ready = sdk.IsReady();
    ImGui::Text("SDK ready: %s", ready ? "yes" : "no");

    ue4::FVector loc;
    const bool haveLoc = sdk.GetLocation(loc);
    if (haveLoc) {
        ImGui::Text("Location: %s", loc.ToString().c_str());
    } else {
        ImGui::Text("Location: n/a");
    }

    ImGui::Separator();

    ImGui::Text("Route: %s", route.Name().c_str());
    ImGui::Text("State: %s", StateName(route.State()));

    const size_t count = route.Waypoints().size();
    ImGui::Text("Waypoint: %zu / %zu", route.CurrentIndex(), count);

    // Timer formatted as mm:ss.mmm
    const double elapsedMs = route.ElapsedMs();
    long long totalMs = (elapsedMs > 0.0) ? static_cast<long long>(elapsedMs) : 0;
    const long long minutes = totalMs / 60000;
    const long long seconds = (totalMs / 1000) % 60;
    const long long millis  = totalMs % 1000;
    ImGui::Text("Timer: %02lld:%02lld.%03lld", minutes, seconds, millis);

    ImGui::Text("Splits: %zu", route.Splits().size());

    ImGui::Separator();

    // --- Run controls -----------------------------------------------------
    if (ImGui::Button("Start")) { route.Start(); }
    ImGui::SameLine();
    if (ImGui::Button("Stop")) { route.Stop(); }
    ImGui::SameLine();
    if (ImGui::Button("Pause")) { route.Pause(); }
    ImGui::SameLine();
    if (ImGui::Button("Reset")) { route.Reset(); }

    if (ImGui::Button("Capture Waypoint")) {
        ue4::FVector cur;
        if (sdk.GetLocation(cur)) {
            route.CaptureWaypoint(cur,
                                  ModeFromString(config.settings.default_mode),
                                  config.settings.arrival_tolerance);
        } else {
            LOG_WARN("Capture Waypoint: no live location available");
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Teleport->Current")) {
        route.TeleportToCurrent(sdk);
    }

    if (ImGui::Button("Save Route")) {
        const std::string path =
            config.route_path.empty() ? std::string("config/routes/captured.json")
                                      : config.route_path;
        route.SaveRoute(path);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload Config")) {
        bot.ReloadConfig();
    }

    ImGui::Separator();

    // --- Waypoint editor --------------------------------------------------
    ImGui::Text("Waypoints");

    std::vector<Waypoint>& wps = route.Waypoints();
    int deleteIndex = -1; // defer erase until after the loop

    if (ImGui::BeginTable("waypoints", 6,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_ScrollY,
                          ImVec2(0.0f, 180.0f))) {
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Position");
        ImGui::TableSetupColumn("Mode");
        ImGui::TableSetupColumn("Tol");
        ImGui::TableSetupColumn("Go");
        ImGui::TableSetupColumn("Del");
        ImGui::TableHeadersRow();

        for (int i = 0; i < static_cast<int>(wps.size()); ++i) {
            Waypoint& wp = wps[static_cast<size_t>(i)];
            ImGui::TableNextRow();
            ImGui::PushID(i);

            // Name
            ImGui::TableSetColumnIndex(0);
            char nameBuf[128];
            std::snprintf(nameBuf, sizeof(nameBuf), "%s", wp.name.c_str());
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputText("##name", nameBuf, sizeof(nameBuf))) {
                wp.name = nameBuf;
            }

            // Position (xyz)
            ImGui::TableSetColumnIndex(1);
            float xyz[3] = { wp.pos.X, wp.pos.Y, wp.pos.Z };
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputFloat3("##xyz", xyz, "%.1f")) {
                wp.pos.X = xyz[0];
                wp.pos.Y = xyz[1];
                wp.pos.Z = xyz[2];
            }

            // Mode combo
            ImGui::TableSetColumnIndex(2);
            static const char* kModeItems[] = { "Teleport", "Walk" };
            int modeIdx = (wp.mode == MoveMode::Walk) ? 1 : 0;
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::Combo("##mode", &modeIdx, kModeItems,
                             IM_ARRAYSIZE(kModeItems))) {
                wp.mode = (modeIdx == 1) ? MoveMode::Walk : MoveMode::Teleport;
            }

            // Tolerance
            ImGui::TableSetColumnIndex(3);
            float tol = wp.tolerance;
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputFloat("##tol", &tol, 0.0f, 0.0f, "%.1f")) {
                wp.tolerance = tol;
            }

            // Go
            ImGui::TableSetColumnIndex(4);
            if (ImGui::Button("Go")) {
                sdk.SetLocation(wp.pos);
            }

            // Delete
            ImGui::TableSetColumnIndex(5);
            if (ImGui::Button("X")) {
                deleteIndex = i;
            }

            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    if (deleteIndex >= 0 && deleteIndex < static_cast<int>(wps.size())) {
        wps.erase(wps.begin() + deleteIndex);
    }

    ImGui::Separator();

    // --- Log pane ---------------------------------------------------------
    ImGui::Text("Log");
    if (ImGui::BeginChild("log", ImVec2(0.0f, 160.0f), true,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
        std::vector<std::string> lines = logger::Tail(15);
        for (const std::string& line : lines) {
            ImGui::TextUnformatted(line.c_str());
        }
    }
    ImGui::EndChild();

    ImGui::End();
}

} // namespace menu
