"""
Music Player Script for Team 18
================================
This script runs on your LAPTOP (not the TM4C).
It subscribes to MQTT topics from the TM4C and plays audio files
when the voting winner is announced.

Setup:
  1. Install dependencies:
       pip install paho-mqtt pygame
  2. Place 3 music files in the same folder as this script:
       song1.mp3, song2.mp3, song3.mp3
  3. Run this script:
       python music_player.py

How it works:
  - Subscribes to "music_play" and "music_stop"
  - When music_play arrives, it matches the song name and plays the file
  - Publishes "music_show_playing" back to the TM4C so it knows what's playing
  - When music_stop arrives or the song ends, it stops and publishes idle status
"""

import paho.mqtt.client as mqtt
import pygame
import os
import sys
import time

# ============================================================================
#  CONFIGURATION - Change these to match your setup
# ============================================================================

BROKER_IP = "192.168.1.1"       # Your laptop's Ethernet IP (where Mosquitto runs)
BROKER_PORT = 1883

# Song file mapping: song name -> filename
# The song names should match what the TM4C publishes in music_play
# Default names are "Song 1", "Song 2", "Song 3" unless changed via MQTT
SONGS = {
    "Song 1": "song1.mp3",
    "Song 2": "song2.mp3",
    "Song 3": "song3.mp3",
}

# ============================================================================
#  MQTT Callbacks
# ============================================================================

def on_connect(client, userdata, flags, rc):
    """Called when connected to the MQTT broker."""
    if rc == 0:
        print("[MQTT] Connected to broker!")
        # Subscribe to topics from the TM4C
        client.subscribe("music_play")
        client.subscribe("music_stop")
        client.subscribe("music_vote")
        client.subscribe("music_set_name_1")
        client.subscribe("music_set_name_2")
        client.subscribe("music_set_name_3")
        print("[MQTT] Subscribed to music topics")
    else:
        print(f"[MQTT] Connection failed with code {rc}")

def on_message(client, userdata, msg):
    """Called when an MQTT message is received."""
    topic = msg.topic
    payload = msg.payload.decode("utf-8", errors="replace")
    print(f"[MQTT] {topic} : {payload}")

    if topic == "music_play":
        play_song(client, payload)

    elif topic == "music_stop":
        stop_song(client)

    elif topic == "music_vote":
        print(f"  [VOTE] Someone voted for Song {payload}")

    elif topic.startswith("music_set_name_"):
        # Update the song name mapping dynamically
        song_num = topic[-1]  # "1", "2", or "3"
        old_key = f"Song {song_num}"
        # Find and remove old mapping if it exists
        filename = SONGS.pop(old_key, f"song{song_num}.mp3")
        # Add new mapping
        SONGS[payload] = filename
        print(f"  [CONFIG] Song {song_num} renamed to \"{payload}\" -> {filename}")

# ============================================================================
#  Music Playback
# ============================================================================

def play_song(client, song_name):
    """Play the audio file associated with the given song name."""
    song_name = song_name.strip()

    if song_name not in SONGS:
        print(f"  [PLAYER] Unknown song: \"{song_name}\"")
        print(f"  [PLAYER] Known songs: {list(SONGS.keys())}")
        # Try to publish back that we don't know this song
        client.publish("music_show_playing", f"Unknown: {song_name}")
        return

    filename = SONGS[song_name]
    filepath = os.path.join(os.path.dirname(os.path.abspath(__file__)), filename)

    if not os.path.exists(filepath):
        print(f"  [PLAYER] File not found: {filepath}")
        print(f"  [PLAYER] Please place {filename} in the script directory!")
        client.publish("music_show_playing", f"File missing: {filename}")
        return

    # Stop any currently playing music
    pygame.mixer.music.stop()

    # Load and play the song
    print(f"  [PLAYER] >>> NOW PLAYING: {song_name} ({filename}) <<<")
    pygame.mixer.music.load(filepath)
    pygame.mixer.music.play()

    # Tell the TM4C what's playing
    client.publish("music_show_playing", f"playing:{song_name}")

def stop_song(client):
    """Stop the currently playing music."""
    pygame.mixer.music.stop()
    print("  [PLAYER] Music stopped")
    client.publish("music_show_playing", "idle")

# ============================================================================
#  Main
# ============================================================================

def main():
    print("=" * 50)
    print("  Team 18 - Music Player")
    print("  Listening for MQTT commands from TM4C...")
    print("=" * 50)
    print()

    # Initialize pygame mixer for audio playback
    pygame.mixer.init()
    print("[AUDIO] Pygame mixer initialized")

    # Check if song files exist
    script_dir = os.path.dirname(os.path.abspath(__file__))
    for name, filename in SONGS.items():
        filepath = os.path.join(script_dir, filename)
        if os.path.exists(filepath):
            print(f"  [OK]    {name} -> {filename}")
        else:
            print(f"  [MISS]  {name} -> {filename}  (file not found!)")
    print()

    # Connect to MQTT broker
    client = mqtt.Client()
    client.on_connect = on_connect
    client.on_message = on_message

    print(f"[MQTT] Connecting to {BROKER_IP}:{BROKER_PORT}...")
    try:
        client.connect(BROKER_IP, BROKER_PORT, 60)
    except Exception as e:
        print(f"[MQTT] ERROR: Could not connect - {e}")
        print(f"[MQTT] Make sure Mosquitto is running on {BROKER_IP}")
        sys.exit(1)

    # Run forever, processing MQTT messages
    print("[MQTT] Waiting for messages... (Ctrl+C to quit)\n")
    try:
        client.loop_forever()
    except KeyboardInterrupt:
        print("\n[MQTT] Shutting down...")
        pygame.mixer.music.stop()
        client.disconnect()
        pygame.quit()

if __name__ == "__main__":
    main()
