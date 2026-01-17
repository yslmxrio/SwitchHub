#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <iostream>
#include <map>
#include <Utils.hpp>
#include "SerialEngine.hpp"
#include <sstream>

// -----------------------------------------------------------------------------
// Data Structures
// -----------------------------------------------------------------------------
struct PortContext {
    std::unique_ptr<SerialEngine> engine;
    std::jthread worker_thread;
    std::jthread detect_thread;
    char asset_id[64] = "";
    int selected_workflow_idx = 0;
    char manual_cmd[128] = "";
    bool is_identifying = false;
    bool is_detecting = false;
    int detected_baud = 0;
    int manual_baud_rate = 9600;
    std::string persistent_log;
};

std::map<std::string, PortContext> active_ports;
static int g_selected_port_idx = 0;
static std::vector<std::string> g_port_list;

// -----------------------------------------------------------------------------
// Theme Configuration
// -----------------------------------------------------------------------------
namespace Theme {
    // Base colors
    constexpr ImU32 BgDark = IM_COL32(18, 18, 22, 255);
    constexpr ImU32 BgPanel = IM_COL32(26, 26, 32, 255);
    constexpr ImU32 BgSection = IM_COL32(32, 32, 40, 255);
    constexpr ImU32 BgInput = IM_COL32(40, 40, 50, 255);
    constexpr ImU32 Border = IM_COL32(55, 55, 70, 255);
    constexpr ImU32 BorderLight = IM_COL32(70, 70, 90, 255);

    // Text colors
    constexpr ImU32 TextPrimary = IM_COL32(230, 230, 235, 255);
    constexpr ImU32 TextSecondary = IM_COL32(160, 160, 175, 255);
    constexpr ImU32 TextMuted = IM_COL32(100, 100, 115, 255);

    // Status colors
    constexpr ImU32 StatusIdle = IM_COL32(120, 120, 140, 255);
    constexpr ImU32 StatusRunning = IM_COL32(80, 160, 255, 255);
    constexpr ImU32 StatusSuccess = IM_COL32(80, 200, 120, 255);
    constexpr ImU32 StatusFailed = IM_COL32(230, 80, 80, 255);
    constexpr ImU32 StatusWarning = IM_COL32(230, 180, 60, 255);

    // Button colors
    constexpr ImU32 BtnPrimary = IM_COL32(60, 130, 200, 255);
    constexpr ImU32 BtnPrimaryHover = IM_COL32(75, 150, 220, 255);
    constexpr ImU32 BtnSuccess = IM_COL32(50, 160, 90, 255);
    constexpr ImU32 BtnSuccessHover = IM_COL32(60, 180, 105, 255);
    constexpr ImU32 BtnDanger = IM_COL32(180, 60, 60, 255);
    constexpr ImU32 BtnDangerHover = IM_COL32(200, 75, 75, 255);

    // Convert ImU32 to ImVec4
    inline ImVec4 ToVec4(ImU32 col) {
        return ImGui::ColorConvertU32ToFloat4(col);
    }
}

