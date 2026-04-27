import serial
import pygame
import os
import sys
import time
import threading
import queue

PORT = "COM9"
BAUD = 115200

SONGS = {
    "1": "song1.mp3",
    "2": "song2.mp3",
    "3": "song3.mp3",
}

cmd_queue = queue.Queue()

def input_thread():
    print("  Type any TM4C command and press Enter")
    print("-" * 60)
    while True:
        try:
            line = input()
            cmd_queue.put(line)
        except (EOFError, KeyboardInterrupt):
            cmd_queue.put(None)
            break

def send_cmd(ser, cmd):
    ser.write((cmd + "\r").encode("utf-8"))
    print(f"[CMD] -> {cmd}")

def main():
    print("=" * 60)
    print("  Team 18 - Music Player")
    print(f"  Listening on {PORT} at {BAUD} baud")
    print("=" * 60)

    pygame.mixer.init()

    t = threading.Thread(target=input_thread, daemon=True)
    t.start()

    while True:
        try:
            ser = serial.Serial(PORT, BAUD, timeout=1)
            print(f"[SERIAL] Opened {PORT}, waiting for TM4C...\n")

            while True:
                # Handle user input (non-blocking)
                try:
                    cmd = cmd_queue.get_nowait()
                    if cmd is None:
                        raise KeyboardInterrupt
                    # Shortcuts
                    if cmd == 'r':
                        send_cmd(ser, "music reset")
                    elif cmd == 'i':
                        send_cmd(ser, "ip")
                    elif cmd in ('1', '2', '3'):
                        send_cmd(ser, f"mqtt publish music_play {cmd}")
                    elif cmd == 'q':
                        raise KeyboardInterrupt
                    elif cmd.strip():
                        send_cmd(ser, cmd)  
                except queue.Empty:
                    pass

                # Read serial line
                raw = ser.readline()
                if not raw:
                    continue

                line = raw.decode("utf-8", errors="replace").strip()
                if line:
                    print(f"[TM4C] {line}")

                if line.startswith("PLAY:"):
                    num = line[5:].strip()
                    if num in SONGS:
                        filepath = os.path.join(os.path.dirname(os.path.abspath(__file__)), SONGS[num])
                        if os.path.exists(filepath):
                            pygame.mixer.music.stop()
                            pygame.mixer.music.load(filepath)
                            pygame.mixer.music.play()
                            print(f"  [PLAYER]: NOW PLAYING: Song {num}")
                        else:
                            print(f"  [PLAYER] File not found: {filepath}")

                elif line == "STOP":
                    pygame.mixer.music.stop()
                    print("  [PLAYER] Music stopped")

        except serial.SerialException as e:
            print(f"[SERIAL] Lost connection ({e}):  retrying in 3s...")
            time.sleep(3)
            continue

        except KeyboardInterrupt:
            print("\nShutting down...")
            break

    pygame.mixer.music.stop()
    pygame.quit()
    try:
        ser.close()
    except Exception:
        pass

if __name__ == "__main__":
    main()
