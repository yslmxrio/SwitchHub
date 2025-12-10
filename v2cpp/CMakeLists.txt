cmake_minimum_required(VERSION 3.20)
project(SwitchHubCpp)

set(CMAKE_CXX_STANDARD 20)

# --- 1. Dependencies (Found via vcpkg) ---
find_package(Boost REQUIRED COMPONENTS system thread regex)
find_package(nlohmann_json REQUIRED)
find_package(glfw3 REQUIRED)
find_package(opengl REQUIRED)

# --- 2. Fetch ImGui Manually ---
include(FetchContent)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui
    GIT_TAG        docking
)
FetchContent_MakeAvailable(imgui)

# Define ImGui Sources
set(IMGUI_DIR ${imgui_SOURCE_DIR})
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

# --- 3. The Executable ---
add_executable(SwitchHub 
    src/Main.cpp
    src/SerialEngine.hpp
    src/Workflow.hpp
    ${IMGUI_SOURCES}
)

# --- 4. Linking ---
target_include_directories(SwitchHub PRIVATE src ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_link_libraries(SwitchHub PRIVATE
    Boost::system
    Boost::thread
    Boost::regex
    nlohmann_json::nlohmann_json
    glfw
    opengl32
)
if(MSVC)
    target_link_options(SwitchHub PRIVATE /ENTRY:mainCRTStartup)
endif()