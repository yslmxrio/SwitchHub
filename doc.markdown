Workflow Hub Documentation

This document explains the architecture and key functions of the serial automation application. The system is built on three main components:

app.py (The Frontend): A Streamlit web application that serves as the user-friendly "control panel." It displays containers, handles button clicks, and shows real-time status and logs.

workflows/workflow_runner.py (The Engine): A generic, standalone Python script that reads a JSON template and executes the serial automation. It's the "engine" that does the actual work.

workflow/templates/*.json (The "Instructions"): JSON files that define the exact steps, commands, and expected replies for a specific device (e.g., "Cisco 2960x").

High-Level Data Flow

User clicks "Start Workflow" for COM3 in the app.py web UI.

app.py launches workflow_runner.py as a subprocess.Popen task, passing it the COM3 port and the path to the selected *.json template.

workflow_runner.py (now a separate process) opens COM3, reads the JSON, and begins executing the steps.

workflow_runner.py communicates back to app.py in two ways:

Status (stdout): It prints STATUS_FLAG::{"text": "...", "interactive": ...} to stdout.

Logs (stderr): It prints all raw serial output and script logs to stderr.

app.py uses two threads per process to read these stdout and stderr streams in real-time. It uses queue.Queue to pass this data safely to the main Streamlit loop.

app.py updates the port_status (the colored text) and the outputs (the black log box) in the UI, which then re-renders to show the new information.

app.py - The Streamlit UI

This is the main "control panel" script. Its primary job is to manage the UI state and orchestrate the workflow_runner.py processes.

Key st.session_state Variables

outputs: Stores the raw log text for each port's black output box.

port_status: Stores the current status dictionary (e.g., {"text": "Processing...", "interactive": false}) for a port.

queues: Holds the queue.Queue objects for the main workflow logs.

ident_queues / ident_messages: Manages the state for the threaded "Ident" button.

pids: Stores the Process ID (PID) of each running workflow_runner.py subprocess, so the "Stop" button can terminate it.

port_workflows / asset_ids: Stores the user's selection from the dropdowns for each port.

Key Functions

run_workflow_on_port(workflow_path, com_port, q):

This function runs in a Streamlit thread when "Start" is clicked.

Its only job is to start the workflow_runner.py subprocess and create two more threads: one to read the subprocess's stdout (for status) and one to read its stderr (for logs).

These inner threads put all messages into the queue (q) that was passed in.

identify_port_threaded(com_port, q):

Runs the "Ident" LED blink logic in a background thread so the UI doesn't freeze.

Uses its own ident_queue (q) to send "info," "success," and "error" messages back to the UI.

get_status_html(status_data):

A helper function that takes the status_data dictionary and converts it into the correct colored HTML (e.g., status-blue, status-red, status-interactive).

This is what creates the flashing yellow and colored background tags.

workflows/workflow_runner.py - The Engine

This is a generic, standalone script that knows nothing about Streamlit. It's a "dumb" worker that just follows the JSON instructions.

Key Functions

send_status(text, is_interactive=False):

This is the primary communication method to app.py.

It wraps the status text and the interactive flag into a JSON object and prints it to stdout, prefixed by STATUS_FLAG::.

app.py listens only for this string on stdout.

log_output(message):

Prints all script-related logs (e.g., "[SCRIPT] Sending...") to stderr.

This cleanly separates them from the status flags.

clean_output(data):

A critical helper that removes terminal control characters (like \b and \r) from the raw serial output. This prevents garbled text (e.g., | /) from the switch's boot spinners.

read_until(ser, expect_regex, timeout):

The core serial logic. It reads from the port, cleans the data, logs it (via log_output), and checks if the expect_regex has been seen.

It also automatically handles Cisco's -- MORE -- pagination by sending a space character.

interrupt_and_read_until(ser, interrupt_char, expect_regex, timeout):

Used for boot-interrupt steps.

It repeatedly sends the interrupt_char (e.g., \u0003 for Ctrl+C) while reading until the expect_regex (e.g., loader>) is seen.

main(json_path, com_port):

Loads the JSON template.

Opens the serial port.

Loops through each step in the JSON.

Calls send_status() to update the UI.

Analyzes the step to decide whether to call interrupt_and_read_until, read_until (for "wait" steps), or send_command.

If any step fails or times out, it sends a Fatally Failed status and exits with an error code.

workflow/templates/*.json - The Instructions

These files define a workflow. They are the "brains" of the operation.

Key Fields in the Step Object

"name": A unique name for the step (e.g., "Deleting config.text").

"status": The user-friendly text that will be displayed in the UI for this step.

"command": The command to send, ending with a carriage return (e.g., flash_init).

"interrupt": Use this instead of "command" for boot interrupts. This is the character to send repeatedly (e.g., \u0003 for Ctrl+C,   for SPACE).

"expect": A regex string that the runner must see in the device's output before it considers the step complete. This is the most critical field.

"timeout": How many seconds to wait for the expect string before failing.

"require_physical_interact": If set to true, the workflow_runner will send a status flag that tells app.py to make the status text flash yellow (e.g., for the MODE button template).