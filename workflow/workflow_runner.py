import serial
import time
import sys
import json
import re
import ctypes
from ctypes import wintypes

STATUS_FLAG = "STATUS_FLAG::"
BAUD_RATE = 9600

def log_output(message):
    print(f"[SCRIPT] {message}", file=sys.stderr, flush=True)

def clean_output(data):
    cleaned_data = re.sub(r'[\x00-\x08\x0B\x0C\x0D\x0E-\x1F\x7F]', '', data)
    return cleaned_data

def send_command(ser, cmd):
    log_output(f">> Sending: {cmd}")
    ser.write(cmd.encode('ascii') + b'\r')
    ser.flush()

def interrupt_and_read_until(ser, interrupt_char, expect_regex, timeout=120):
    log_output(f"Sending interrupt '{interrupt_char.encode()}' until '{expect_regex}' is seen...")
    buffer = ""
    start_time = time.time()

    #cycle = 0
    try:
        com_handle = ser._port_handle
    except AttributeError:
        log_output("Could not grab COM handle. Reverting to standard break.")
        com_handle = None

    if com_handle is None:
        try:
            com_handle = ser.hComPort
        except AttributeError:
            pass

    # WITCH CRAFT DO NOT TOUCH
    while time.time() - start_time < timeout:

        if interrupt_char == "__BREAK__":
            if com_handle:
                ctypes.windll.kernel32.SetCommBreak(wintypes.HANDLE(com_handle))
                time.sleep(0.25)
                ctypes.windll.kernel32.ClearCommBreak(wintypes.HANDLE(com_handle))
            else:
                ser.send_break(duration=0.25)
            ser.write(b'\x03\x1b\x00')
            ser.flush()
            print("![WIN_API_HYBRID]", file=sys.stderr, end='', flush=True)
        else:
            ser.write(interrupt_char.encode('ascii'))
            ser.flush()

        """ interrupt_char == "__BREAK__":
            ser.send_break(duration=0.5)
            print("!", file=sys.stderr, end='', flush=True)
            if cycle % 4 == 0:
                ser.break_condition = True
                time.sleep(2.0)
                ser.break_condition = False
                print("![LONG]", file=sys.stderr, end='', flush=True)
            elif cycle % 4 == 1:
                ser.write(b'\x1b' * 10)
                ser.flush()
                print("![ESC]", file=sys.stderr, end='', flush=True)
            elif cycle % 4 == 2:
                ser.write(b'\x30' * 10)
                ser.flush()
                print("![^C]", file=sys.stderr, end='', flush=True)
            elif cycle % 4 == 3:
                ser.write(b'\x00' * 50)
                ser.flush()
                print("![NULL]", file=sys.stderr, end='', flush=True)
                
            cycle += 1

            ser.baudrate = 300
            ser.write(b'\x20' * 15)
            ser.flush()
            ser.baudrate = 9600
            print("![300]", file=sys.stderr, end='', flush=True)"""


        time.sleep(0.1)

        # 3. Read
        if ser.in_waiting > 0:
            data = ser.read(ser.in_waiting).decode('ascii', errors='ignore')
            cleaned_data = clean_output(data)
            buffer += cleaned_data

            if cleaned_data:
                print(cleaned_data, file=sys.stderr, flush=True, end='')

                # 4. Check if we found it
            if re.search(expect_regex, buffer, re.IGNORECASE | re.MULTILINE):
                log_output(f"\n[.] Interrupt successful! Matched: '{expect_regex}'")
                return buffer
    # --- END MODIFIED LOOP ---

    log_output(f"\n[!] TIMEOUT waiting for: '{expect_regex}' after interrupt.")
    raise serial.SerialTimeoutException(f"Timeout waiting for '{expect_regex}' after interrupt")

def read_until(ser, expect_regex, timeout=30):
    buffer = ""
    start_time = time.time()

    while time.time() - start_time < timeout:
        if ser.in_waiting > 0:
            data = ser.read(ser.in_waiting).decode('ascii', errors='ignore')
            cleaned_data = clean_output(data)
            buffer += cleaned_data

            if cleaned_data:
                print(cleaned_data, file=sys.stderr, flush=True, end='')

            if buffer.rstrip().endswith('-- MORE --'):
                log_output("[.] Handling pagination ('-- MORE --')...")
                ser.write(b' ')
                ser.flush()
                buffer = ""
                continue

            if re.search(expect_regex, buffer, re.IGNORECASE | re.MULTILINE):
                log_output(f"[.] Matched: '{expect_regex}'")
                return buffer

        time.sleep(0.1)

    log_output(f"[!] TIMEOUT waiting for: '{expect_regex}'")
    raise serial.SerialTimeoutException(f"Timeout waiting for '{expect_regex}'")

def send_status(text, is_interactive=False, is_completed=False):
    """
    Sends a structured JSON status to stdout.
    """
    status_payload = json.dumps({
        "text": text,
        "interactive": is_interactive,
        "complete": is_completed,
    })
    print(f"{STATUS_FLAG}{status_payload}")
    sys.stdout.flush()

def main(json_path, com_port):
    try:

        with open(json_path, 'r') as f:
            workflow = json.load(f)
    except Exception as e:
        send_status("Fatally Failed", True) # Make errors flash
        log_output(f"!====== FAILED to load template: {e} ======!")
        sys.exit(1)

    log_output(f"*=*=*=*=*= Running workflow '{workflow['name']}' on {com_port} *=*=*=*=*=")

    try:
        ser = serial.Serial(com_port, BAUD_RATE, timeout=1)
    except Exception as e:
        send_status("Fatally Failed", True) # Make errors flash
        log_output(f"!====== FAILED to open port {com_port}: {e} ======!")
        sys.exit(1)

    try:
        for step in workflow['steps']:
            status_message = step.get("status", step['name'])
            command = step.get('command')
            interrupt_char = step.get('interrupt')
            expect_string = step.get('expect')
            timeout = step.get('timeout', 30)

            # --- PHYSICAL INTERACTION FLAG ---
            is_interactive = step.get("require_physical_interact", False)
            is_completed = step.get("is_completed", False)

            # Send the structured status
            send_status(status_message, is_interactive)

            if interrupt_char:
                interrupt_and_read_until(ser, interrupt_char, expect_string, timeout)

            elif command is None:
                if expect_string:
                    log_output(f"Waiting for prompt (expect: '{expect_string}')...")
                    read_until(ser, expect_string, timeout)

            else:
                send_command(ser, command)
                if expect_string:
                    read_until(ser, expect_string, timeout)
                else:
                    time.sleep(0.5)

    except Exception as e:
        send_status("Fatally Failed", True) # Make errors flash
        log_output(f"!====== CRITICAL ERROR: {e} ======!")
        ser.close()
        sys.exit(1)

    ser.close()
    log_output("Workflow finished successfully.")
    send_status("Successfully Finished")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        send_status("Fatally Failed", True)
        print("[SCRIPT] ERROR: Missing arguments. Usage: python workflow_runner.py <template.json> <COM_PORT>", file=sys.stderr, flush=True)
        sys.exit(1)

    json_path = sys.argv[1]
    com_port = sys.argv[2]
    main(json_path, com_port)