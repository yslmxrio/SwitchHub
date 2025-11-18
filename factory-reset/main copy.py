import serial
import time


def wait_for_prompt(ser, prompt="switch:", timeout=180):
    """Wait until a specific prompt appears in serial output."""
    start = time.time()
    buffer = ""
    while True:
        if time.time() - start > timeout:
            print(f"Timed out waiting for prompt: {prompt}")
            return False
        line = ser.readline().decode(errors="ignore")
        if not line:
            continue
        print(line.strip())
        buffer += line
        if prompt.lower() in buffer.lower():
            print(f"\n✅ Detected prompt: {prompt}")
            return True


def send_command(ser, cmd, wait_time=4.0, handle_more=True, confirm_prompts=True):
    """Send a command and handle '-- MORE --' pagination and confirmation prompts."""
    print(f"\n> Sending: {cmd}")
    ser.write((cmd + "\r\n").encode())
    time.sleep(wait_time)

    buffer = ""
    last_line_time = time.time()
    
    while True:
        line = ser.read(ser.in_waiting or 1).decode(errors="ignore")
        if line:
            print(line, end="", flush=True)
            buffer += line
            last_line_time = time.time()

            # Handle confirmation prompts
            if confirm_prompts and ("Are you sure you want to delete" in line or "(y/n)?" in line):
                ser.write("y\r\n".encode())
                buffer=""
                wait_for_prompt(ser, "switch:", timeout=30)
                break
                #time.sleep(3)
                #ser.write("\r\n".encode())
                print("Confirmed prompt with Y!")
            # Handle pagination
            if handle_more and ("--More--" in line or "-- MORE --" in line):
                ser.write(b" ")  # send space to continue
                print("Pressed SPACE for more output.")
                buffer=""
                time.sleep(0.5)

        # Stop reading if no new data for 3 seconds
        if time.time() - last_line_time > 5:
            break

    return buffer


def factory_reset_cisco_2960x(port="COM5", baud=9600):
    """Main automation logic for Cisco Catalyst 2960X reset."""
    print(f"🔌 Connecting to {port}...")
    ser = serial.Serial(port, baudrate=baud, timeout=2)
    time.sleep(1)

    print("\nPower OFF the switch, hold the MODE button, then power ON.")
    print("Waiting for 'switch:' prompt...")

    if not wait_for_prompt(ser, "switch:", timeout=240):
        print("Did not detect 'switch:' prompt. Exiting.")
        ser.close()
        return

    # Initialize flash
    send_command(ser, "flash_init", wait_time=0.5)

    # Wait again for switch prompt after flash init
    print("\n⏳ Waiting for 'switch:' prompt again after flash initialization...")
    if not wait_for_prompt(ser, "switch:", timeout=60):
        print("⚠️ 'switch:' prompt not detected after flash_init. Proceeding anyway.")

    # Delete config files
    send_command(ser, "delete flash:/private-config.text")
    send_command(ser, "delete flash:/config.text")
    send_command(ser, "delete flash:/vlan.dat")

    # Boot the switch
    send_command(ser, "boot", wait_time=2)

    print("\n✅ Factory reset complete. The switch should now reboot clean.")
    ser.close()


if __name__ == "__main__":
    factory_reset_cisco_2960x(port="COM5", baud=9600)
