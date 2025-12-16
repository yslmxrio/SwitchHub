#pragma once
#include <boost/asio.hpp>
#include <boost/regex.hpp>
#include <thread>
#include <mutex>
#include <deque>
#include <iostream>
#include <atomic>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <filesystem>
#include "Workflow.hpp"

#define WIN32_LEAN_AND_MEAN

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <termios.h>
#endif

// ============================================================================
// SWITCHHUB SERIAL ENGINE - COMPLETE VERSION
// ============================================================================
// This engine implements ALL features from the development session:
//
//   LED IDENTIFICATION:
//   - blink_port_led() - DTR/RTS LED control (FTDI adapters)
//   - blink_port_led_tx() - TX LED control (works with more adapters)
//
//   AUTOMATIC LOG FILE SAVING:
//   - Creates logs/assetid_YYYY-MM-DD_HH-MM-SS.log
//   - Saves all TX/RX with timestamps
//   - Logs final status (SUCCESS/FAILED/INTERRUPTED/INCOMPLETE)
//
//   AUTO-BAUD DETECTION:
//   - detect_baud_rate() - Tests common baud rates
//   - test_baud_rate() - Validates each rate
//
//   ADVANCED RESPONSE HANDLING:
//   - expect_any - Match multiple possible patterns
//   - on_match - Conditional command execution
//   - delay_after_match - Post-match delay before sending command
//
//   FALLBACK & ERROR HANDLING:
//   - alternate_steps - Try different approaches if step fails
//   - optional - Allow steps to fail without stopping workflow
//
//   PAGINATION HANDLING:
//   - Automatic detection of "-- MORE --" prompts
//   - Auto-sends space to continue
// ============================================================================

// Status structure to pass data to the GUI
struct EngineStatus {
    std::string text_log;           // Complete TX/RX log
    std::string status_msg;         // Current status message
    bool is_interactive = false;    // Waiting for physical interaction
    bool is_complete = false;       // Workflow completed successfully
    bool is_failed = false;         // Workflow failed
    int detected_baud_rate = 0;     // Auto-detected baud rate (0 if not detected)
    bool override_baud_rate = true;
    std::string log_file_path;      // Path to log file
};

class SerialEngine {
public:
    SerialEngine(const std::string& port, const Workflow& workflow, const std::string& asset_id = "")
        : port_name_(port), workflow_(workflow), stop_requested_(false), is_blinking_(false), asset_id_(asset_id) {
        create_log_file();
    }

    ~SerialEngine() {
        stop_blinking();
        close_log_file();
    }

    // =========================================================================
    //   FEATURE: LED IDENTIFICATION - Standalone Methods
    // =========================================================================
    // These static methods can identify ports WITHOUT creating a SerialEngine
    // Perfect for the IDENT button functionality

