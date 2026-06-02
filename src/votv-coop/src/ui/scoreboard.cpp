// ui/scoreboard.cpp -- see ui/scoreboard.h.

#include "ui/scoreboard.h"

#include "coop/roster.h"

#include "imgui.h"

namespace ui::scoreboard {
namespace {

// A small filled status dot drawn inline before a name (green = connected). Uses
// the window draw list + a Dummy spacer so the following SameLine() name lands
// just to its right, vertically centred on the text line.
void StatusDot(bool connected) {
    const ImVec4 col = connected ? ImVec4(0.36f, 0.85f, 0.42f, 1.0f)
                                 : ImVec4(0.60f, 0.60f, 0.62f, 1.0f);
    const float radius = 4.0f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddCircleFilled(
        ImVec2(p.x + radius, p.y + ImGui::GetTextLineHeight() * 0.5f),
        radius, ImGui::GetColorU32(col));
    ImGui::Dummy(ImVec2(radius * 2.0f + 6.0f, ImGui::GetTextLineHeight()));
}

}  // namespace

bool LocalIsHost() { return coop::roster::LocalIsHost(); }  // lock-free (overlay hot path)

void Render() {
    coop::roster::Snapshot s;
    coop::roster::GetSnapshot(s);

    const ImGuiIO& io = ImGui::GetIO();
    // Top-centre, pinned. Pivot (0.5, 0) keeps it centred regardless of width.
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.14f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.0f));  // upper-centre, below the top HUD
    ImGui::SetNextWindowSize(ImVec2(300.0f, 0.0f), ImGuiCond_Always);  // fixed width, auto height

    // Clean translucent panel: padded, rounded, borderless, dark bg so it reads over
    // any scene. Own header (no OS-style title bar).
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 13.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(6.0f, 6.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06f, 0.07f, 0.09f, 0.94f));

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize;

    if (ImGui::Begin("###coop_scoreboard", nullptr, flags)) {
        // Header: bright "PLAYERS" + accent online count.
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.96f, 0.98f, 1.00f, 1.0f));
        ImGui::TextUnformatted("PLAYERS");
        ImGui::PopStyleColor();
        ImGui::SameLine(0.0f, 8.0f);
        if (s.inSession) ImGui::TextColored(ImVec4(0.45f, 0.78f, 1.00f, 1.0f), "%d online", s.count);
        else             ImGui::TextDisabled("offline");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Role is conveyed by NICK COLOUR (host = gold, client = soft white) -- no
        // separate role text column. "(you)" marks the local player.
        const ImGuiTableFlags tflags = ImGuiTableFlags_RowBg | ImGuiTableFlags_PadOuterX;
        if (ImGui::BeginTable("##roster", 1, tflags)) {
            ImGui::TableSetupColumn("Player", ImGuiTableColumnFlags_WidthStretch);
            for (int i = 0; i < s.count; ++i) {
                const coop::roster::Row& r = s.rows[i];
                const char* nick = r.nick[0] ? r.nick : (r.isLocal ? "Player" : "Remote player");
                const ImVec4 nickCol = r.isHost ? ImVec4(1.00f, 0.82f, 0.35f, 1.0f)   // host  = gold
                                                : ImVec4(0.86f, 0.89f, 0.94f, 1.0f);  // client = soft white
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                StatusDot(r.connected);
                ImGui::SameLine();
                ImGui::TextColored(nickCol, "%s", nick);
                if (r.isLocal) { ImGui::SameLine(0.0f, 6.0f); ImGui::TextDisabled("(you)"); }
            }
            ImGui::EndTable();
        }
        if (s.count == 0) ImGui::TextDisabled("No players.");
    }
    ImGui::End();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);
}

}  // namespace ui::scoreboard
