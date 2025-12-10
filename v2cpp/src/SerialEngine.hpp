#pragma once
#include <boost/asio.hpp>
#include <boost/regex.hpp>
#include <thread>
#include <mutex>
#include <deque>
#include <iostream>
#include "Workflow.hpp"

// 2. Define this macro to stop windows.h from including the old Winsock 1.0
#define WIN32_LEAN_AND_MEAN

// 3. Now it is safe to include windows.h
#include <windows.h>


// Status structure to pass data to the GUI
struct EngineStatus {
    std::string text_log;
    std::string status_msg;
    bool is_interactive = false;
    bool is_complete = false;

    bool is_failed = false;
};

class SerialEngine {
public:
    SerialEngine(const std::string& port, const Workflow& workflow)
        : port_name_(port), workflow_(workflow), stop_requested_(false) {}

    // Main Worker Loop
    void run() {
        try {
            boost::asio::io_context io;
            boost::asio::serial_port ser(io, port_name_);
            
            
            // Standard Serial Settings (9600-8-N-1)
            ser.set_option(boost::asio::serial_port_base::baud_rate(9600));
            ser.set_option(boost::asio::serial_port_base::character_size(8));
            ser.set_option(boost::asio::serial_port_base::stop_bits(boost::asio::serial_port_base::stop_bits::one));
            ser.set_option(boost::asio::serial_port_base::parity(boost::asio::serial_port_base::parity::none));

            log("[SYSTEM] Workflow '" + workflow_.name + "' started on " + port_name_);

            for (const auto& step : workflow_.steps) {
                if (stop_requested_) break;

                update_status(step.status_text, step.require_physical_interact);

                // A. Handle Interrupts
                if (step.interrupt) {
                    perform_interrupt_sequence(ser, step);
                    continue;
                }

                // B. Send Command (Only if it exists)
                if (step.command) {
                    write_line(ser, *step.command);
                }

                // C. Read / Wait (Even if we didn't send anything)
                if (step.expect_regex) {
                    read_until(ser, *step.expect_regex, step.timeout_sec);
                }
                else if (step.timeout_sec > 0) {
                    // Just wait/listen if no specific regex is needed
                    // (Ensure you added the read_any function I mentioned earlier, 
                    //  or just sleep if you haven't)
                    read_any(ser, step.timeout_sec);
                }
            }
            if (!stop_requested_) update_status("Successfully Finished", false, true);

        } catch (const std::exception& e) {
            log("[ERROR] Critical Failure: " + std::string(e.what()));
            update_status("Fatally Failed", false, false, true);
        }
    }

    void stop() { stop_requested_ = true; }

