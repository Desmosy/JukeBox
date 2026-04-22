import paho.mqtt.client as mqtt
import pygame
import os
import sys
import time

BROKER_IP = "192.168.1.59"
BROKER_PORT = 1883
CLIENT_ID = "koshish_laptop"

FEED_PLAY        = "music_play"
FEED_STOP        = "music_stop"
FEED_VOTE        = "music_vote"
FEED_SHOW        = "music_show_playing"

SONGS = {
    "Song 1": "song1.mp3",
    "Song 2": "song2.mp3",
    "Song 3": "song3.mp3",
}

def on_connect(client, userdata, flags, reason_code, properties):
    if reason_code == 0:
        print(f"[MQTT] Connected to Mosquitto Broker at {BROKER_IP}!")
        print(f"[MQTT] Using ClientID: {CLIENT_ID}")
        # Subscribe to the music control topics
        client.subscribe(FEED_PLAY)
        client.subscribe(FEED_STOP)
        client.subscribe(FEED_VOTE)
        client.subscribe(FEED_SHOW)
        print(f"[MQTT] Subscribed to {FEED_PLAY}, {FEED_STOP}, {FEED_VOTE}, {FEED_SHOW}")
    else:
        print(f"[MQTT] Connection failed with code {reason_code}")

def on_message(client, userdata, msg):
    topic = msg.topic
    payload = msg.payload.decode("utf-8", errors="replace")
    print(f"[MQTT] {topic} : {payload}")

    if topic == FEED_PLAY:
        play_song(client, payload)

    elif topic == FEED_STOP:
        stop_song(client)

    elif topic == FEED_VOTE:
        print(f"  [VOTE] Received vote for song: {payload}")

    elif topic == FEED_SHOW:
        print(f"  [NOW PLAYING] {payload}")

def play_song(client, song_name):
    song_name = song_name.strip()
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
    print(f"  Team 18 - Music Player (Mosquitto Broker)")
    print(f"  Connecting to: {BROKER_IP}")
    print("=" * 60)

    pygame.mixer.init()
    
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