// -----------------------------------------------------------------------------
// UI Helper Functions
// -----------------------------------------------------------------------------
void SetupStyle() {
    ImGuiStyle& style = ImGui::GetStyle();

    // Sizing
    style.WindowPadding = ImVec2(16, 16);
    style.FramePadding = ImVec2(12, 8);
    style.ItemSpacing = ImVec2(10, 8);
    style.ItemInnerSpacing = ImVec2(8, 6);
    style.IndentSpacing = 20.0f;
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 12.0f;

    // Rounding
    style.WindowRounding = 0.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding = 3.0f;
    style.TabRounding = 4.0f;

    // Borders
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;
    style.TabBorderSize = 0.0f;

    // Colors
    ImVec4* c = style.Colors;
    c[ImGuiCol_WindowBg] = Theme::ToVec4(Theme::BgDark);
    c[ImGuiCol_ChildBg] = Theme::ToVec4(Theme::BgPanel);
    c[ImGuiCol_PopupBg] = Theme::ToVec4(Theme::BgSection);
    c[ImGuiCol_Border] = Theme::ToVec4(Theme::Border);
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);

    c[ImGuiCol_FrameBg] = Theme::ToVec4(Theme::BgInput);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.22f, 0.28f, 1.0f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.26f, 0.26f, 0.32f, 1.0f);

    c[ImGuiCol_TitleBg] = Theme::ToVec4(Theme::BgPanel);
    c[ImGuiCol_TitleBgActive] = Theme::ToVec4(Theme::BgSection);
    c[ImGuiCol_TitleBgCollapsed] = Theme::ToVec4(Theme::BgPanel);

    c[ImGuiCol_ScrollbarBg] = Theme::ToVec4(Theme::BgPanel);
    c[ImGuiCol_ScrollbarGrab] = Theme::ToVec4(Theme::Border);
    c[ImGuiCol_ScrollbarGrabHovered] = Theme::ToVec4(Theme::BorderLight);
    c[ImGuiCol_ScrollbarGrabActive] = Theme::ToVec4(Theme::StatusRunning);

    c[ImGuiCol_CheckMark] = Theme::ToVec4(Theme::StatusRunning);
    c[ImGuiCol_SliderGrab] = Theme::ToVec4(Theme::BtnPrimary);
    c[ImGuiCol_SliderGrabActive] = Theme::ToVec4(Theme::BtnPrimaryHover);

    c[ImGuiCol_Button] = Theme::ToVec4(Theme::BtnPrimary);
    c[ImGuiCol_ButtonHovered] = Theme::ToVec4(Theme::BtnPrimaryHover);
    c[ImGuiCol_ButtonActive] = ImVec4(0.18f, 0.40f, 0.65f, 1.0f);

    c[ImGuiCol_Header] = ImVec4(0.22f, 0.22f, 0.28f, 1.0f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.28f, 0.28f, 0.35f, 1.0f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.32f, 0.32f, 0.40f, 1.0f);

    c[ImGuiCol_Tab] = Theme::ToVec4(Theme::BgSection);
    c[ImGuiCol_TabHovered] = ImVec4(0.28f, 0.28f, 0.36f, 1.0f);
    c[ImGuiCol_TabActive] = Theme::ToVec4(Theme::BtnPrimary);
    c[ImGuiCol_TabUnfocused] = Theme::ToVec4(Theme::BgSection);
    c[ImGuiCol_TabUnfocusedActive] = ImVec4(0.22f, 0.22f, 0.28f, 1.0f);

    c[ImGuiCol_Separator] = Theme::ToVec4(Theme::Border);
    c[ImGuiCol_SeparatorHovered] = Theme::ToVec4(Theme::BorderLight);
    c[ImGuiCol_SeparatorActive] = Theme::ToVec4(Theme::StatusRunning);

    c[ImGuiCol_Text] = Theme::ToVec4(Theme::TextPrimary);
    c[ImGuiCol_TextDisabled] = Theme::ToVec4(Theme::TextMuted);
}

// Draw a labeled section header
void SectionHeader(const char* label) {
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(Theme::TextSecondary));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::Spacing();
}

// Status badge with colored indicator
void StatusBadge(const char* text, ImU32 color) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();

    float radius = 5.0f;
    draw->AddCircleFilled(ImVec2(pos.x + radius, pos.y + ImGui::GetTextLineHeight() * 0.5f),
        radius, color);

    ImGui::Dummy(ImVec2(radius * 2 + 8, 0));
    ImGui::SameLine();
    ImGui::TextUnformatted(text);
}

// Styled button variants
bool ButtonPrimary(const char* label, const ImVec2& size = ImVec2(0, 0)) {
    ImGui::PushStyleColor(ImGuiCol_Button, Theme::ToVec4(Theme::BtnPrimary));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::ToVec4(Theme::BtnPrimaryHover));
    bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleColor(2);
    return clicked;
}

