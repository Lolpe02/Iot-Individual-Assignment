#logger.py
import serial, sys

port = sys.argv[1]
baud = int(sys.argv[2])
max_lines = int(sys.argv[3]) if len(sys.argv) > 3 else 100

def is_timing_row(line):
    parts = line.split(",")
    if len(parts) != 4:
        return False
    try:
        [int(p) for p in parts]
        return True
    except ValueError:
        return False

with serial.Serial(port, baud, timeout=1) as ser, \
    open("tools/data.csv", "w") as f:
    print(f"Logging... stop a {max_lines} righe timing (Ctrl+C per fermare prima)")
    # Aspetta il segnale WAITING dall'ESP32
    print("In attesa dell'ESP32...")
    while True:
        line = ser.readline().decode("utf-8", errors="ignore").strip()
        if "Starting up..." in line:
            break
    
    # Manda il via
    ser.write(b'\n')
    print("Avviato — logging in corso...")
    count = 0
    while count < max_lines:
        try:
            line = ser.readline().decode("utf-8", errors="ignore").strip()
            if not line:
                continue
            if "[MQTT]" in line or ">" in line:
                print(line)
            f.write(line + "\n")
            f.flush()
            if is_timing_row(line):
                count += 1
        except KeyboardInterrupt:
            break
    print(f"Fatto — {count} righe timing salvate.")

# python tools/log_timings_serial.py COM3 115200