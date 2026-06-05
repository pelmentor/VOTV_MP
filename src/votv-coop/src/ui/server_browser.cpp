// ui/server_browser.cpp -- see ui/server_browser.h.
//
// MULTIPLAYER server browser, rendered as an ImGui modal over VOTV's main menu.
// Row model ported from MTA's CServerListItem (reference/mtasa-blue/Client/core/
// ServerBrowser/CServerList.h:95-310): name, address (ip:port), players cur/max,
// ping, version, world/gamemode, passworded. The live feed (master fetch + LAN
// discovery + per-server ping) is P3 (coop/net/lobby_client + lobby_announcer +
// the VPS master); this file owns the table UI + the Connect / Host / Direct-IP
// controls so the whole MULTIPLAYER entry point is in place and screenshot-able.

#include "ui/server_browser.h"

#include "ue_wrap/log.h"

#include "imgui.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <vector>

namespace ui::server_browser {
namespace {

std::atomic<bool> g_open{false};

// One server row (MTA CServerListItem minus the ASE-specific cruft). Render-thread
// only -- the list is seeded once and (P3) refreshed from the master/LAN feed.
struct ServerRow {
    char name[64];
    char address[48];   // ip:port (also the Connect target + the dedup key in MTA)
    int  playersCur;
    int  playersMax;
    int  ping;          // ms; -1 == unknown / not yet queried
    char world[40];     // gamemode / map -- VOTV "world" the host is in
    char version[24];
    bool locked;        // passworded
};

std::vector<ServerRow> g_rows;
int  g_selected = -1;
char g_directIp[48] = "127.0.0.1:7777";
char g_status[160] = "";

void SetStatus(const char* s) { std::snprintf(g_status, sizeof(g_status), "%s", s); }

ServerRow MakeRow(const char* name, const char* addr, int cur, int max, int ping,
                  const char* world, const char* ver, bool locked) {
    ServerRow r{};
    std::snprintf(r.name, sizeof(r.name), "%s", name);
    std::snprintf(r.address, sizeof(r.address), "%s", addr);
    r.playersCur = cur; r.playersMax = max; r.ping = ping;
    std::snprintf(r.world, sizeof(r.world), "%s", world);
    std::snprintf(r.version, sizeof(r.version), "%s", ver);
    r.locked = locked;
    return r;
}

// Seed the list once. These are PLACEHOLDER rows (clearly captioned in the UI) so
// the table renders populated before the master-server feed (P3) is wired -- they
// demonstrate the column model + the Connect flow, not real online servers.
void EnsureSeeded() {
    static bool seeded = false;
    if (seeded) return;
    seeded = true;
    g_rows.push_back(MakeRow("Local game", "127.0.0.1:7777", 1, 4, 0, "Site-23 (story)", "0.9.0-n", false));
    g_rows.push_back(MakeRow("Dr. Bao's Observatory", "192.168.1.50:7777", 2, 4, 14, "Site-23 (story)", "0.9.0-n", false));
    g_rows.push_back(MakeRow("Kerfur Co-op Night", "playvotv.example.net:7777", 3, 4, 47, "Sandbox", "0.9.0-n", true));
    g_rows.push_back(MakeRow("Argemia Relay", "10.0.0.7:7777", 0, 4, 88, "Site-23 (story)", "0.9.0-n", false));
}

// Connect to a server address. P3 wires this to coop::net::Session::Start(Client,
// ip) + the fresh-boot / host-world-mirror path; for now it records intent so the
// flow is observable in the log + the status line.
void DoConnect(const char* address) {
    if (!address || !address[0]) { SetStatus("Connect: no address."); return; }
    char buf[160];
    std::snprintf(buf, sizeof(buf), "Connecting to %s ...", address);
    SetStatus(buf);
    UE_LOGI("server_browser: Connect requested -> %s (P2 wires Session::Start(Client))", address);
}

void DoHost() {
    SetStatus("Host: starting a listen server ... (P2 wires Session::Start(Host))");
    UE_LOGI("server_browser: Host requested (P2 wires Session::Start(Host) + save picker)");
}

void DoRefresh() {
    SetStatus("Refresh: master-server feed is P3 (showing placeholder list).");
    UE_LOGI("server_browser: Refresh requested (P3 wires lobby_client master GET + LAN discovery)");
}

}  // namespace

void Open()  { g_open.store(true,  std::memory_order_relaxed); }
void Close() { g_open.store(false, std::memory_order_relaxed); }
void Toggle(){ g_open.store(!g_open.load(std::memory_order_relaxed), std::memory_order_relaxed); }
bool IsOpen(){ return g_open.load(std::memory_order_relaxed); }

void Render() {
    if (!IsOpen()) return;
    EnsureSeeded();

    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(760.0f, 470.0f), ImGuiCond_Appearing);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 14.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06f, 0.07f, 0.09f, 0.97f));

    bool open = true;
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("MULTIPLAYER###coop_browser", &open, flags)) {
        // Header.
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.96f, 0.98f, 1.00f, 1.0f));
        ImGui::TextUnformatted("Server browser");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextDisabled("(Voices of the Void  -  Coop 0.9.0-n)");
        ImGui::Spacing();

        // Top controls row.
        if (ImGui::Button("Refresh")) DoRefresh();
        ImGui::SameLine();
        if (ImGui::Button("Host Game")) DoHost();
        ImGui::SameLine(0.0f, 24.0f);
        ImGui::TextDisabled("Direct connect:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200.0f);
        ImGui::InputText("##directip", g_directIp, sizeof(g_directIp));
        ImGui::SameLine();
        if (ImGui::Button("Connect##direct")) DoConnect(g_directIp);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Server table (MTA column model). Scrollable; the last row of controls is
        // reserved below, so the table takes the remaining height minus a footer.
        const float footer = ImGui::GetFrameHeightWithSpacing() * 2.2f;
        const ImGuiTableFlags tflags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                       ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable |
                                       ImGuiTableFlags_PadOuterX;
        if (ImGui::BeginTable("##serverlist", 6, tflags, ImVec2(0.0f, -footer))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("",        ImGuiTableColumnFlags_WidthFixed, 22.0f);   // lock
            ImGui::TableSetupColumn("Name",    ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Players", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn("Ping",    ImGuiTableColumnFlags_WidthFixed, 54.0f);
            ImGui::TableSetupColumn("World",   ImGuiTableColumnFlags_WidthFixed, 140.0f);
            ImGui::TableSetupColumn("Version", ImGuiTableColumnFlags_WidthFixed, 76.0f);
            ImGui::TableHeadersRow();

            for (int i = 0; i < static_cast<int>(g_rows.size()); ++i) {
                const ServerRow& r = g_rows[i];
                ImGui::TableNextRow();
                ImGui::PushID(i);

                ImGui::TableSetColumnIndex(0);
                if (r.locked) ImGui::TextColored(ImVec4(1.00f, 0.78f, 0.35f, 1.0f), "L");

                ImGui::TableSetColumnIndex(1);
                // Whole row is selectable (spans columns); double-click connects.
                if (ImGui::Selectable(r.name, g_selected == i,
                                      ImGuiSelectableFlags_SpanAllColumns |
                                      ImGuiSelectableFlags_AllowDoubleClick)) {
                    g_selected = i;
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) DoConnect(r.address);
                }

                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%d/%d", r.playersCur, r.playersMax);
                ImGui::TableSetColumnIndex(3);
                if (r.ping < 0) ImGui::TextDisabled("--");
                else            ImGui::Text("%d", r.ping);
                ImGui::TableSetColumnIndex(4);
                ImGui::TextUnformatted(r.world);
                ImGui::TableSetColumnIndex(5);
                ImGui::TextDisabled("%s", r.version);

                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        // Footer: selection-aware Connect + the placeholder-data caption + status.
        const bool hasSel = g_selected >= 0 && g_selected < static_cast<int>(g_rows.size());
        if (!hasSel) ImGui::BeginDisabled();
        if (ImGui::Button("Connect", ImVec2(120.0f, 0.0f)) && hasSel)
            DoConnect(g_rows[g_selected].address);
        if (!hasSel) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(90.0f, 0.0f))) open = false;
        ImGui::SameLine(0.0f, 18.0f);
        ImGui::TextDisabled("Placeholder list -- live servers arrive with the master-server feed.");

        if (g_status[0]) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.00f, 1.0f), "%s", g_status);
        }
    }
    ImGui::End();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    if (!open) Close();  // title-bar X or the Close button
}

}  // namespace ui::server_browser