    EngineStatus get_state() {
        std::lock_guard<std::mutex> lock(state_mutex_);
        return current_state_;
    }

private:
    bool handle_pagination(boost::asio::serial_port& ser, std::string& buffer) {
        const std::vector<std::string> page_prompts = {
            "-- MORE --",
            " --More-- ",
            "<--- More --->",
            "Press any key to continue"
        };

        for (const auto& prompt : page_prompts) {
            if (buffer.find(prompt) != std::string::npos) {
                log_raw("[Handling Pagination] ");

                boost::asio::write(ser, boost::asio::buffer(" "));

                size_t pos = buffer.find(prompt);
                if (pos != std::string::npos) {
                    buffer.erase(pos, prompt.length());
                }
                return true;
            }
        }
        return false;
    }
    void read_any(boost::asio::serial_port& ser, int timeout_sec) {
        auto start = std::chrono::steady_clock::now();
        std::string buffer;
        boost::asio::serial_port::native_handle_type handle = ser.native_handle();

        while (std::chrono::steady_clock::now() - start < std::chrono::seconds(timeout_sec)) 
        {
            if (stop_requested_) return;

        
            COMSTAT status;
            DWORD errors;
            ClearCommError(handle, &errors, &status);

            if (status.cbInQue > 0)
            {
                //std::string dummy_buffer;
                read_chunk_into_buffer(ser, buffer);
                handle_pagination(ser, buffer);
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    // The "Witchcraft" Logic for Interrupts
    void perform_interrupt_sequence(boost::asio::serial_port& ser, const Step& step) {
        auto start = std::chrono::steady_clock::now();
        std::string buffer = "";
        bool is_break_mode = (step.interrupt.value() == "__BREAK__");

        log("[SYSTEM] Starting Interrupt Sequence: " + *step.interrupt);

        while (std::chrono::steady_clock::now() - start < std::chrono::seconds(step.timeout_sec)) {
            if (stop_requested_) return;

            if (is_break_mode) {
                // 1. Hardware Break
                #ifdef _WIN32
                    SetCommBreak(ser.native_handle());
                    std::this_thread::sleep_for(std::chrono::milliseconds(250));
                    ClearCommBreak(ser.native_handle());
                #endif
                
                // 2. Shotgun Characters
                boost::asio::write(ser, boost::asio::buffer("\x03\x1b\x00"));
            } else {
                // Standard Character Spam
                boost::asio::write(ser, boost::asio::buffer(*step.interrupt));
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            boost::asio::serial_port::native_handle_type handle = ser.native_handle();
            COMSTAT status;
            DWORD errors;
            ClearCommError(handle, &errors, &status);

            // 3. Read & Check
            if (status.cbInQue > 0) {
                read_chunk_into_buffer(ser, buffer);

                // Check for "Locked" Password Recovery Prompt
                if (is_break_mode && buffer.find("reset the") != std::string::npos) {
                     log("\n[SECURITY] Locked device detected. Authorizing destructive reset...");
                     write_line(ser, "y");
                     buffer.clear();
                 }

                // Check for Success Match
                boost::regex pattern(step.expect_regex.value(), boost::regex::icase);
                if (boost::regex_search(buffer, pattern)) {
                    log("\n[SUCCESS] Interrupt matched target prompt.");
                    return;
                }
            }
        }
        throw std::runtime_error("Timeout waiting for interrupt.");
    }


    void read_until(boost::asio::serial_port& ser, const std::string& pattern_str, int timeout_sec) {
        auto start = std::chrono::steady_clock::now();
        std::string buffer;
        boost::regex pattern(pattern_str, boost::regex::icase);

        boost::asio::serial_port::native_handle_type handle = ser.native_handle();


        while (std::chrono::steady_clock::now() - start < std::chrono::seconds(timeout_sec)) {
            if (stop_requested_) return;

            COMSTAT status;
            DWORD errors;
            ClearCommError(handle, &errors, &status);

            if (status.cbInQue > 0) {
                read_chunk_into_buffer(ser, buffer);

                if (handle_pagination(ser, buffer)) {
                    //reset timer after handling paging
                    start = std::chrono::steady_clock::now();
                    continue;
                }

                if (boost::regex_search(buffer, pattern)) return;


                // OLD PAGINATION HANDLE
                /* 
                    if (buffer.find("-- MORE --") != std::string::npos) {
                    boost::asio::write(ser, boost::asio::buffer(" "));
                    buffer.clear();
                }
                */

            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        throw std::runtime_error("Timeout waiting for: " + pattern_str);
    }

    void read_chunk_into_buffer(boost::asio::serial_port& ser, std::string& buffer) {
        std::array<char, 1024> data;
        boost::system::error_code ec;
        size_t len = ser.read_some(boost::asio::buffer(data), ec);
        if (!ec && len > 0) {
            std::string chunk(data.data(), len);
            // Basic sanitization (remove nulls)
            chunk.erase(std::remove(chunk.begin(), chunk.end(), '\0'), chunk.end());
            log_raw(chunk);
            buffer += chunk;
        }
    }

    void write_line(boost::asio::serial_port& ser, const std::string& cmd) {
        log("[TX] " + cmd);
        std::string payload = cmd + "\r";
        boost::asio::write(ser, boost::asio::buffer(payload));
    }

    void log(const std::string& msg) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        current_state_.text_log += msg + "\n";
    }

    void log_raw(const std::string& msg) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        current_state_.text_log += msg;
    }

    void update_status(std::string msg, bool interact = false, bool complete = false, bool fail = false) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        current_state_.status_msg = msg;
        current_state_.is_interactive = interact;
        current_state_.is_complete = complete;
        current_state_.is_failed = fail;
    }

    std::string port_name_;
    Workflow workflow_;
    std::atomic<bool> stop_requested_;
    std::mutex state_mutex_;
    EngineStatus current_state_;
};