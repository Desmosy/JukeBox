// Music Voting Library
// Team 18 - Web Server Team 2

//-----------------------------------------------------------------------------
// Hardware Target
//-----------------------------------------------------------------------------

// Target Platform: EK-TM4C123GXL w/ ENC28J60
// Target uC:       TM4C123GH6PM
// System Clock:    40 MHz

// No external hardware needed for music.
// The TM4C manages voting and publishes the winner via MQTT.
// A Python script on the laptop subscribes to music_play and
// plays the actual audio files.

//-----------------------------------------------------------------------------
// Device includes, defines, and assembler directives
//-----------------------------------------------------------------------------

#ifndef MUSIC_H_
#define MUSIC_H_

#include <stdint.h>
#include <stdbool.h>

// Number of songs available for voting
#define NUM_SONGS 3

// Maximum length of a song name
#define MAX_SONG_NAME 32

// Voting period in seconds (timer starts on first vote)
#define VOTE_PERIOD_S 30

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

void initMusic();

// Song name management (set remotely via MQTT)
void setSongName(uint8_t songIndex, const char *name);
const char* getSongName(uint8_t songIndex);

// Voting
void castVote(uint8_t songNumber);
uint8_t getVoteCount(uint8_t songIndex);
void resetVotes(bool publishMqttMsg);
uint8_t tallyVotes();
bool isVotingLocked();
bool isVotingActive();
uint8_t getWinner();

// Status
void displayMusicStatus();

// Timer callback
void voteTimerCallback();

#endif
