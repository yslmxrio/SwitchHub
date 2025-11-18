import serial
import time


def detect_vendor(output):
    if "Cisco" in output:
        return "cisco"
    elif "c2960x" in output:
        return "cisco"
    elif "Xmodem file system is available." in output:
        return "cisco"
    elif "ProCurve" in output:
        return "hp"
    elif "Aruba" in output:
        return "aruba"
    elif "Ubiquit" in output or "UniFi" in output:
        return "unifi"
    return None

def read_serial_lines(port="COM5", baudrate = 9600):


    buffer = ""
    # Continuously read and print all output of serial connection
    try:
        with serial.Serial(port='COM5',
                baudrate=9600,
                bytesize=8,
                parity='N',
                stopbits=1,
                timeout=1,
                xonxoff=False,  # disable software flow control
                rtscts=False,   # disable hardware flow control   
                dsrdtr=False    # disable DSR/DTR flow control
        ) as serialConn:
            print(f"Connected to Serial COM Port {port} at baudrate {baudrate}.")
            print("Reading lines.... Press CTRL + C to stop reading. \n")

            isFlashInitialized  = False
            isInitializingFlash  = False

            commandsList = [
                "delete flash:/private-config.text",
                "delete flash:/config.text",
                "boot",
            ]

            currentCommand = "TEST"

            while True:
                data = serialConn.readline().decode(errors="ignore").rstrip()
                if data:
                    print(data)
                    buffer += data
                    if "Are you sure you want to delete" in buffer:
                        print("\nDETECTED DELETE CONFIRM PROMPT! SENDING Y ...")
                        serialConn.write(b"y\r\n")
                        buffer = ""
                    if "switch:" in buffer:
                        if not isFlashInitialized and not isInitializingFlash:
                            serialConn.write(b"flash_init\r\n")
                            isInitializingFlash = True
                            buffer= ""
                        elif isFlashInitialized and not isInitializingFlash:
                            if commandsList:
                                currentCommand = commandsList.pop(0)
                                print(f"SENDING COMMAND: {currentCommand}")
                                serialConn.write(f"{currentCommand}\r\n".encode())
                                time.sleep(2)
                            buffer = ""
                    if "-- MORE --" in buffer and isInitializingFlash:

                        serialConn.write((b"\r\n"))
                        print("PRESSED ENTER FOR -- MORE")
                        buffer = ""
                    elif "..done Initializing Flash." in buffer and isInitializingFlash:
                        isFlashInitialized = True
                        isInitializingFlash = False

                    #if "...done Initializing Flash." in buffer and "switch:" in buffer:
                    #    isFlashInitialized = True
                    #    print("WAITING FOR PROMPT, FLASH INITED")
                    #if currentCommand and "switch:" in buffer:
                     #    currentCommand = None
    # Serial Error handling
    except serial.SerialException as e:
        print(f"ERR: Serial Error: {e}")
    except KeyboardInterrupt:
        serialConn.close()
        print(f"Closed Serial Connectin {serialConn.port}")
        print("\nStopped by User.")

def main():
    read_serial_lines()
            

            

if __name__ == "__main__":
    main()
