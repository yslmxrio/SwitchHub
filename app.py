import streamlit as slit
import serial.tools.list_ports
import serial
import glob
import subprocess
import threading
import time
import sys
import queue
from datetime import datetime
import os
import signal
import json

# Define BAUD_RATE globally
BAUD_RATE = 9600

# Initialize session_state keys
if "outputs" not in slit.session_state:
    slit.session_state.outputs = {}
if "threads" not in slit.session_state:
    slit.session_state.threads = {}
if "queues" not in slit.session_state:
    slit.session_state.queues = {}
if "asset_ids" not in slit.session_state:
    slit.session_state.asset_ids = {}
if "port_workflows" not in slit.session_state:
    slit.session_state.port_workflows = {}
if "ident_queues" not in slit.session_state:
    slit.session_state.ident_queues = {}
if "ident_messages" not in slit.session_state:
    slit.session_state.ident_messages = {}
if "pids" not in slit.session_state:
    slit.session_state.pids = {}
if "port_status" not in slit.session_state:
    slit.session_state.port_status = {}

def get_com_ports():
    com_ports = serial.tools.list_ports.comports()
    return [com_port.device for com_port in com_ports]

def get_workflow_templates(workflow_dir="workflow/templates"):
    return glob.glob(f"{workflow_dir}/*.json")

def run_workflow_on_port(workflow_path, com_port, q):
    try:
        q.put(("info", f"--- Running {workflow_path} on {com_port} ---\n"))

        process = subprocess.Popen(
            [sys.executable, "-u", "workflow/workflow_runner.py", workflow_path, com_port],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            errors="ignore"
        )
        q.put(("pid", process.pid))

        def read_stderr():
            # Read character-by-character, not line-by-line
            # This fixes the bug where raw output was not appearing
            for char in iter(lambda: process.stderr.read(1), ''):
                q.put(("output", char))

        stderr_thread = threading.Thread(target=read_stderr)
        stderr_thread.start()
        for line in iter(process.stdout.readline, ''):
            if line.startswith("STATUS_FLAG::"):
                payload_str = line.replace("STATUS_FLAG::", "").strip()
                try:
                    # --- THIS IS THE NEW LOGIC ---
                    # Parse the JSON payload from the runner
                    status_data = json.loads(payload_str)
                    q.put(("status", status_data))
                except json.JSONDecodeError:
                    # Fallback for simple strings
                    q.put(("status", {"text": payload_str, "interactive": False}))
            else:
                q.put(("output", f"[STDOUT_UNEXPECTED] {line}"))

        process.stdout.close()
        process.stderr.close()
        stderr_thread.join()
        return_code = process.wait()

        if return_code == 0:
            q.put(("info", f"\n--- FINISHED {com_port}: SUCCESS ---"))
            q.put(("status", {"text": "Successfully Finished", "completed": True}))
        else:
            q.put(("info", f"\n--- FINISHED {com_port}: FAIL (Code {return_code}) ---"))
            q.put(("status", {"text": "Fatally Failed", "interactive": True}))

    except Exception as e:
        q.put(("info", f"\n!!!---!!! CRITICAL ERROR on {com_port} !!!---!!!\n{e}"))
        q.put(("status", {"text": "Fatally Failed", "interactive": True}))
    finally:
        q.put(("done", None))

# Streamlit UI
slit.set_page_config(layout="wide")

