import serial
import time
import sys

# --- Configuration ---
# !! Change this to your serial port !!
SERIAL_PORT = "COM5"  # Windows example
# SERIAL_PORT = "/dev/ttyUSB0"  # Linux example
BAUD_RATE = 9600
# ---------------------


def send_command(ser, cmd):
    """
    Sends a command string to the switch with the required 
    carriage return.
    """
    print(f"\n[SCRIPT] >>> Sending: {cmd}")
    # Cisco console commands expect a carriage return (\r), not a newline (\n)
    ser.write(cmd.encode('ascii') + b'\r')
    ser.flush()


def main():
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
    except serial.SerialException as e:
        print(f"Error: Could not open port {SERIAL_PORT}.")
        print(f"Details: {e}")
        print("Please check your port, permissions, and ensure no other program is using it.")
        return

    print(f"Successfully connected to {SERIAL_PORT}. Waiting for switch output...")
    print("="*60)
    print(">>> Please physically press the MODE button on the switch now <<<")
    print(">>> to interrupt the boot process and get the 'switch:' prompt. <<<")
    print("="*60)

    buffer = ""
    # This state machine is the key to success
    state = "WAIT_FOR_PROMPT"

    WAIT_TIME_TO_BOOT = 150
    START_TIME_BOOT = None
    IS_BOOTING = False
    try:
        while True:
            # Read any data in the serial buffer, or wait 1s for 1 byte
            data = ser.read(ser.in_waiting or 1).decode('ascii', errors='ignore')

            if not data:
                continue  # No data read (timeout), loop again

            # --- Real-time Output ---
            # Print switch output to console immediately
            sys.stdout.write(data)
            sys.stdout.flush()

            buffer += data
            #Keep rolling buffer of the last 1024 characters
            if len(buffer) > 1024:
                buffer = buffer[-1024:]

            # 1. --- Handle Pagination globally ---
            if buffer.rstrip().endswith('-- MORE --'):
                print("\n[SCRIPT] >>> Handling pagination...")
                ser.write(b' ')  # Send a space (not a command)
                ser.flush()
                buffer = ""  # Clear buffer to avoid re-triggering

            # --- State Machine Logic ---
            # Wait for the first 'switch:' prompt
            elif state == "WAIT_FOR_PROMPT" and "switch:" in buffer:
                print("\n[SCRIPT] 'switch:' prompt detected.")
                send_command(ser, "flash_init")
                buffer = ""
                state = "WAIT_FOR_FLASH_INIT_DONE"

            # State 2: 'flash_init' runs and returns to 'switch:', execute delete config.text
            elif state == "WAIT_FOR_FLASH_INIT_DONE" and "switch:" in buffer:
                print("\n[SCRIPT] 'flash_init' complete.")
                print("\n[SCRIPT] Sending 'delete flash:/config.text'")
                send_command(ser, "delete flash:/config.text")
                buffer = ""
                state = "WAIT_FOR_CONFIG_DELETE"

            # State 3: Waiting for 'delete config.text (y/n)', send y
            elif state == "WAIT_FOR_CONFIG_DELETE" and "(y/n)" in buffer.lower():
                print("\n[SCRIPT] Confirm delete 'config.text' (y/n) detected.")
                send_command(ser, "y")
                buffer = ""
                state = "WAIT_FOR_CONFIG_DELETE_DONE"

            # State 4 Wait for switch: again, send delete private-config
            elif state == "WAIT_FOR_CONFIG_DELETE_DONE" and "switch:" in buffer.lower():
                print("\n[SCRIPT] Sending 'delete flash:/private-config.text'")
                send_command(ser, "delete flash:/private-config.text")
                buffer = ""
                state = "WAIT_FOR_PRIVATE_CONFIG_DELETE"
            
            # State 5 Confirm private-config deleteion, send y
            elif state == "WAIT_FOR_PRIVATE_CONFIG_DELETE" and "(y/n)" in buffer.lower():
                print("\n[SCRIPT] Confirm delete 'private-config.text' (y/n) detected.")
                send_command(ser, "y")
                buffer=""
                state = "WAIT_FOR_PRIVATE_CONFIG_DELETE_DONE"

            # State 4: 'delete' finishes and returns to 'switch:', send delete vlan
            elif state == "WAIT_FOR_PRIVATE_CONFIG_DELETE_DONE" and "switch:" in buffer:
                send_command(ser, "delete flash:/vlan.dat")
                buffer = ""
                state = "WAIT_FOR_VLAN_CONFIRM"

            # State 5: Waiting for 'delete vlan.dat (y/n)'
            elif state == "WAIT_FOR_VLAN_CONFIRM" and "(y/n)" in buffer.lower():
                print("\n[SCRIPT] 'vlan.dat' (y/n) detected.")
                send_command(ser, "y")
                buffer = ""
                state = "WAIT_FOR_VLAN_DELETE_DONE"

            # State 6: 'delete vlan' finishes and returns to 'switch:'
            elif state == "WAIT_FOR_VLAN_DELETE_DONE" and "switch:" in buffer:
                print("\n[SCRIPT] 'vlan.dat' deleted.")
                send_command(ser, "boot")
                buffer = ""
                state = "BOOTING"

            # Start boot timer
            elif state == "BOOTING" and START_TIME_BOOT is None:
                START_TIME_BOOT = time.time()
                print(f"[SCRIPT] SET START_TIME_BOOT TO {START_TIME_BOOT}")
                IS_BOOTING = True

            # Switch is booting, send Enter to get initial config dialog
            elif state == "BOOTING" and not("Would you like to enter the initial configuration dialog?".lower() in buffer.lower()):
                if IS_BOOTING and (time.time() - START_TIME_BOOT > WAIT_TIME_TO_BOOT):
                    send_command(ser, "\r")
                    print("[SCRIPT] SENT ENTER COMMAND WHILE WAITING FOR INITIAL CONFIG PROMPT.")
                    time.sleep(5)

            elif state == "BOOTING" and ("Would you like to enter the initial configuration dialog? [yes/no]: ".lower() in buffer.lower()):
                IS_BOOTING = False
                print("\n" + "="*60)
                print("="*60)
                print("[SCRIPT] Negating initial config dialog with NO.")
                send_command(ser, "no")
                buffer = ""
                state = "NEGATE_INIT_CONFIG_DIALOG"
            
            # ... (all states before this are correct) ...

            elif state == "NEGATE_INIT_CONFIG_DIALOG" and "switch>".lower() in buffer.lower():
                send_command(ser, "enable")
                buffer = ""
                state = "NEGATE_INIT_CONFIG_DIALOG_DONE"

            # --- !!! THIS IS THE CORRECTED MULTI-LINE TCL SECTION !!! ---

            # State: We are at 'switch#', send 'tclsh' to enter Tcl mode
            elif state == "NEGATE_INIT_CONFIG_DIALOG_DONE" and "switch#".lower() in buffer.lower():
                print("[SCRIPT] Entering Tcl shell...")
                send_command(ser, "tclsh")
                buffer = ""
                state = "WAIT_FOR_TCL_PROMPT"

            # New State: Wait for the Tcl prompt "Switch(tcl)#"
            elif state == "WAIT_FOR_TCL_PROMPT" and "switch(tcl)#".lower() in buffer.lower():
                print("[SCRIPT] Tcl prompt detected. Sending script line 1...")
                # 1. Send first line of the loop, ending with a Tcl continuation char \
                #    (Python needs '\\' to send a single '\')
                send_command(ser, "foreach f [glob flash:/*] { \\")
                buffer = ""
                state = "WAIT_FOR_TCL_CONT_1"

            # New State: Wait for the Tcl continuation prompt '+'
            elif state == "WAIT_FOR_TCL_CONT_1" and "+>" in buffer:
                print("[SCRIPT] Tcl cont... Sending script line 2...")
                # 2. Send the variable definitions
                send_command(ser, "set fname [file tail $f]; set ext [file extension $f]; \\")
                buffer = ""
                state = "WAIT_FOR_TCL_CONT_2"
            
            # Wait for the Tcl continuation prompt '+'
            elif state == "WAIT_FOR_TCL_CONT_2" and "+>" in buffer:
                print("[SCRIPT] Tcl cont... Sending script line 3 (if block)...")
                # Send the 'if' statement block
                #    Note: We must escape the quotes " inside the Python string
                send_command(ser, "if {[string match \"c*universal*\" $fname] || [string match \"cat*universal*\" $fname] || [string match \".E*\" $ext] || ([string match \"package*\" $fname] && [string match \".conf\" $ext])} { \\")
                buffer = ""
                state = "WAIT_FOR_TCL_CONT_3"

            # Wait for the Tcl continuation prompt '+'
            elif state == "WAIT_FOR_TCL_CONT_3" and "+>" in buffer:
                print("[SCRIPT] Tcl cont... Sending script line 4 (puts skip)...")
                # 4. Send the 'puts skip' block
                send_command(ser, "puts \"Skipping possible IOS image: $fname\"; continue }; \\")
                buffer = ""
                state = "WAIT_FOR_TCL_CONT_4"

            # New State: Wait for the Tcl continuation prompt '+'
            elif state == "WAIT_FOR_TCL_CONT_4" and "+>" in buffer:
                print("[SCRIPT] Tcl cont... Sending script line 5 (puts delete)...")
                # Send the final line *WITHOUT* a continuation char.
                #    The \r from send_command will execute the entire command.
                send_command(ser, "puts \"Deleting file/dir: $fname\"; exec delete /force /recursive $f }")
                buffer = ""
                state = "WAIT_FOR_TCL_LOOP_DONE"

            # Wait for the loop to finish and return to "Switch(tcl)#"
            elif state == "WAIT_FOR_TCL_LOOP_DONE" and "switch(tcl)#".lower() in buffer.lower():
                print("[SCRIPT] Tcl loop finished. Exiting Tcl shell...")
                # 6. Send tclquit
                send_command(ser, "tclquit")
                buffer = ""
                state = "WAIT_FOR_TCL_SCRIPT_DONE"
            
            # Final State: Wait for tclquit to finish and return to "Switch#"
            elif state == "WAIT_FOR_TCL_SCRIPT_DONE" and "switch#".lower() in buffer.lower():
                print("\n"+"="*60)
                print("[SCRIPT] --- Factory Reset and Cleanup complete! ---")
                print("="*60)
                state = "DONE" # DONE FINAL STATE
                buffer = ""
                break # Exit the while loop

    except KeyboardInterrupt:
        print("\n[SCRIPT] --- Script interrupted by user. ---")
    except Exception as e:
        print(f"\n[SCRIPT] --- An unexpected error occurred: {e} ---")
    finally:
        if ser.is_open:
            ser.close()
            print("[SCRIPT] Serial port closed.")


if __name__ == "__main__":
    main()