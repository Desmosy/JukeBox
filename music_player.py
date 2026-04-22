import paho.mqtt.client as mqtt
import pygame
import os
import sys
import time

BROKER_IP = "192.168.1.131"
BROKER_PORT = 1883
# Client ID should be unique for each person connecting
CLIENT_ID = "koshish_laptop"

# The main feed topic from the whiteboard
FRIEND_FEED      = "da_coder/feeds/cse4352"

# Map your internal music topics to sub-topics or use the main one
# For now, let's listen to the main one your friend gave you
FEED_PLAY        = "music_play"
FEED_STOP        = "music_stop"
FEED_VOTE        = "music_vote"

SONGS = {
    "Song 1": "song1.mp3",
    "Song 2": "song2.mp3",
    "Song 3": "song3.mp3",
}

def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print(f"[MQTT] Connected to Broker at {BROKER_IP}!")
        print(f"[MQTT] Using ClientID: {CLIENT_ID}")
        # Subscribe to the specific feed from the board
        client.subscribe(FRIEND_FEED)
        # Also subscribe to your own internal topics
        client.subscribe(FEED_PLAY)
        client.subscribe(FEED_STOP)
        print(f"[MQTT] Subscribed to {FRIEND_FEED}, {FEED_PLAY}, etc.")
    else:
        print(f"[MQTT] Connection failed with code {rc}")

def on_message(client, userdata, msg):
    topic = msg.topic
    payload = msg.payload.decode("utf-8", errors="replace")
    print(f"[MQTT] {topic} : {payload}")

    # If your friend sends the song name to the cse4352 topic
    if topic == FRIEND_FEED or topic == FEED_PLAY:
        play_song(client, payload)

    elif topic == FEED_STOP:
        stop_song(client)

def play_song(client, song_name):
    song_name = song_name.strip()
    # If he just sends "1", "2", or "3", we convert it to "Song X"
    if song_name in ["1", "2", "3"]:
        song_name = f"Song {song_name}"

    if song_name not in SONGS:
        print(f"  [PLAYER] Unknown song/command: \"{song_name}\"")
        return

    filename = SONGS[song_name]
    filepath = os.path.join(os.path.dirname(os.path.abspath(__file__)), filename)

    if not os.path.exists(filepath):
        print(f"  [PLAYER] File not found: {filepath}")
        return

    pygame.mixer.music.stop()
    print(f"  [PLAYER] >>> NOW PLAYING: {song_name} <<<")
    pygame.mixer.music.load(filepath)
    pygame.mixer.music.play()

def stop_song(client):
    pygame.mixer.music.stop()
    print("  [PLAYER] Music stopped")

def main():
    print("=" * 60)
    print(f"  Team 18 - Music Player (Whiteboard Config)")
    print(f"  Connecting to: {BROKER_IP}")
    print("=" * 60)

    pygame.mixer.init()
    
    # Initialize client with unique ID and latest API version
    # Note: Use CallbackAPIVersion.VERSION1 if using an older library version
    from paho.mqtt.enums import CallbackAPIVersion
    client = mqtt.Client(CallbackAPIVersion.VERSION2, client_id=CLIENT_ID)

    client.on_connect = on_connect
    client.on_message = on_message

    print(f"[MQTT] Connecting to {BROKER_IP}...")
    try:
        client.connect(BROKER_IP, BROKER_PORT, 60)
    except Exception as e:
        print(f"[MQTT] ERROR: Could not connect - {e}")
        sys.exit(1)

    try:
        client.loop_forever()
    except KeyboardInterrupt:
        print("\nShutting down...")
        pygame.mixer.music.stop()
        client.disconnect()
        pygame.quit()

if __name__ == "__main__":
    main()



