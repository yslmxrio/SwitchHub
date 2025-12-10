#pragma once
#include <string>
#include <vector>
#include <optional>
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

struct Step {
    std::string name;
    std::string status_text;

    // Optional fields (use std::nullopt if missing)
    std::optional<std::string> command;
    std::optional<std::string> interrupt;     // e.g., "__BREAK__"
    std::optional<std::string> expect_regex;

    int timeout_sec = 10;
    bool require_physical_interact = false;
    int hold_interact_timer = 10;
};

struct Workflow {
    std::string name;
    std::string description;
    std::vector<Step> steps;
};

// --- JSON MAPPING MAGIC ---

// 1. Tell library how to read a Step
inline void from_json(const json& j, Step& s) {
    j.at("name").get_to(s.name);

    if (j.contains("status")) j.at("status").get_to(s.status_text);
    else if (j.contains("status_text")) j.at("status_text").get_to(s.status_text);

    // Safe Optional Loading: Check if key exists AND is NOT null
    if (j.contains("command") && !j.at("command").is_null()) {
        s.command = j.at("command").get<std::string>();
    }
    else {
        s.command = std::nullopt;
    }

    if (j.contains("interrupt") && !j.at("interrupt").is_null()) {
        s.interrupt = j.at("interrupt").get<std::string>();
    }
    else {
        s.interrupt = std::nullopt;
    }

    // Handle 'expect' vs 'expect_regex' mismatch
    if (j.contains("expect") && !j.at("expect").is_null()) {
        s.expect_regex = j.at("expect").get<std::string>();
    }
    else if (j.contains("expect_regex") && !j.at("expect_regex").is_null()) {
        s.expect_regex = j.at("expect_regex").get<std::string>();
    }
    else {
        s.expect_regex = std::nullopt;
    }

    s.hold_interact_timer = j.value("hold_interact_timer", 0);

    // Defaults
    s.timeout_sec = j.value("timeout", j.value("timeout_sec", 10)); // Checks "timeout" then "timeout_sec"
    s.require_physical_interact = j.value("require_physical_interact", false);
   
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