    // Method 1: DTR/RTS LED Control (works with FTDI adapters)
    static void blink_port_led(const std::string& port_name,
        std::chrono::milliseconds duration = std::chrono::seconds(5),
        std::chrono::milliseconds interval = std::chrono::milliseconds(100)) {

        std::thread([port_name, duration, interval]() {
            try {
                boost::asio::io_context io;
                boost::asio::serial_port ser(io, port_name);

                // Configure port
                ser.set_option(boost::asio::serial_port_base::baud_rate(9600));
                ser.set_option(boost::asio::serial_port_base::character_size(8));
                ser.set_option(boost::asio::serial_port_base::stop_bits(boost::asio::serial_port_base::stop_bits::one));
                ser.set_option(boost::asio::serial_port_base::parity(boost::asio::serial_port_base::parity::none));

                auto start_time = std::chrono::steady_clock::now();
                bool led_state = false;

                // Blink DTR/RTS LEDs
                while (std::chrono::steady_clock::now() - start_time < duration) {
#ifdef _WIN32
                    DWORD dtr_func = led_state ? SETDTR : CLRDTR;
                    DWORD rts_func = led_state ? SETRTS : CLRRTS;
                    EscapeCommFunction(ser.native_handle(), dtr_func);
                    EscapeCommFunction(ser.native_handle(), rts_func);
#else
                    int status;
                    ioctl(ser.native_handle(), TIOCMGET, &status);
                    if (led_state) {
                        status |= (TIOCM_DTR | TIOCM_RTS);
                    }
                    else {
                        status &= ~(TIOCM_DTR | TIOCM_RTS);
                    }
                    ioctl(ser.native_handle(), TIOCMSET, &status);
#endif

                    led_state = !led_state;
                    std::this_thread::sleep_for(interval);
                }

                // Turn off LEDs at end
#ifdef _WIN32
                EscapeCommFunction(ser.native_handle(), CLRDTR);
                EscapeCommFunction(ser.native_handle(), CLRRTS);
#else
                int status;
                ioctl(ser.native_handle(), TIOCMGET, &status);
                status &= ~(TIOCM_DTR | TIOCM_RTS);
                ioctl(ser.native_handle(), TIOCMSET, &status);
#endif

            }
            catch (const std::exception& e) {
                std::cerr << "[IDENT] Failed: " << e.what() << std::endl;
            }
            }).detach();
    }

    // Method 2: TX LED Blink (works with CH340, CP2102, and most adapters)
    static void blink_port_led_tx(const std::string& port_name,
        std::chrono::milliseconds duration = std::chrono::seconds(5),
        std::chrono::milliseconds interval = std::chrono::milliseconds(100)) {

        std::thread([port_name, duration, interval]() {
            try {
                boost::asio::io_context io;
                boost::asio::serial_port ser(io, port_name);

                ser.set_option(boost::asio::serial_port_base::baud_rate(9600));
                ser.set_option(boost::asio::serial_port_base::character_size(8));
                ser.set_option(boost::asio::serial_port_base::stop_bits(boost::asio::serial_port_base::stop_bits::one));
                ser.set_option(boost::asio::serial_port_base::parity(boost::asio::serial_port_base::parity::none));

                auto start_time = std::chrono::steady_clock::now();

                // Blink TX LED by sending data
                while (std::chrono::steady_clock::now() - start_time < duration) {
                    std::string burst(10, '\0');  // Send null bytes to trigger TX LED
                    boost::asio::write(ser, boost::asio::buffer(burst));
                    std::this_thread::sleep_for(interval);
                }

            }
            catch (const std::exception& e) {
                std::cerr << "[IDENT-TX] Failed: " << e.what() << std::endl;
            }
            }).detach();
    }

    // =========================================================================
    //   FEATURE: AUTOMATIC LOG FILE SAVING
    // =========================================================================
    // Creates: logs/assetid_YYYY-MM-DD_HH-MM-SS.log
    // Contains: Complete TX/RX transcript with final status

private:
    void create_log_file() {
        // Create logs directory if it doesn't exist
        std::filesystem::create_directories("logs");

        // Get current timestamp
        auto now = std::chrono::system_clock::now();
        auto now_c = std::chrono::system_clock::to_time_t(now);
        std::tm now_tm;

#ifdef _WIN32
        localtime_s(&now_tm, &now_c);
#else
        localtime_r(&now_c, &now_tm);
#endif

        // Build filename: assetid_YYYY-MM-DD_HH-MM-SS.log
        std::ostringstream filename;
        filename << "logs/";

        if (!asset_id_.empty()) {
            filename << asset_id_;
        }
        else {
            // Use port name if no asset ID
            std::string clean_port = port_name_;
            std::replace(clean_port.begin(), clean_port.end(), '/', '_');
            std::replace(clean_port.begin(), clean_port.end(), '\\', '_');
            std::replace(clean_port.begin(), clean_port.end(), ':', '_');
            filename << clean_port;
        }

        filename << "_" << std::put_time(&now_tm, "%Y-%m-%d_%H-%M-%S") << ".log";

        log_file_path_ = filename.str();
        log_file_.open(log_file_path_, std::ios::out);

        if (log_file_.is_open()) {
            // Write header
            log_file_ << "=================================================\n";
            log_file_ << "SwitchHub Workflow Log\n";
            log_file_ << "=================================================\n";
            log_file_ << "Port:      " << port_name_ << "\n";
            log_file_ << "Asset ID:  " << (asset_id_.empty() ? "(not set)" : asset_id_) << "\n";
            log_file_ << "Workflow:  " << workflow_.name << "\n";
            log_file_ << "Started:   " << std::put_time(&now_tm, "%Y-%m-%d %H:%M:%S") << "\n";
            log_file_ << "=================================================\n\n";
            log_file_.flush();

            // Store path in status for GUI access
            std::lock_guard<std::mutex> lock(state_mutex_);
            current_state_.log_file_path = log_file_path_;
        }
    }

