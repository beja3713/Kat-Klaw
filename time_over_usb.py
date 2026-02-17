import serial
import time

ser = serial.Serial('/dev/ttyACM0', 115200, timeout=1)
time.sleep(1)  # wait for ESP32 to reset

while True:
    send_time = int(time.time() * 1000)  # milliseconds
    ping_msg = f"PING:{send_time}"
    ser.write((ping_msg + "\n").encode())

    line = ser.readline().decode().strip()
    if line.startswith("PING:"):
        recv_time = int(time.time() * 1000)
        sent_time = int(line.split(":")[1])
        rtt = recv_time - sent_time
        print(f"Round-trip latency: {rtt} ms")
    time.sleep(1)
