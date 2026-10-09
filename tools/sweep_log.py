import serial, time, datetime

PORT = "COM7"
BAUD = 115200
OUT = "sweep_log.txt"

while True:
    try:
        s = serial.Serial(PORT, BAUD, timeout=1)
        print("conectado")
        with open(OUT, "a", encoding="utf-8") as f:
            while True:
                line = s.readline().decode("utf-8", errors="replace").rstrip()
                if line:
                    msg = f"{datetime.datetime.now():%H:%M:%S} {line}"
                    print(msg)
                    f.write(msg + "\n")
                    f.flush()
    except serial.SerialException:
        print("porta indisponivel, tentando de novo...")
        time.sleep(2)