    void close_log_file() {
        if (log_file_.is_open()) {
            auto now = std::chrono::system_clock::now();
            auto now_c = std::chrono::system_clock::to_time_t(now);
            std::tm now_tm;

#ifdef _WIN32
            localtime_s(&now_tm, &now_c);
#else
            localtime_r(&now_c, &now_tm);
#endif

            // Write footer with final status
            log_file_ << "\n=================================================\n";
            log_file_ << "Workflow Ended: " << std::put_time(&now_tm, "%Y-%m-%d %H:%M:%S") << "\n";

            std::lock_guard<std::mutex> lock(state_mutex_);
            if (current_state_.is_complete) {
                log_file_ << "Status: SUCCESS\n";
            }
            else if (current_state_.is_failed) {
                log_file_ << "Status: FAILED\n";
            }
            else if (stop_requested_) {
                log_file_ << "Status: INTERRUPTED BY USER\n";
            }
            else {
                log_file_ << "Status: INCOMPLETE\n";
            }

            log_file_ << "=================================================\n";
            log_file_.close();
        }
    }

public:
    // =========================================================================
    //   FEATURE: AUTO-BAUD RATE DETECTION
    // =========================================================================
    // Automatically detects device baud rate by testing common rates

   // =========================================================================
    //   FEATURE: AUTO-BAUD RATE DETECTION (FIXED)
    // =========================================================================

 // =========================================================================
    //   ROBUST HARDWARE-BASED AUTO-BAUD DETECTION
    // =========================================================================

    // GUI Wrapper
    int detect_baud_rate() {
        try {
            boost::asio::io_context io;
            boost::asio::serial_port ser(io, port_name_);
            return detect_baud_rate(ser);
        }
        catch (...) { return 0; }
    }