bool ButtonSuccess(const char* label, const ImVec2& size = ImVec2(0, 0)) {
    ImGui::PushStyleColor(ImGuiCol_Button, Theme::ToVec4(Theme::BtnSuccess));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::ToVec4(Theme::BtnSuccessHover));
    bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleColor(2);
    return clicked;
}

bool ButtonDanger(const char* label, const ImVec2& size = ImVec2(0, 0)) {
    ImGui::PushStyleColor(ImGuiCol_Button, Theme::ToVec4(Theme::BtnDanger));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::ToVec4(Theme::BtnDangerHover));
    bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleColor(2);
    return clicked;
}

bool ButtonSecondary(const char* label, const ImVec2& size = ImVec2(0, 0)) {
    ImGui::PushStyleColor(ImGuiCol_Button, Theme::ToVec4(Theme::BgSection));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.25f, 0.32f, 1.0f));
    bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleColor(2);
    return clicked;
}

// -----------------------------------------------------------------------------
// Port Sidebar - List of available ports
// -----------------------------------------------------------------------------
void RenderPortSidebar(float width) {
    ImGui::BeginChild("Sidebar", ImVec2(width, 0), true);

    SectionHeader("SERIAL PORTS");

    if (ButtonPrimary("Refresh Ports", ImVec2(-1, 32))) {
        g_port_list = get_available_ports();
        // Initialize contexts for new ports
        for (const auto& port : g_port_list) {
            if (active_ports.find(port) == active_ports.end()) {
                active_ports[port] = PortContext();
            }
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (g_port_list.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(Theme::TextMuted));
        ImGui::TextWrapped("No serial ports detected. Click Refresh to scan.");
        ImGui::PopStyleColor();
    }

    for (int i = 0; i < (int)g_port_list.size(); i++) {
        const std::string& port = g_port_list[i];
        PortContext& ctx = active_ports[port];

        // Determine status for this port
        ImU32 status_color = Theme::StatusIdle;
        const char* status_text = "Idle";

        if (ctx.engine) {
            EngineStatus state = ctx.engine->get_state();
            if (state.is_failed) {
                status_color = Theme::StatusFailed;
                status_text = "Failed";
            }
            else if (state.is_complete) {
                status_color = Theme::StatusSuccess;
                status_text = "Complete";
            }
            else if (state.is_interactive) {
                status_color = Theme::StatusWarning;
                status_text = "Waiting";
            }
            else {
                status_color = Theme::StatusRunning;
                status_text = "Running";
            }
        }

        ImGui::PushID(i);

        // Port selection button
        bool is_selected = (g_selected_port_idx == i);
        if (is_selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, Theme::ToVec4(Theme::BtnPrimary));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::ToVec4(Theme::BtnPrimaryHover));
        }
        else {
            ImGui::PushStyleColor(ImGuiCol_Button, Theme::ToVec4(Theme::BgSection));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.22f, 0.28f, 1.0f));
        }

        if (ImGui::Button(port.c_str(), ImVec2(-1, 40))) {
            g_selected_port_idx = i;
        }

        ImGui::PopStyleColor(2);

        // Status indicator below port name
        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImVec2 rect_min = ImGui::GetItemRectMin();
        ImVec2 rect_max = ImGui::GetItemRectMax();

        // Draw status dot on the right side
        float dot_x = rect_max.x - 16;
        float dot_y = (rect_min.y + rect_max.y) * 0.5f;
        draw->AddCircleFilled(ImVec2(dot_x, dot_y), 5.0f, status_color);

        // Tooltip with status
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Status: %s", status_text);
        }

        ImGui::PopID();
        ImGui::Spacing();
    }

    ImGui::EndChild();
}

