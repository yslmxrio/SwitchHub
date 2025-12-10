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
    char asset_id[64] = "";
    int selected_workflow_idx = 0;
    char manual_cmd[128] = "";
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

    // 3. Header
    ImGui::TextColored(color, "[%s] Status: %s", port_name.c_str(), state.status_msg.empty() ? "Idle" : state.status_msg.c_str());
    ImGui::InputText("Asset ID", active_ports[port_name].asset_id, 64);

    

    // --- WORKFLOW SELECTION DROPDOWN --
    if (!has_engine) {
        // Get list of files (e.g., "Reset.json", "Upgrade.json")
        // Note: In a real app, you might want to cache this vector globally so we don't scan disk every frame.
        std::vector<std::string> workflows = get_workflow_files();

        if (workflows.empty()) {
            ImGui::TextDisabled("No .json files in /workflows");
        }
        else {
            // Safety check for index
            int& current_idx = active_ports[port_name].selected_workflow_idx;
            if (current_idx >= workflows.size()) current_idx = 0;

            // The Combo Box
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
                    // Construct full path: "workflows/Reset.json"
                    std::string full_path = "workflows/" + workflows[current_idx];

                    Workflow wf = LoadWorkflowFromFile(full_path);

                    active_ports[port_name].engine = std::make_unique<SerialEngine>(port_name, wf);
                    active_ports[port_name].worker_thread = std::jthread([&, port_name]() {
                        active_ports[port_name].engine->run();
                        });
                }
                catch (const std::exception& e) {
                    std::cerr << "[ERROR] " << e.what() << std::endl;
                }
            }
        }
    }
    // -----------------------------------------

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
                active_ports[port_name].engine->stop();
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
        // 1. Cleanup previous run if it exists (so we can open the port)
        if (active_ports[port_name].engine) {
            if (active_ports[port_name].worker_thread.joinable()) {
                active_ports[port_name].worker_thread.join();
            }
            active_ports[port_name].engine.reset();
        }

        // 2. Create a "Mini-Workflow" on the fly
        Workflow manual_wf;
        manual_wf.name = "Manual Override";

        Step s;
        s.name = "Manual TX";
        s.status_text = "Sending Manual Command...";
        s.command = std::string(active_ports[port_name].manual_cmd);
        s.timeout_sec = 2.5; // Listen for 2 seconds after sending to catch output
        // Note: No expect_regex means it will use the read_any() we just added

        manual_wf.steps.push_back(s);

        // 3. Clear input field (optional)
        // memset(active_ports[port_name].manual_cmd, 0, 128);

        // 4. Fire it off!
        active_ports[port_name].engine = std::make_unique<SerialEngine>(port_name, manual_wf);
        active_ports[port_name].worker_thread = std::jthread([&, port_name]() {
            active_ports[port_name].engine->run();
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
    //glfwSwapInterval(1); // Enable VSync

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    // Initialize the ports you want to see
    std::vector<std::string> foundPorts = get_available_ports();

    if (foundPorts.empty()) {
        std::cout << "[INFO] No serial devices found." << std::endl;
    }
    else {
        for (const std::string& foundPort : foundPorts) {
            std::cout << "[INFO] Found device on: " << foundPort << std::endl;

            // Try to connect to this port
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
            // 1. Get new list
            std::vector<std::string> new_ports = get_available_ports();

            // 2. Add new ones to foundPorts (preserving old ones prevents UI flicker)
            foundPorts = new_ports;

            // Optional: Clean up active_ports map if a device was unplugged
            // (Advanced: leave this for now to avoid deleting active workflows)
        }
        ImGui::SameLine();
        ImGui::Text("Active Devices: %d", foundPorts.size());
        ImGui::Separator();

        if (ImGui::BeginTable("Grid", 2)) {
            for (const std::string& port : foundPorts)
            {
                ImGui::TableNextColumn(); RenderPortPanel(port);
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
    
    // Cleanup
    active_ports.clear();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}