slit.markdown("""
<style>
/* ... existing styles for code block, button, etc ... */
.stCodeBlock pre {
    white-space: pre-wrap !important;
    word-wrap: break-word !important;
}
div[data-testid="stVerticalBlock"][style*="height: 400px"] {
    border: 1px solid #262730; 
    border-radius: 0.5rem; 
}
.stDownloadButton button {
    background-color: #FFC107; color: #000000; border: 1px solid #FFC107;
}
.stDownloadButton button:hover {
    background-color: #FFD700; color: #000000; border: 1px solid #FFD700;
}
.stDownloadButton button:disabled {
    background-color: #f0f2f6; color: #a0a4ac; border: 1px solid #f0f2f6;
}
[data-testid="stMarkdownContainer"] p {
    margin: 0;
}
.status-blue { 
    color: #007bff; 
    font-weight: 500; 
    background-color: rgba(0, 123, 255, 0.15); /* Blue with 15% opacity */
    padding: 2px 6px;
    border-radius: 4px;
    display: inline-block;
}
.status-green { color: #28a745; font-weight: 700; }
.status-red { color: #dc3545; font-weight: 700; }
.status-gray { 
    color: #6c757d; 
    font-weight: 500; 
    background-color: rgba(108, 117, 125, 0.15); /* Gray with 15% opacity */
    padding: 2px 6px;
    border-radius: 4px;
    display: inline-block;
}

/* --- NEW CSS FOR FLASHING --- */
@keyframes flash-yellow {
  0%, 100% { background-color: transparent; color: #FFC107; }
  50% { background-color: #FFC107; color: #111; }
}
@keyframes flash-red {
  0%, 100% {background-color: transparent; color: #DC3545; }
  50% { background-color: #DC3545; color: #111; }
}
@keyframes flash-green {
  0%, 100% {background-color: transparent; color: #28a745; }
  50% { background-color: #28a745; color: #111; }
}
.status-interactive {
  animation: flash-yellow 1.5s infinite;
  padding: 2px 6px;
  border-radius: 4px;
  font-weight: 700;
  display: inline-block; /* Ensures background fits text */
}
.status-completed {
  animation: flash-green 1.5s infinite;
  padding: 2px 6px;
  border-radius: 4px;
  font-weight: 700;
  display: inline-block; /* Ensures background fits text */
}
.status-failed {
  color: #dc3545; !important
  font-weight: 700;
  animation: flash-red 1.5s infinite;
  padding: 2px 6px;
  border-radius: 4px;
  font-weight: 700;
  display: inline-block; /* Ensures background fits text */
}
</style>
""", unsafe_allow_html=True)

slit.title("Workflow Hub")

def identify_port_threaded(com_port, q):
    ident_duration = 20
    try:
        q.put(("info", f"Sending {ident_duration}-sec blink to {com_port}"))
        with serial.Serial(com_port, BAUD_RATE, timeout=1) as ser:
            end_time = time.time() + ident_duration
            while time.time() < end_time:
                ser.write(b"IDENTIFYING_PORT\r\n")
                ser.flush()
                time.sleep(0.05)
        q.put(("success", f"Ident for {com_port} complete."))
        time.sleep(2)
    except Exception as e:
        q.put(("error", f"Could not open {com_port}. Is it in use? Error: {e}"))
        time.sleep(3)
    finally:
        q.put((None, None))

slit.header("Select COM Ports")
available_com_ports = get_com_ports()

if slit.checkbox("Select All COM Ports"):
    selected_com_ports = slit.multiselect(
        "COM Ports", available_com_ports, default=available_com_ports
    )
else:
    selected_com_ports = slit.multiselect("COM Ports", available_com_ports)

if slit.button("Prepare COM Port Containers", type="primary"):
    if not selected_com_ports:
        slit.error("You must select at least one COM Port")
    else:
        slit.info(f"Preparing {len(selected_com_ports)} port(s)...")
        slit.session_state.outputs = {}
        slit.session_state.threads = {}
        slit.session_state.queues = {}
        slit.session_state.asset_ids = {}
        slit.session_state.port_workflows = {}
        slit.session_state.ident_queues = {}
        slit.session_state.ident_messages = {}
        slit.session_state.pids = {}
        slit.session_state.port_status = {}
        for com_port in selected_com_ports:
            slit.session_state.outputs[f"output_{com_port}"] = "Waiting to start..."