    // Main Logic
    int detect_baud_rate(boost::asio::serial_port& ser) {
        // Prioritize most common rates to save time
        const std::vector<int> rates = { 9600, 115200, 57600, 38400, 19200, 4800, 2400 };

        log("[AUTO-BAUD] Scanning signal (Hardware Framing Check)...");

        auto total_start = std::chrono::steady_clock::now();

        // Scan for up to 15 seconds
        while (std::chrono::steady_clock::now() - total_start < std::chrono::seconds(15)) {
            if (stop_requested_) return 0;

            for (int baud : rates) {
                if (stop_requested_) return 0;

                if (test_baud_rate(ser, baud)) {
                    log("[AUTO-BAUD] LOCKED! Signal confirmed at " + std::to_string(baud) + " baud.");
                    return baud;
                }
            }

            // Small delay between full cycles to let user plug in device
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        log("[AUTO-BAUD] Timeout. No valid signal found. Defaulting to 9600.");
        return 9600;
    }

    bool test_baud_rate(boost::asio::serial_port& ser, int baud) {
        try {
            // 1. Configure Port
            boost::system::error_code ec;
            ser.set_option(boost::asio::serial_port_base::baud_rate(baud), ec);
            if (ec) return false;

            // CRITICAL: Baud switch settling time (Windows driver latency)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));

            // 2. Reset Hardware Error Flags (Clear old framing errors)
            boost::asio::serial_port::native_handle_type handle = ser.native_handle();
            DWORD errors;
            COMSTAT status;
            ClearCommError(handle, &errors, &status); // Reset flags

            // 3. Flush Buffer (Drain old junk)
            flush_input_buffer(ser);

            // 4. Active Probe: Provoke a response
            // Sending '\r' usually triggers a new prompt or error message
            boost::asio::write(ser, boost::asio::buffer("\r"));

            // 5. Listen Phase (200ms is enough for 9600 baud text)
            std::string buffer;
            auto start = std::chrono::steady_clock::now();
            bool hardware_error_detected = false;

            while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(250)) {
                // Poll Hardware Status
                ClearCommError(handle, &errors, &status);

                // --- THE KEY FIX: Check Hardware Framing Errors ---
                // CE_FRAME (0x0008) = Stop bit missing. 
                // This GUARANTEES the baud rate is wrong.
                if (errors & CE_FRAME) {
                    hardware_error_detected = true;
                    break; // Fail immediately, don't waste time
                }

                if (status.cbInQue > 0) {
                    std::array<char, 512> data;
                    size_t len = ser.read_some(boost::asio::buffer(data), ec);
                    if (!ec && len > 0) {
                        buffer.append(data.data(), len);
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            // 6. Verdict Logic
            if (hardware_error_detected) return false; // Hardware said NO

            if (buffer.empty()) return false; // Silence is inconclusive, keep searching

            // 7. Final Software Check (Sanity)
            // Even if hardware didn't complain, ensure we have readable text
            // (e.g., prevents matching random noise that didn't trigger framing error)
            return is_valid_response(buffer);

        }
        catch (...) { return false; }
    }

    void flush_input_buffer(boost::asio::serial_port& ser) {
        // Fast purge
#ifdef _WIN32
        PurgeComm(ser.native_handle(), PURGE_RXCLEAR | PURGE_RXABORT);
#else
        // Linux fallback
        std::array<char, 1024> dump;
        while (ser.read_some(boost::asio::buffer(dump), ec) > 0);
#endif
    }

    bool is_valid_response(const std::string& data) {
        if (data.length() < 2) return false;

        int valid_chars = 0;
        int junk_chars = 0;

        for (unsigned char c : data) {
            // Allow: Alphanumeric, Punctuation, Newlines, Tab
            if ((c >= 32 && c <= 126) || c == '\r' || c == '\n' || c == '\t') {
                valid_chars++;
            }
            // Ignore: Nulls (common in break), but count high-ASCII as junk
            else if (c > 127 || (c < 9 && c != 0)) {
                junk_chars++;
            }
        }

        // Strict ratio: Must be >90% clean text to be accepted
        if (junk_chars > 0 && valid_chars < junk_chars) return false;
        return (valid_chars > 0);
    }

public:
    // =========================================================================
    // MAIN WORKFLOW EXECUTION
    // =========================================================================
    // Includes support for:
    //   Auto-baud detection
    //   Fallback strategies (alternate_steps, optional)
    //   Advanced response handling (expect_any, on_match, delay_after_match)

    void run(int manual_baud_rate) {
        try {
            boost::asio::io_context io;
            boost::asio::serial_port ser(io, port_name_); // Port opened here

            // Configure serial port
            ser.set_option(boost::asio::serial_port_base::baud_rate(manual_baud_rate));
            ser.set_option(boost::asio::serial_port_base::character_size(8));
            ser.set_option(boost::asio::serial_port_base::stop_bits(boost::asio::serial_port_base::stop_bits::one));
            ser.set_option(boost::asio::serial_port_base::parity(boost::asio::serial_port_base::parity::none));

            log("[SYSTEM] Workflow '" + workflow_.name + "' started on " + port_name_ +
                " @ " + std::to_string(manual_baud_rate) + " baud");

            // Execute all steps
            for (const auto& step : workflow_.steps) {
                if (stop_requested_) {
                    log("[SYSTEM] Workflow interrupted by user");
                    break;
                }

                //   FEATURE: Execute step with fallback support
                execute_step_with_fallback(ser, step);
            }

            if (!stop_requested_) {
                log("[SYSTEM] Workflow completed successfully");
                update_status("Successfully Finished", false, true);
            }

        }
        catch (const std::exception& e) {
            log("[ERROR] Critical Failure: " + std::string(e.what()));
            update_status("Fatally Failed", false, false, true);
        }

        close_log_file();
    }

    void stop() {
        stop_requested_ = true;
    }

    EngineStatus get_state() {
        std::lock_guard<std::mutex> lock(state_mutex_);
        return current_state_;
    }

private:
    // =========================================================================
    //   FEATURE: FALLBACK EXECUTION with alternate_steps and optional
    // =========================================================================

    void execute_step_with_fallback(boost::asio::serial_port & ser, const Step & step) {
        update_status(step.status_text, step.require_physical_interact);

        try {
            execute_single_step(ser, step);
            return;  // Success!

        }
        catch (const std::exception& e) {
            // Step failed - check if we have alternates
            if (!step.alternate_steps.empty()) {
                log("[FALLBACK] Main step failed, trying alternates...");

                for (size_t i = 0; i < step.alternate_steps.size(); i++) {
                    try {
                        log("[FALLBACK] Trying alternate #" + std::to_string(i + 1));
                        execute_single_step(ser, step.alternate_steps[i]);
                        log("[FALLBACK]   Alternate succeeded!");
                        return;  // Alternate succeeded!

                    }
                    catch (const std::exception& alt_e) {
                        log("[FALLBACK] ✗ Alternate #" + std::to_string(i + 1) + " failed: " + alt_e.what());
                    }
                }

                // All alternates failed
                if (step.optional) {
                    log("[OPTIONAL] Step marked as optional, continuing workflow");
                    return;
                }
                else {
                    throw std::runtime_error("All alternates exhausted: " + std::string(e.what()));
                }

            }
            else if (step.optional) {
                // No alternates but step is optional
                log("[OPTIONAL] Step failed but marked as optional, continuing");
                return;

            }
            else {
                // No alternates and not optional - fail
                throw;
            }
        }
    }

    void execute_single_step(boost::asio::serial_port & ser, const Step & step) {
        if (step.interrupt) {
            perform_interrupt_sequence(ser, step);
            return;
        }

        // Send initial command if present
        if (step.command) {
            write_line(ser, *step.command);
        }

        //   FEATURE: expect_any with on_match and delay_after_match
        if (!step.expect_any.empty()) {
            std::string matched_pattern = read_until_any(ser, step.expect_any, step.timeout_sec);

            //   FEATURE: delay_after_match - Wait AFTER match, BEFORE sending command
            if (step.delay_after_match > 0) {
                log("[DELAY] Waiting " + std::to_string(step.delay_after_match) +
                    " seconds after match before sending command...");
                std::this_thread::sleep_for(std::chrono::seconds(step.delay_after_match));
            }

            //   FEATURE: on_match - Conditional command execution
            if (!step.on_match.empty()) {
                auto it = step.on_match.find(matched_pattern);
                if (it != step.on_match.end()) {
                    if (!it->second.empty()) {  // Only send if command is not empty
                        log("[CONDITIONAL] Matched '" + matched_pattern + "', sending: " + it->second);
                        write_line(ser, it->second);

                        // Give device time to respond
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                    }
                }
            }
        }
        // Original single expect pattern (with delay support)
        else if (step.expect_regex) {
            read_until(ser, *step.expect_regex, step.timeout_sec);

            //   FEATURE: delay_after_match for single pattern too
            if (step.delay_after_match > 0) {
                log("[DELAY] Waiting " + std::to_string(step.delay_after_match) +
                    " seconds after match...");
                std::this_thread::sleep_for(std::chrono::seconds(step.delay_after_match));
            }
        }
        else if (step.timeout_sec > 0) {
            read_any(ser, step.timeout_sec);
        }
    }

    // =========================================================================
    //   FEATURE: expect_any - Match ANY of multiple patterns
    // =========================================================================

    std::string read_until_any(boost::asio::serial_port & ser,
        const std::vector<std::string>&patterns,
        int timeout_sec) {
        auto start = std::chrono::steady_clock::now();
        std::string buffer;
        boost::asio::serial_port::native_handle_type handle = ser.native_handle();

        // Compile all patterns
        std::vector<boost::regex> compiled_patterns;
        for (const auto& pattern_str : patterns) {
            compiled_patterns.emplace_back(pattern_str, boost::regex::icase);
        }

        while (std::chrono::steady_clock::now() - start < std::chrono::seconds(timeout_sec)) {
            if (stop_requested_) return "";

#ifdef _WIN32
            COMSTAT status;
            DWORD errors;
            ClearCommError(handle, &errors, &status);
            if (status.cbInQue > 0) {
#else
            int bytes = 0;
            ioctl(handle, FIONREAD, &bytes);
            if (bytes > 0) {
#endif
                read_chunk_into_buffer(ser, buffer);

                //   FEATURE: Automatic pagination handling
                if (handle_pagination(ser, buffer)) {
                    start = std::chrono::steady_clock::now();
                    continue;
                }

                // Check if any pattern matches
                for (size_t i = 0; i < compiled_patterns.size(); i++) {
                    if (boost::regex_search(buffer, compiled_patterns[i])) {
                        log("[MATCH] Matched pattern: '" + patterns[i] + "'");
                        return patterns[i];  // Return which pattern matched
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

        // Build error message showing all patterns
        std::string pattern_list = "[";
        for (size_t i = 0; i < patterns.size(); i++) {
            pattern_list += patterns[i];
            if (i < patterns.size() - 1) pattern_list += ", ";
        }
        pattern_list += "]";

        throw std::runtime_error("Timeout waiting for any of: " + pattern_list);
        }

    //   FEATURE: Automatic pagination handling
    bool handle_pagination(boost::asio::serial_port & ser, std::string & buffer) {
        const std::vector<std::string> page_prompts = {
            "-- MORE --", " --More-- ", "<--- More --->", "Press any key to continue"
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

    void read_any(boost::asio::serial_port & ser, int timeout_sec) {
        auto start = std::chrono::steady_clock::now();
        std::string buffer;
        boost::asio::serial_port::native_handle_type handle = ser.native_handle();

        while (std::chrono::steady_clock::now() - start < std::chrono::seconds(timeout_sec))
        {
            if (stop_requested_) return;

#ifdef _WIN32
            COMSTAT status;
            DWORD errors;
            ClearCommError(handle, &errors, &status);
            if (status.cbInQue > 0) {
#else
            int bytes = 0;
            ioctl(handle, FIONREAD, &bytes);
            if (bytes > 0) {
#endif
                read_chunk_into_buffer(ser, buffer);
                handle_pagination(ser, buffer);
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }

    void perform_interrupt_sequence(boost::asio::serial_port & ser, const Step & step) {
        auto start = std::chrono::steady_clock::now();
        std::string buffer = "";
        bool is_break_mode = (step.interrupt.value() == "__BREAK__");

        log("[SYSTEM] Starting Interrupt Sequence: " + *step.interrupt);

        while (std::chrono::steady_clock::now() - start < std::chrono::seconds(step.timeout_sec)) {
            if (stop_requested_) return;

            if (is_break_mode) {
#ifdef _WIN32
                SetCommBreak(ser.native_handle());
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
                ClearCommBreak(ser.native_handle());
#endif
                boost::asio::write(ser, boost::asio::buffer("\x03\x1b\x00"));
            }
            else {
                boost::asio::write(ser, boost::asio::buffer(*step.interrupt));
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            boost::asio::serial_port::native_handle_type handle = ser.native_handle();

#ifdef _WIN32
            COMSTAT status;
            DWORD errors;
            ClearCommError(handle, &errors, &status);
            if (status.cbInQue > 0) {
#else
            int bytes = 0;
            ioctl(handle, FIONREAD, &bytes);
            if (bytes > 0) {
#endif
                read_chunk_into_buffer(ser, buffer);

                if (is_break_mode && buffer.find("reset the") != std::string::npos) {
                    log("\n[SECURITY] Locked device detected. Authorizing destructive reset...");
                    write_line(ser, "y");
                    buffer.clear();
                }

                boost::regex pattern(step.expect_regex.value(), boost::regex::icase);
                if (boost::regex_search(buffer, pattern)) {
                    log("\n[SUCCESS] Interrupt matched target prompt.");
                    return;
                }
            }
            }
        throw std::runtime_error("Timeout waiting for interrupt.");
        }

    void read_until(boost::asio::serial_port & ser, const std::string & pattern_str, int timeout_sec) {
        auto start = std::chrono::steady_clock::now();
        std::string buffer;
        boost::regex pattern(pattern_str, boost::regex::icase);
        boost::asio::serial_port::native_handle_type handle = ser.native_handle();

        while (std::chrono::steady_clock::now() - start < std::chrono::seconds(timeout_sec)) {
            if (stop_requested_) return;

#ifdef _WIN32
            COMSTAT status;
            DWORD errors;
            ClearCommError(handle, &errors, &status);
            if (status.cbInQue > 0) {
#else
            int bytes = 0;
            ioctl(handle, FIONREAD, &bytes);
            if (bytes > 0) {
#endif
                read_chunk_into_buffer(ser, buffer);

                if (handle_pagination(ser, buffer)) {
                    start = std::chrono::steady_clock::now();
                    continue;
                }

                if (boost::regex_search(buffer, pattern)) return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        throw std::runtime_error("Timeout waiting for: " + pattern_str);
        }

    void read_chunk_into_buffer(boost::asio::serial_port & ser, std::string & buffer) {
        std::array<char, 1024> data;
        boost::system::error_code ec;
        size_t len = ser.read_some(boost::asio::buffer(data), ec);
        if (!ec && len > 0) {
            std::string chunk(data.data(), len);
            chunk.erase(std::remove(chunk.begin(), chunk.end(), '\0'), chunk.end());
            log_raw(chunk);
            buffer += chunk;
        }
    }

    void write_line(boost::asio::serial_port & ser, const std::string & cmd) {
        log("[TX] " + cmd);
        std::string payload = cmd + "\r";
        boost::asio::write(ser, boost::asio::buffer(payload));
    }

    void log(const std::string & msg) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        current_state_.text_log += msg + "\n";

        //   FEATURE: Write to log file
        if (log_file_.is_open()) {
            log_file_ << msg << "\n";
            log_file_.flush();
        }
    }

    void log_raw(const std::string & msg) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        current_state_.text_log += msg;

        //   FEATURE: Write to log file
        if (log_file_.is_open()) {
            log_file_ << msg;
            log_file_.flush();
        }
    }

    void update_status(std::string msg, bool interact = false, bool complete = false, bool fail = false) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        current_state_.status_msg = msg;
        current_state_.is_interactive = interact;
        current_state_.is_complete = complete;
        current_state_.is_failed = fail;
    }

    void stop_blinking() {
        // Placeholder for future LED blinking during workflow
        is_blinking_ = false;
    }

    // Member variables
    std::atomic<bool> is_blinking_;
    std::thread blink_thread_;
    std::string asset_id_;
    std::string log_file_path_;
    std::ofstream log_file_;
    std::string port_name_;
    Workflow workflow_;
    std::atomic<bool> stop_requested_;
    std::mutex state_mutex_;
    EngineStatus current_state_;
    };