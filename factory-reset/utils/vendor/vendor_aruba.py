        with serial.Serial(port, baudrate, timeout=1) as ser:
            print(f"Connected to Serial COM Port {port} at baudrate {baudrate}.")
            print("Reading lines.... Press CTRL + C to stop reading. \n")

            isFlashInitialized: bool = False
            isInitializingFlash: bool = False

            while True:
                data = ser.readline().decode(errors="ignore")
                if data:
                    print(data)
                    buffer += data
                    if "Are you sure you want to delete" in buffer:
                        print("\nDETECTED DELETE CONFIRM PROMPT! SENDING Y ...")
                        ser.write(b"y\r\n")
                        time.sleep(1)
                        buffer = ""
                    if "switch:" in buffer:
                        if not isFlashInitialized and not isInitializingFlash:
                            isInitializingFlash = True
                            print("\nFlash is NOT initialized, initializing now ...")
                            ser.write("flash_init\r\n".encode())
                            time.sleep(1)
            
                        elif isFlashInitialized and not isInitializingFlash:
                            if not currentCommand and commandsList:
                                currentCommand = commandsList.pop(0)
                                print(f"SENDING COMMAND: {currentCommand}")
                                ser.write(f"{currentCommand}\r\n".encode())
                                time.sleep(2.5)
                            buffer = ""
                    if "-- MORE --" in buffer and isInitializingFlash:
                        isFlashInitialized = True
                        isInitializingFlash = False
                        ser.write((b"\r\n"))
                        print("PRESSED ENTER")
                        buffer = ""

                    #if "...done Initializing Flash." in buffer and "switch:" in buffer:
                    #    isFlashInitialized = True
                    #    print("WAITING FOR PROMPT, FLASH INITED")
                    #if currentCommand and "switch:" in buffer:
                     #    currentCommand = None