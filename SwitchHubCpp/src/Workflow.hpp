#pragma once
#include <string>
#include <vector>
#include <optional>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// ============================================================================
// SWITCHHUB WORKFLOW STRUCTURE - COMPLETE VERSION
// ============================================================================
// This file defines the workflow data structure with ALL implemented features:
//
// CORE FEATURES:
// - Basic command/expect workflow execution
// - Interrupt sequences for boot breaking
// - Physical interaction support
//
// ADVANCED RESPONSE HANDLING:
//   expect_any - Match multiple possible patterns (first match wins)
//   on_match - Send different commands based on which pattern matched
//   delay_after_match - Wait X seconds after match before sending command
//
// FALLBACK & ERROR HANDLING:
//   alternate_steps - Try different approaches if main step fails
//   optional - Allow steps to fail without stopping workflow
//
// AUTO-DETECTION:
//   auto_detect_baud - Automatically detect device baud rate
// ============================================================================

struct Step {
    // === REQUIRED FIELDS ===
    std::string name;           // Step identifier for logging
    std::string status_text;    // Text displayed in GUI during step

    bool send_raw = false;

    // === OPTIONAL ACTION (choose one) ===
    std::optional<std::string> command;     // Command to send to device
    std::optional<std::string> interrupt;   // Interrupt sequence ("__BREAK__" for boot break)

    // === RESPONSE HANDLING (choose one) ===
    std::optional<std::string> expect_regex;    // Wait for single pattern (legacy)

    //   FEATURE: expect_any - Match ANY of multiple patterns
    std::vector<std::string> expect_any;        // Match first occurrence of any pattern

    bool parse_and_delete = false;
    std::string delete_path = "flash:/";
    std::vector<std::string> preserve_extensions = { ".bin", ".ipe", ".pkg" };
    std::vector<std::string> preserve_dirs = {"license", "versionInfo", "pki"};
    std::string delete_command = "delete /unreserved {path}";
    std::string dir_delete_command = "rmdir {path}";

    //   FEATURE: on_match - Conditional command execution
    std::map<std::string, std::string> on_match;  // pattern -> command mapping

    //   FEATURE: delay_after_match - Post-match delay
    int delay_after_match = 0;  // Seconds to wait AFTER match, BEFORE sending command

    // === FALLBACK & ERROR HANDLING ===

    //   FEATURE: alternate_steps - Fallback strategies
    std::vector<Step> alternate_steps;  // Backup steps to try if this step fails

    //   FEATURE: optional - Non-critical steps
    bool optional = false;  // If true, step failure doesn't stop workflow

    // === TIMING & INTERACTION ===
    int timeout_sec = 10;                   // How long to wait for response
    bool require_physical_interact = false; // Pause for user physical action
    int hold_interact_timer = 10;           // Duration of physical interaction pause
};

struct Workflow {
    std::string name;                   // Workflow name
    std::string description;            // Workflow description
    std::vector<Step> steps;            // Ordered list of steps

    //   FEATURE: auto_detect_baud - Automatic baud rate detection
    int baud_rate = 9600;
};

// ============================================================================
// JSON DESERIALIZATION
// ============================================================================

// Forward declaration for recursive Step parsing
void from_json(const json& j, Step& s);

inline void from_json(const json& j, Step& s) {
    // === REQUIRED FIELDS ===
    j.at("name").get_to(s.name);

    // Support both "status" and "status_text" (legacy compatibility)
    if (j.contains("status")) j.at("status").get_to(s.status_text);
    else if (j.contains("status_text")) j.at("status_text").get_to(s.status_text);

    // === OPTIONAL COMMAND ===
    if (j.contains("command") && !j.at("command").is_null()) {
        s.command = j.at("command").get<std::string>();
    }

    // === OPTIONAL INTERRUPT ===
    if (j.contains("interrupt") && !j.at("interrupt").is_null()) {
        s.interrupt = j.at("interrupt").get<std::string>();
    }

    // === SINGLE EXPECT PATTERN (Legacy) ===
    if (j.contains("expect") && !j.at("expect").is_null()) {
        s.expect_regex = j.at("expect").get<std::string>();
    }
    else if (j.contains("expect_regex") && !j.at("expect_regex").is_null()) {
        s.expect_regex = j.at("expect_regex").get<std::string>();
    }

    // ===   FEATURE: expect_any - Multiple possible patterns ===
    if (j.contains("expect_any") && j.at("expect_any").is_array()) {
        for (const auto& pattern : j.at("expect_any")) {
            s.expect_any.push_back(pattern.get<std::string>());
        }
    }

    // ===   FEATURE: on_match - Conditional commands ===
    if (j.contains("on_match") && j.at("on_match").is_object()) {
        for (auto& [key, val] : j.at("on_match").items()) {
            s.on_match[key] = val.get<std::string>();
        }
    }

    // ===   FEATURE: delay_after_match - Post-match delay ===
    s.delay_after_match = j.value("delay_after_match", 0);

    // ===  FEATURE: alternate_steps - Fallback strategies ===
    if (j.contains("alternate_steps") && j.at("alternate_steps").is_array()) {
        for (const auto& alt_json : j.at("alternate_steps")) {
            Step alt_step;
            from_json(alt_json, alt_step);  // Recursive parsing
            s.alternate_steps.push_back(alt_step);
        }
    }

    // ===  FEATURE: optional - Non-critical step flag ===
    s.optional = j.value("optional", false);

    // === TIMING & INTERACTION ===
    s.hold_interact_timer = j.value("hold_interact_timer", 0);
    s.timeout_sec = j.value("timeout", j.value("timeout_sec", 10));
    s.require_physical_interact = j.value("require_physical_interact", false);

    s.send_raw = j.value("send_raw", false);

    s.parse_and_delete = j.value("parse_and_delete", false);

    if (j.contains("delete_path")) {
        s.delete_path = j.at("delete_path").get<std::string>();
    }

    if (j.contains("preserve_extensions") && j.at("preserve_extensions").is_array()) {
        s.preserve_extensions.clear();
        for (const auto& ext : j.at("preserve_extensions")) {
            s.preserve_extensions.push_back(ext.get<std::string>());
        }
    }

    if (j.contains("preserve_dirs") && j.at("preserve_dirs").is_array()) {
        s.preserve_dirs.clear();
        for (const auto& dir : j.at("preserve_dirs")) {
            s.preserve_dirs.push_back(dir.get<std::string>());
        }
    }

    if (j.contains("delete_command")) {
        s.delete_command = j.at("delete_command").get<std::string>();
    }

    if (j.contains("dir_delete_command")) {
        s.dir_delete_command = j.at("dir_delete_command").get<std::string>();
    }
}

inline void from_json(const json& j, Workflow& w) {
    j.at("name").get_to(w.name);
    w.description = j.value("description", "");
    j.at("steps").get_to(w.steps);
    
}

inline Workflow LoadWorkflowFromFile(const std::string& filepath) {
    std::ifstream f(filepath);
    if (!f.is_open()) {
        throw std::runtime_error("Could not open workflow file: " + filepath);
    }
    json j = json::parse(f);
    return j.get<Workflow>();
}