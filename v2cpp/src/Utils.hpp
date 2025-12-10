#pragma once
#include <vector>
#include <string>
#include <iostream>
#include <filesystem>
// 2. Define this macro to stop windows.h from including the old Winsock 1.0
#define WIN32_LEAN_AND_MEAN

// 3. Now it is safe to include windows.h
#include <windows.h>

// Returns a list of all connected serial ports (e.g. "COM3", "COM4")
std::vector<std::string> get_available_ports() {
    std::vector<std::string> ports;
    HKEY hKey;
    
    // Open the Registry key where Windows stores active serial ports
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DEVICEMAP\\SERIALCOMM", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char valueName[256];
        DWORD valueNameSize = 256;
        char data[256];
        DWORD dataSize = 256;
        DWORD type;
        DWORD index = 0;

        // Iterate through all values in this key
        while (RegEnumValueA(hKey, index, valueName, &valueNameSize, NULL, &type, (LPBYTE)data, &dataSize) == ERROR_SUCCESS) {
            if (type == REG_SZ) {
                ports.push_back(std::string(data)); // data contains "COM3", "COM4", etc.
            }
            // Reset sizes for next iteration
            valueNameSize = 256;
            dataSize = 256;
            index++;
        }
        RegCloseKey(hKey);
    }
    return ports;
}

inline std::vector<std::string> get_workflow_files() {
    std::vector<std::string> files;
    std::string path = "workflows";

    // Create directory if it doesn't exist
    if (!std::filesystem::exists(path)) {
        std::filesystem::create_directory(path);
    }

    for (const auto& entry : std::filesystem::directory_iterator(path)) {
        if (entry.path().extension() == ".json") {
            // Returns just the filename (e.g., "Reset.json")
            files.push_back(entry.path().filename().string());
        }
    }
    return files;
}

