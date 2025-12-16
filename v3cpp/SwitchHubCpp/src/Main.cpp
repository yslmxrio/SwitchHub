#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <iostream>
#include <map>
#include <Utils.hpp>
#include "SerialEngine.hpp"

// Global map to hold the state of each port
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
};
std::map<std::string, PortContext> active_ports;

void RenderPortPanel(const std::string& port_name) {
    ImGui::PushID(port_name.c_str());
    ImGui::BeginGroup();

    // 1. Get Status
    EngineStatus state;
    bool has_engine = (active_ports[port_name].engine != nullptr);
    if (has_engine) state = active_ports[port_name].engine->get_state();

    // 2. Colors
    ImVec4 color = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
    if (state.is_failed) color = ImVec4(0.8f, 0.2f, 0.2f, 1.0f);
    else if (state.is_complete) color = ImVec4(0.2f, 0.8f, 0.2f, 1.0f);
    else if (state.is_interactive) color = ImVec4(1.0f, 0.8f, 0.0f, 1.0f);
    else if (has_engine) color = ImVec4(0.0f, 0.5f, 1.0f, 1.0f);

    // 3. Header with IDENT button
    ImGui::TextColored(color, "[%s] Status: %s", port_name.c_str(), state.status_msg.empty() ? "Idle" : state.status_msg.c_str());

    // IDENT button (right aligned) - NOW WORKING!
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 80);
    if (active_ports[port_name].is_identifying) {
        // Show blinking state
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.6f, 0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.7f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.8f, 0.5f, 0.0f, 1.0f));
        ImGui::Button("Blinking...##ident", ImVec2(80, 0));
        ImGui::PopStyleColor(3);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("LED is blinking (auto-stops in 5s)");
        }
    }
    else {
        // Not blinking - button is clickable
        if (ImGui::Button("IDENT##ident", ImVec2(80, 0))) {
            active_ports[port_name].is_identifying = true;

            // Call static method - opens port once, blinks, closes
            SerialEngine::blink_port_led_tx(port_name);

            // Auto-reset flag after 5 seconds
            std::thread([port_name]() {
                std::this_thread::sleep_for(std::chrono::seconds(5) + std::chrono::milliseconds(200));
                active_ports[port_name].is_identifying = false;
                }).detach();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Blink LED to identify this cable (works anytime!)");
        }
    }

    ImGui::InputText("Asset ID", active_ports[port_name].asset_id, 64);
    ImGui::InputInt("Baud Rate", &active_ports[port_name].manual_baud_rate);
    

    // AUTO DETECT BAUD BUTTON
    /*if (!has_engine && !active_ports[port_name].is_detecting) {
        if (ImGui::Button("Auto Detect Baud", ImVec2(150, 0))) {
            active_ports[port_name].is_detecting = true;
            active_ports[port_name].detected_baud = 0;

            active_ports[port_name].detect_thread = std::jthread([port_name]() {
                try {
                    Workflow dummy_wf;
                    dummy_wf.name = "Baud Detection";
                    auto temp_engine = std::make_unique<SerialEngine>(port_name, dummy_wf);

                    int baud = temp_engine->detect_baud_rate();
                    active_ports[port_name].detected_baud = baud;
                    active_ports[port_name].is_detecting = false;
                }
                catch (...) {
                    active_ports[port_name].detected_baud = 0;
                    active_ports[port_name].is_detecting = false;
                }
                });
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Automatically detect the correct baud rate");
        }

        if (active_ports[port_name].detected_baud > 0) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f),
                "Detected: %d baud",
                active_ports[port_name].detected_baud);
        }
    }
    else if (active_ports[port_name].is_detecting) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.5f, 1.0f, 1.0f));
        ImGui::Button("Detecting...", ImVec2(150, 0));
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Testing common baud rates...");
        }
    }*/

    // --- WORKFLOW SELECTION DROPDOWN --
    if (!has_engine) {
        std::vector<std::string> workflows = get_workflow_files();

        if (workflows.empty()) {
            ImGui::TextDisabled("No .json files in /workflows");
        }
        else {
            int& current_idx = active_ports[port_name].selected_workflow_idx;
            if (current_idx >= workflows.size()) current_idx = 0;

            if (ImGui::BeginCombo("Workflow", workflows[current_idx].c_str())) {
                for (int n = 0; n < workflows.size(); n++) {
                    const bool is_selected = (current_idx == n);
                    if (ImGui::Selectable(workflows[n].c_str(), is_selected))
                        current_idx = n;

                    if (is_selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            // Start Button
            if (ImGui::Button("START WORKFLOW")) {
                try {
                    std::string full_path = "workflows/" + workflows[current_idx];
                    Workflow wf = LoadWorkflowFromFile(full_path);

                    // Pass asset_id to SerialEngine for log filename
                    std::string asset_id = active_ports[port_name].asset_id;
                    active_ports[port_name].engine = std::make_unique<SerialEngine>(port_name, wf, asset_id);
                    active_ports[port_name].worker_thread = std::jthread([&, port_name]() {
                        active_ports[port_name].engine->run(active_ports[port_name].manual_baud_rate);
                        });
                }
                catch (const std::exception& e) {
                    std::cerr << "[ERROR] " << e.what() << std::endl;
                }
            }
        }
    }
    else {
        // Running/Finished State UI
        if (state.is_complete || state.is_failed) {
            if (ImGui::Button("RESET / OK")) {
                if (active_ports[port_name].worker_thread.joinable()) {
                    active_ports[port_name].worker_thread.join();
                }
                active_ports[port_name].engine.reset();
            }
        }
  else {
            if (ImGui::Button("STOP WORKFLOW")) {
                // 1. Tell engine to stop
                active_ports[port_name].engine->stop();
                
                // 2. Wait for thread to exit (happens fast now)
                if (active_ports[port_name].worker_thread.joinable()) {
                    active_ports[port_name].worker_thread.join();
                }
                
                // 3. Delete the engine to release the COM port
                active_ports[port_name].engine.reset();
            }
        }

        // Show detected baud rate if available
        if (state.detected_baud_rate > 0) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f),
                "@ %d baud",
                state.detected_baud_rate);
        }

        // Show log file path (clickable to open folder)
        if (!state.log_file_path.empty()) {
            ImGui::SameLine();
            if (ImGui::SmallButton("📄 Log")) {
                // Open logs folder in file explorer
#ifdef _WIN32
                system("explorer logs");
#else
                system("xdg-open logs || open logs");
#endif
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Log saved: %s\nClick to open logs folder", state.log_file_path.c_str());
            }
        }
    }

    bool is_running = has_engine && !state.is_complete && !state.is_failed;
    ImGui::Separator();
    ImGui::BeginDisabled(is_running);

    ImGui::Text("Manual input cmd...");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150);

    bool enter_pressed = ImGui::InputText("##cmd", active_ports[port_name].manual_cmd, 128, ImGuiInputTextFlags_EnterReturnsTrue);

    ImGui::SameLine();
    if (ImGui::Button("SEND") || enter_pressed) {
        // Cleanup previous run
        if (active_ports[port_name].engine) {
            if (active_ports[port_name].worker_thread.joinable()) {
                active_ports[port_name].worker_thread.join();
            }
            active_ports[port_name].engine.reset();
        }

        // Create manual workflow
        Workflow manual_wf;
        manual_wf.name = "Manual Override";

        Step s;
        s.name = "Manual TX";
        s.status_text = "Sending Manual Command...";
        s.command = std::string(active_ports[port_name].manual_cmd);
        s.timeout_sec = 2.5;

        manual_wf.steps.push_back(s);

        // Pass asset_id for log filename
        std::string asset_id = active_ports[port_name].asset_id;
        active_ports[port_name].engine = std::make_unique<SerialEngine>(port_name, manual_wf, asset_id);
        active_ports[port_name].worker_thread = std::jthread([&, port_name]() {
            active_ports[port_name].engine->run(active_ports[port_name].manual_baud_rate);
            });
    }

    ImGui::EndDisabled();

    // Log Region
    ImGui::BeginChild("LogRegion", ImVec2(0, 500), true, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::TextUnformatted(state.text_log.c_str());
    if (state.text_log.length() > 0 && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();

    ImGui::EndGroup();
    ImGui::PopID();
}

int main() {
    if (!glfwInit()) return 1;
    GLFWwindow* window = glfwCreateWindow(1280, 720, "SwitchHub C++", NULL, NULL);
    if (!window) return 1;
    glfwMakeContextCurrent(window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    // Initialize ports
    std::vector<std::string> foundPorts = get_available_ports();

    if (foundPorts.empty()) {
        std::cout << "[INFO] No serial devices found." << std::endl;
    }
    else {
        for (const std::string& foundPort : foundPorts) {
            std::cout << "[INFO] Found device on: " << foundPort << std::endl;
            try {
                active_ports[foundPort];
            }
            catch (...) {
                std::cout << "[WARN] Could not open " << foundPort << std::endl;
            }
        }
    }

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);

        ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBringToFrontOnFocus;
        ImGui::Begin("SwitchHub Dashboard", nullptr, window_flags);

        if (ImGui::Button("Refresh Ports")) {
            std::vector<std::string> new_ports = get_available_ports();
            foundPorts = new_ports;
        }
        ImGui::SameLine();
        ImGui::Text("Active Devices: %d", (int)foundPorts.size());
        ImGui::Separator();

        if (ImGui::BeginTable("Grid", 2)) {
            for (const std::string& port : foundPorts)
            {
                ImGui::TableNextColumn();
                RenderPortPanel(port);
            }
            ImGui::EndTable();
        }
        ImGui::End();

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    // Cleanup - ensure all threads are stopped
    for (auto& [port_name, ctx] : active_ports) {
        if (ctx.detect_thread.joinable()) {
            ctx.detect_thread.join();
        }
    }

    active_ports.clear();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}