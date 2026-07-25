import socket
import time
import subprocess
import argparse
import select
import os
import sys

# ==================================================
# NETWORK & DEVICE CONFIGURATION
# ==================================================
CONTROL_PORT = 8888

# Map your physical IP addresses to their logical 2-digit Device IDs
DEVICES = {
    "192.168.1.108": {"id": "01", "name": "Ultrasonic Motion Alarm 01"},
    "192.168.1.138": {"id": "02", "name": "Bed Scale Alarm 02"},
    "192.168.1.145": {"id": "03", "name": "Bed Scale Alarm 03 (bench test)"}
}

# ==================================================
# TIMER & AUDIO CONFIGURATION
# ==================================================
POLL_INTERVAL = 10.0  

# Command used to play audio. 
# Termux Default: ["termux-media-player", "play"]
# Linux / RPi   : ["mpv"] or ["aplay"]
# Windows       : ["ffplay", "-nodisp", "-autoexit"]
PLAYER_CMD = ["termux-media-player", "play"]

# ==================================================
# COMMAND LINE ARGUMENT PARSING
# ==================================================
parser = argparse.ArgumentParser(description="WAV-based Patient Supervision Engine.")
parser.add_argument("--en", action="store_true", help="Enable English wav alerts")
parser.add_argument("--th", action="store_true", help="Enable Thai wav alerts")
args = parser.parse_args()

active_languages = []
if args.en: active_languages.append("en")
if args.th: active_languages.append("th")

if not active_languages:
    print("[*] No language flag provided (--en or --th). Defaulting to English (en/...).")
    active_languages = ["en"]

# Initialize tracking registers for health monitoring
device_states = {}
for ip, dev in DEVICES.items():
    device_states[ip] = {
        "id": dev["id"],
        "name": dev["name"],
        "last_seen": time.time(),
        "online": True
    }

# Setup unified UDP Socket for sending and receiving on port 8888
try:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("0.0.0.0", CONTROL_PORT))
    sock.setblocking(False)
except Exception as e:
    print(f"[-] Failed to bind socket to port {CONTROL_PORT}: {e}")
    sys.exit(1)

print(f"[*] Starting WAV-based Supervision Engine...")
for ip, dev in DEVICES.items():
    print(f"[*] Tracking {dev['name']} ({dev['id']}) at {ip}")
print(f"[*] Active Audio Folders : {', '.join([lang for lang in active_languages])}/")
print(f"[*] Listening for Unified UDP events on port: {CONTROL_PORT}...\n")

def get_timestamp():
    """Generates a clean timestamp for terminal logging."""
    return time.strftime("[%Y-%m-%d %H:%M:%S]")

def play_audio(msg_code):
    """
    Loops through active languages, formats the filepath (e.g. en/0101.wav),
    and executes the media player synchronously.
    """
    for lang in active_languages:
        filepath = os.path.join(lang, f"{msg_code}.wav")
        
        if os.path.exists(filepath):
            print(f"{get_timestamp()} [AUDIO] Playing {filepath}...")
            # subprocess.run blocks execution until the wav finishes playing, 
            # naturally queueing multiple languages back-to-back.
            subprocess.run(PLAYER_CMD + [filepath])
        else:
            print(f"{get_timestamp()} [AUDIO-ERR] File not found: {filepath}")

# ==================================================
# MAIN ASYNCHRONOUS EVENT LOOP
# ==================================================
last_poll_time = time.time()

while True:
    current_time = time.time()

    # 1. READ NETWORK BUFFER (Zero-Lag Parsing)
    # select() waits up to 0.5 seconds for incoming packets, yielding CPU if idle.
    readable, _, _ = select.select([sock], [], [], 0.5)
    
    for s in readable:
        data, addr = s.recvfrom(1024)
        ip = addr[0]
        
        try:
            payload = data.decode('utf-8').strip()
        except UnicodeDecodeError:
            continue

        # --- HEALTH TRACKING UPDATE ---
        if ip in device_states:
            device_states[ip]["last_seen"] = current_time
            if not device_states[ip]["online"]:
                print(f"{get_timestamp()} [NETWORK RECOVERED] {device_states[ip]['name']} is back online.")
                device_states[ip]["online"] = True

        # --- EVENT: NEW MESSAGE FORMAT TRIGGERS ---
        if payload.startswith("msg "):
            msg_code = payload.split(" ")[1] # Extracts "0101"
            print(f"{get_timestamp()} [!!! CRITICAL ALARM !!!] Trigger received: {msg_code} from {ip}")
            
            play_audio(msg_code)
            
            # Instantly clear the distributed matrix latch on the sensor
            sock.sendto(b"status 0", (ip, CONTROL_PORT))
            print(f"{get_timestamp()} [+] Dispatched 'status 0' clear command to {ip}")

        # --- EVENT: POLLED MATRIX BACKUP ---
        # If the UDP broadcast dropped, but the sensor latched to Status 2,
        # our polling engine will catch it here.
        elif payload.startswith("OK status 2"):
            sensor_id = device_states.get(ip, {}).get("id", "00")
            msg_code = f"{sensor_id}01" # Default to message '01' for that sensor
            
            print(f"{get_timestamp()} [!!! CRITICAL ALARM !!!] Backup poll detected latched alert on {ip}")
            
            play_audio(msg_code)
            sock.sendto(b"status 0", (ip, CONTROL_PORT))
            print(f"{get_timestamp()} [+] Dispatched 'status 0' clear command to {ip}")

        # --- EVENT: SOFT ALERTS (Informational text) ---
        elif payload.startswith("alert "):
            alert_text = payload[6:]
            print(f"{get_timestamp()} [* SOFT ALERT *] {ip}: {alert_text}")

    # 2. PERIODIC HEALTH POLL ENGINE (Every 10 Seconds)
    if current_time - last_poll_time >= POLL_INTERVAL:
        last_poll_time = current_time
        
        for ip, state in device_states.items():
            # Send the matrix status query
            sock.sendto(b"status ?", (ip, CONTROL_PORT))
            
            # Check if this specific device has ghosted us
            time_since_last_seen = current_time - state["last_seen"]
            if time_since_last_seen > (POLL_INTERVAL * 3):
                if state["online"]:
                    print(f"{get_timestamp()} [CRITICAL] SUPERVISION LOST ON {state['name'].upper()}!")
                    state["online"] = False