active_threads = False
if slit.session_state.outputs:
    output_ports = list(slit.session_state.outputs.keys())
    port_count = len(output_ports)
    MAX_COLS_PER_ROW = 2

    # --- UPDATED HTML FUNCTION ---
    def get_status_html(status_data):
        # Default state
        status_text = "Idle"
        is_interactive = False
        is_completed = False

        if isinstance(status_data, dict):
            status_text = status_data.get("text", "Idle")
            is_interactive = status_data.get("interactive", False)
            is_completed = status_data.get("completed", True)
        elif status_data: # Fallback for old string
            status_text = str(status_data)

        # --- MODIFIED LOGIC ---
        # Prioritize red "Failed" status over flashing
        if status_text == "Fatally Failed":
            return f'<p class="status-failed">Err: {status_text}</p>'

        # Check for flashing flag
        if is_interactive:
            return f'<div class="status-interactive">Interact: {status_text}</div>'

        # Standard color logic
        if status_text == "Successfully Finished":
            return f'<p class="status-green status-completed">Status: {status_text}</p>'
        elif status_text == "Idle" or status_text == "Starting...":
            return f'<p class="status-gray">Status: {status_text}</p>'
        else:
            return f'<p class="status-blue">Processing: {status_text}</p>'

    available_workflows = get_workflow_templates()

    if port_count > 0:
        for i in range(0, port_count, MAX_COLS_PER_ROW):
            row_port_keys = output_ports[i : i + MAX_COLS_PER_ROW]
            output_cols = slit.columns(MAX_COLS_PER_ROW)

            for col_index, port_key in enumerate(row_port_keys):
                port_name = port_key.replace("output_", "")

                thread_is_running = (port_name in slit.session_state.threads and
                                     slit.session_state.threads[port_name].is_alive())

                if port_name in slit.session_state.queues:
                    q = slit.session_state.queues[port_name]
                    while not q.empty():
                        msg_type, msg = q.get()
                        if msg_type == "pid":
                            slit.session_state.pids[port_name] = msg
                        elif msg_type == "output":
                            slit.session_state.outputs[port_key] += msg
                        elif msg_type == "status":
                            # --- MODIFIED: 'msg' is now a dictionary ---
                            slit.session_state.port_status[port_name] = msg
                        elif msg_type == "info":
                            slit.session_state.outputs[port_key] += msg
                        elif msg_type == "done":
                            del slit.session_state.queues[port_name]
                            if port_name in slit.session_state.pids:
                                del slit.session_state.pids[port_name]
                            break

                with output_cols[col_index]:
                    # ... (Ident logic remains the same) ...
                    ident_placeholder = slit.empty()
                    if port_name in slit.session_state.ident_queues:
                        q = slit.session_state.ident_queues[port_name]
                        while not q.empty():
                            msg_type, msg = q.get()
                            if msg_type is None:
                                del slit.session_state.ident_queues[port_name]
                                if port_name in slit.session_state.ident_messages:
                                    del slit.session_state.ident_messages[port_name]
                                ident_placeholder.empty()
                            else:
                                slit.session_state.ident_messages[port_name] = (msg_type, msg)

                    if port_name in slit.session_state.ident_messages:
                        msg_type, msg = slit.session_state.ident_messages[port_name]
                        if msg_type == "info":
                            ident_placeholder.info(msg)
                        elif msg_type == "success":
                            ident_placeholder.success(msg)
                        elif msg_type == "error":
                            ident_placeholder.error(msg)

                    title_col, button_col, = slit.columns([3,1])
                    with title_col:
                        slit.subheader(port_name)
                    with button_col:
                        slit.write("")
                        ident_is_running = port_name in slit.session_state.ident_queues
                        if slit.button("Ident", key=f"blink_{port_name}",
                                       use_container_width=True, disabled=thread_is_running or ident_is_running):
                            q = queue.Queue()
                            slit.session_state.ident_queues[port_name] = q
                            thread = threading.Thread(target=identify_port_threaded, args=(port_name, q))
                            thread.start()
                            slit.rerun()

                    # --- MODIFIED: Status display ---
                    # Get the status dict, or a default dict if none
                    status_data = slit.session_state.port_status.get(port_name, {"text": "Idle", "interactive": False, "completed": False})
                    slit.markdown(get_status_html(status_data), unsafe_allow_html=True)

                    current_assetid = slit.session_state.asset_ids.get(port_name, "")
                    # ... (rest of Asset ID, Selectbox, Start/Stop buttons, etc. is unchanged) ...
                    slit.session_state.asset_ids[port_name] = slit.text_input(
                        "Asset ID", value=current_assetid, key=f"asset_id_{port_name}"
                    )

                    current_workflow = slit.session_state.port_workflows.get(port_name)
                    default_index = 0
                    if current_workflow and current_workflow in available_workflows:
                        default_index = available_workflows.index(current_workflow)
                    slit.session_state.port_workflows[port_name] = slit.selectbox(
                        "Workflow Template",
                        options=available_workflows,
                        index=default_index,
                        key=f"workflow_{port_name}",
                        disabled=thread_is_running
                    )

                    if thread_is_running:
                        if slit.button("Stop Workflow", key=f"stop_{port_name}",
                                       use_container_width=True, type="primary",
                                       help="Stops the workflow. The thread will finish and post a FAIL message."):
                            pid = slit.session_state.pids.get(port_name)
                            if pid:
                                try:
                                    os.kill(pid, signal.SIGTERM)
                                    ident_placeholder.warning(f"Sent stop signal to {port_name} (PID: {pid})...")
                                except ProcessLookupError:
                                    ident_placeholder.error(f"Process {pid} already dead.")
                                except Exception as e:
                                    ident_placeholder.error(f"Could not stop process: {e}")
                    else:
                        if slit.button("Start Workflow", key=f"start_{port_name}",
                                       use_container_width=True, disabled=ident_is_running):
                            workflow_to_run = slit.session_state.port_workflows.get(port_name)
                            asset_id = slit.session_state.asset_ids.get(port_name, "")
                            if not workflow_to_run:
                                slit.error("No workflow selected for this port!")
                            elif not asset_id:
                                slit.error(f"Asset ID is required!")
                            else:
                                if port_name in slit.session_state.pids:
                                    del slit.session_state.pids[port_name]
                                slit.session_state.outputs[port_key] = ""
                                # --- MODIFIED: Set status as dict ---
                                slit.session_state.port_status[port_name] = {"text": "Starting...", "interactive": False, "completed": False}
                                q = queue.Queue()
                                slit.session_state.queues[port_name] = q
                                thread = threading.Thread(
                                    target=run_workflow_on_port,
                                    args=(workflow_to_run, port_name, q)
                                )
                                slit.session_state.threads[port_name] = thread
                                thread.start()
                                slit.rerun()

                    log_data = slit.session_state.outputs.get(port_key, "")
                    asset_id = slit.session_state.asset_ids.get(port_name, "NO_ASSET_ID")
                    timestamp = datetime.now().strftime("%d.%m.%Y_%H-%M-%S")
                    file_name = f"{asset_id}_{timestamp}.log"
                    slit.download_button(
                        label="Save Log",
                        data=log_data.encode("utf-8"),
                        file_name=file_name,
                        mime="text/plain",
                        key=f"download_{port_name}",
                        use_container_width=True,
                        disabled=(not log_data)
                    )

                    output_container = output_cols[col_index].container(height=400)
                    output_container.markdown(f"```bash\n{slit.session_state.outputs.get(port_key, '')}\n```")

                if port_name in slit.session_state.threads and \
                        slit.session_state.threads[port_name].is_alive():
                    active_threads = True
                if port_name in slit.session_state.ident_queues:
                    active_threads = True

    slit.components.v1.html(
        """
        <script>
        (function() {
            function scrollAllContainers() {
                var containers = window.parent.document.querySelectorAll('div[data-testid="stVerticalBlock"][style*="height: 400px"]');
                containers.forEach(function(container) {
                    container.scrollTop = container.scrollHeight;
                });
            }
            setTimeout(scrollAllContainers, 0);
        })();
        </script>
        """,
        height=0
    )

if active_threads:
    time.sleep(0.1)
    slit.rerun()