// -----------------------------------------------------------------------------
// Main Port Panel - Configuration and Controls
// -----------------------------------------------------------------------------
void RenderPortPanel(const std::string& port_name) {
    PortContext& ctx = active_ports[port_name];

    // Get current state
    EngineStatus state;
    bool has_engine = (ctx.engine != nullptr);
    if (has_engine) {
        state = ctx.engine->get_state();
    }

    bool is_running = has_engine && !state.is_complete && !state.is_failed;

    // Determine status
    ImU32 status_color = Theme::StatusIdle;
    const char* status_text = "Idle";

    if (state.is_failed) {
        status_color = Theme::StatusFailed;
        status_text = "Failed";
    }
    else if (state.is_complete) {
        status_color = Theme::StatusSuccess;
        status_text = "Complete";
    }
    else if (state.is_interactive) {
        status_color = Theme::StatusWarning;
        std::string status_text = ctx.engine->get_state().status_msg;
    }
    else if (has_engine) {
        status_color = Theme::StatusRunning;
        status_text = state.status_msg.empty() ? "Running" : state.status_msg.c_str();
    }

    float label_width = 100.0f;
    float input_width = 220.0f;

    // === HEADER ROW ===
    ImGui::Text("%s", port_name.c_str());
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 110);

    if (ctx.is_identifying) {
        ImGui::PushStyleColor(ImGuiCol_Button, Theme::ToVec4(Theme::StatusWarning));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::ToVec4(Theme::StatusWarning));
        ImGui::Button("Blinking...", ImVec2(110, 0));
        ImGui::PopStyleColor(2);
    }
    else {
        if (ButtonSecondary("Identify Port", ImVec2(110, 0))) {
            ctx.is_identifying = true;
            SerialEngine::blink_port_led_tx(port_name);
            std::thread([&ctx]() {
                std::this_thread::sleep_for(std::chrono::seconds(5) + std::chrono::milliseconds(200));
                ctx.is_identifying = false;
                }).detach();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Blink TX LED to identify physical cable");
        }
    }

    // Status line
    StatusBadge(status_text, status_color);
    if (state.detected_baud_rate > 0) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(Theme::TextMuted));
        ImGui::Text("@ %d baud", state.detected_baud_rate);
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // === CONFIGURATION ===
    SectionHeader("CONFIGURATION");

    // Asset ID
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Asset ID");
    ImGui::SameLine(label_width);
    ImGui::SetNextItemWidth(input_width);
    ImGui::InputText("##asset", ctx.asset_id, sizeof(ctx.asset_id));

    // Baud Rate
    static const std::vector<int> baud_rates = {
        300, 600, 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400
    };

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Baud Rate");
    ImGui::SameLine(label_width);
    ImGui::SetNextItemWidth(input_width);
    if (ImGui::BeginCombo("##baud", std::to_string(ctx.manual_baud_rate).c_str())) {
        for (int baud : baud_rates) {
            bool selected = (ctx.manual_baud_rate == baud);
            if (ImGui::Selectable(std::to_string(baud).c_str(), selected)) {
                ctx.manual_baud_rate = baud;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    // Workflow
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Workflow");
    ImGui::SameLine(label_width);
    ImGui::SetNextItemWidth(input_width);

    if (!has_engine) {
        std::vector<std::string> workflows = get_workflow_files();
        if (workflows.empty()) {
            ImGui::TextDisabled("No workflows found");
        }
        else {
            if (ctx.selected_workflow_idx >= (int)workflows.size()) {
                ctx.selected_workflow_idx = 0;
            }
            if (ImGui::BeginCombo("##workflow", workflows[ctx.selected_workflow_idx].c_str())) {
                for (int n = 0; n < (int)workflows.size(); n++) {
                    bool selected = (ctx.selected_workflow_idx == n);
                    if (ImGui::Selectable(workflows[n].c_str(), selected)) {
                        ctx.selected_workflow_idx = n;
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }
    }
    else {
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(Theme::StatusRunning));
        ImGui::Text("Active");
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // === CONTROLS ===
    SectionHeader("CONTROLS");

    if (!has_engine) {
        std::vector<std::string> workflows = get_workflow_files();
        ImGui::BeginDisabled(workflows.empty());
        if (ButtonSuccess("Start Workflow", ImVec2(140, 30))) {
            if (!workflows.empty()) {
                try {
                    std::string path = "workflows/" + workflows[ctx.selected_workflow_idx];
                    Workflow wf = LoadWorkflowFromFile(path);
                    std::string asset = ctx.asset_id;
                    ctx.engine = std::make_unique<SerialEngine>(port_name, wf, asset);
                    ctx.worker_thread = std::jthread([&ctx]() {
                        ctx.engine->run(ctx.manual_baud_rate);
                        });
                }
                catch (const std::exception& e) {
                    std::cerr << "[ERROR] " << e.what() << std::endl;
                }
            }
        }
        ImGui::EndDisabled();
    }
    else {
        if (state.is_complete || state.is_failed) {
            if (ButtonPrimary("Reset", ImVec2(100, 30))) {
                if (ctx.worker_thread.joinable()) {
                    ctx.worker_thread.join();
                }
                ctx.engine.reset();
            }
            if (!state.log_file_path.empty()) {
                ImGui::SameLine();
                if (ButtonSecondary("Open Log Folder", ImVec2(130, 30))) {
#ifdef _WIN32
                    system("explorer logs");
#else
                    system("xdg-open logs || open logs");
#endif
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", state.log_file_path.c_str());
                }
            }
        }
        else {
            if (ButtonDanger("Stop Workflow", ImVec2(130, 30))) {
                ctx.engine->stop();
                if (ctx.worker_thread.joinable()) {
                    ctx.worker_thread.join();
                }
                ctx.engine.reset();
            }
        }
    }

    // Manual command row
    ImGui::Spacing();
    ImGui::BeginDisabled(is_running);

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Manual:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 80);
    bool enter_pressed = ImGui::InputText("##cmd", ctx.manual_cmd, sizeof(ctx.manual_cmd),
        ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();

    if (ButtonPrimary("Send", ImVec2(70, 0)) || enter_pressed) {
        if (ctx.engine) {
            EngineStatus old_state = ctx.engine->get_state();
            if (!old_state.text_log.empty()) {
                ctx.persistent_log += old_state.text_log;
                ctx.persistent_log += "\n--- Manual Command ---\n";
            }
            if (ctx.worker_thread.joinable()) {
                ctx.worker_thread.join();
            }
            ctx.engine.reset();
        }

        Workflow manual_wf;
        manual_wf.name = "Manual Command";
        Step s;
        s.name = "Manual TX";
        s.status_text = "Sending Manual Command...";
        s.command = std::string(ctx.manual_cmd);
        s.timeout_sec = 2.5;
        manual_wf.steps.push_back(s);

        std::string asset = ctx.asset_id;
        ctx.engine = std::make_unique<SerialEngine>(port_name, manual_wf, asset);
        ctx.worker_thread = std::jthread([&ctx]() {
            ctx.engine->run(ctx.manual_baud_rate);
            });
    }

    ImGui::EndDisabled();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // === CONSOLE OUTPUT (takes remaining space) ===
    ImGui::Text("Console Output");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60);
    if (ButtonSecondary("Clear", ImVec2(60, 0))) {
        ctx.persistent_log.clear();
    }

    ImGui::Spacing();

    // Console scrollable area - fill remaining height
    float console_height = ImGui::GetContentRegionAvail().y;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.04f, 0.04f, 0.05f, 1.0f));
    ImGui::BeginChild("LogContent", ImVec2(0, console_height), true,
        ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::PopStyleColor();

    std::string full_log = ctx.persistent_log;
    if (has_engine) {
        full_log += state.text_log;
    }

    if (full_log.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(Theme::TextMuted));
        ImGui::TextUnformatted("No output yet. Start a workflow or send a command.");
        ImGui::PopStyleColor();
    }
    else {
        std::istringstream stream(full_log);
        std::string line;

        while (std::getline(stream, line)) {
            ImVec4 color = ImVec4(0.75f, 0.80f, 0.75f, 1.0f);  // Default: device output

            if (line.find("[TX]") == 0 || line.find("[TX-RAW]") == 0) {
                color = ImVec4(0.4f, 0.9f, 0.5f, 1.0f);  // Green
            }
            else if (line.find("[SYSTEM]") == 0 || line.find("[AUTO-BAUD]") == 0 ||
                line.find("[MATCH]") == 0 || line.find("[SUCCESS]") == 0 ||
                line.find("[SCAN]") == 0 || line.find("[PARSER]") == 0) {
                color = ImVec4(0.5f, 0.7f, 1.0f, 1.0f);  // Blue
            }
            else if (line.find("[ERROR]") == 0 || line.find("[FAILED]") == 0 ||
                line.find("[WARN]") == 0 || line.find("[TIMEOUT]") == 0) {
                color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);  // Red
            }
            else if (line.find("[FALLBACK]") == 0 || line.find("[OPTIONAL]") == 0 ||
                line.find("[DELAY]") == 0 || line.find("[CONDITIONAL]") == 0) {
                color = ImVec4(1.0f, 0.85f, 0.4f, 1.0f);  // Yellow
            }
            else if (line.find("[DELETE") == 0 || line.find("[PRESERVE]") == 0 ||
                line.find("[RECURSE]") == 0 || line.find("[CONFIRM]") == 0) {
                color = ImVec4(0.4f, 0.9f, 0.9f, 1.0f);  // Cyan
            }

            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::TextUnformatted(line.c_str());
            ImGui::PopStyleColor();
        }

        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20) {
            ImGui::SetScrollHereY(1.0f);
        }
    }

    ImGui::EndChild();
}

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------
int main() {
    if (!glfwInit()) return 1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);

    GLFWwindow* window = glfwCreateWindow(1400, 900, "SwitchHub Console Manager", NULL, NULL);
    if (!window) {
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);

    float xscale, yscale;
    glfwGetWindowContentScale(window, &xscale, &yscale);
    float dpi_scale = xscale;

    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    float font_size = 16.0f * dpi_scale;

    #ifdef _WIN32
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", font_size);
    #elif __APPLE__
        io.Fonts->AddFontFromFileTTF("/System/Library/Fonts/SFNS.ttf", font_size);
    #else
        io.Fonts->AddFontFromFileTTF("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", font_size);
    #endif

    if (io.Fonts->Fonts.empty()) {
        io.Fonts->AddFontDefault();
    }

    io.FontGlobalScale = 1.2f / dpi_scale;

    SetupStyle();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    // Initial port scan
    g_port_list = get_available_ports();
    for (const auto& port : g_port_list) {
        active_ports[port] = PortContext();
        std::cout << "[INFO] Found: " << port << std::endl;
    }

    // Main loop
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Fullscreen window
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoBringToFrontOnFocus;

        ImGui::Begin("Main", nullptr, flags);

        // Sidebar width
        float sidebar_width = 200.0f;

        // Render sidebar
        RenderPortSidebar(sidebar_width);

        ImGui::SameLine();

        // Main panel - no scrollbar, content handles its own scrolling
        ImGui::BeginChild("MainPanel", ImVec2(0, 0), true, ImGuiWindowFlags_NoScrollbar);

        if (g_port_list.empty()) {
            ImGui::Spacing();
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(Theme::TextMuted));
            ImGui::TextWrapped("No serial ports available. Connect a device and click 'Refresh Ports'.");
            ImGui::PopStyleColor();
        }
        else if (g_selected_port_idx >= 0 && g_selected_port_idx < (int)g_port_list.size()) {
            RenderPortPanel(g_port_list[g_selected_port_idx]);
        }

        ImGui::EndChild();

        ImGui::End();

        // Render
        ImGui::Render();
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.07f, 0.07f, 0.09f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    // Cleanup
    for (auto& [name, ctx] : active_ports) {
        if (ctx.worker_thread.joinable()) ctx.worker_thread.join();
        if (ctx.detect_thread.joinable()) ctx.detect_thread.join();
    }
    active_ports.clear();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}