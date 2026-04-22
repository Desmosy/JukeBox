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

#include <stdio.h>
#include <string.h>
#include "music.h"
#include "mqtt.h"
#include "uart0.h"
#include "timer.h"

// ------------------------------------------------------------------------------
//  Globals
// ------------------------------------------------------------------------------

// Song names (configurable via MQTT with music_set_name_1/2/3)
char songNames[NUM_SONGS][MAX_SONG_NAME] = {
    "Song 1",
    "Song 2",
    "Song 3"
};

// Vote counts for each song
uint8_t votes[NUM_SONGS] = { 0, 0, 0 };

// Total number of votes cast this round
uint16_t totalVotes = 0;

// Whether the voting timer is active
bool votingActive = false;

// Whether voting is locked (timer expired, waiting for reset)
bool votingLocked = false;

// Winning song number from last round (1-3, or 0 if none)
uint8_t lastWinner = 0;

// ------------------------------------------------------------------------------
//  Subroutines
// ------------------------------------------------------------------------------

// Initialize the music voting system (no hardware needed)
void initMusic()
{
    putsUart0("Music: Voting system ready\n");
}

// Set a song name (index 0-2). Called when MQTT topic music_set_name_X arrives.
void setSongName(uint8_t songIndex, const char *name)
{
    if (songIndex >= NUM_SONGS) return;
    uint8_t i;
    for (i = 0; i < MAX_SONG_NAME - 1 && name[i] != '\0'; i++)
        songNames[songIndex][i] = name[i];
    songNames[songIndex][i] = '\0';

    char str[60];
    snprintf(str, sizeof(str), "Music: Song %d set to \"%s\"\n", songIndex + 1, songNames[songIndex]);
    putsUart0(str);
}

// Get a song name by index (0-2)
const char* getSongName(uint8_t songIndex)
{
    if (songIndex >= NUM_SONGS) return "Unknown";
    return songNames[songIndex];
}

// Cast a vote for a song (songNumber is 1, 2, or 3)
void castVote(uint8_t songNumber)
{
    if (votingLocked)
    {
        putsUart0("Music: Voting is LOCKED. Type 'music reset' to start new round.\n");
        return;
    }

    if (songNumber < 1 || songNumber > NUM_SONGS)
    {
        putsUart0("Music: Invalid vote (use 1, 2, or 3)\n");
        return;
    }

    votes[songNumber - 1]++;
    totalVotes++;

    char str[60];
    snprintf(str, sizeof(str), "Music: Vote #%d for Song %d (%s)\n",
             totalVotes, songNumber, songNames[songNumber - 1]);
    putsUart0(str);

    // Publish the vote over MQTT so the network can see it
    char payload[4];
    snprintf(payload, sizeof(payload), "%d", songNumber);
    publishMqtt("da_coder/feeds/cse4352", payload);

    // If this is the first vote of the round, start the 30-second voting timer
    if (!votingActive)
    {
        votingActive = true;
        startOneshotTimer(voteTimerCallback, VOTE_PERIOD_S);
        putsUart0("Music: Voting open! 30 seconds to vote...\n");
    }
}

// Get vote count for a song (index 0-2)
uint8_t getVoteCount(uint8_t songIndex)
{
    if (songIndex >= NUM_SONGS) return 0;
    return votes[songIndex];
}

// Reset all votes for a new round (unlocks voting)
void resetVotes()
{
    uint8_t i;
    for (i = 0; i < NUM_SONGS; i++)
        votes[i] = 0;
    totalVotes = 0;
    votingActive = false;
    votingLocked = false;
    lastWinner = 0;

    // Publish music_stop so the Python script stops playing
    publishMqtt("music_stop", "");
}

// Tally votes and return the winning song number (1-3), or 0 if no votes
uint8_t tallyVotes()
{
    if (totalVotes == 0)
        return 0;

    uint8_t winner = 0;
    uint8_t maxVotes = 0;
    uint8_t i;

    for (i = 0; i < NUM_SONGS; i++)
    {
        if (votes[i] > maxVotes)
        {
            maxVotes = votes[i];
            winner = i + 1;   // Return 1-based song number
        }
    }

    return winner;
}

// Called automatically when the 30-second voting timer expires.
// Tallies votes, announces the winner on UART, and publishes
// music_play so the laptop Python script picks it up and plays audio.
void voteTimerCallback()
{
    char str[80];

    putsUart0("\n========== VOTING CLOSED ==========\n");

    // Display results
    uint8_t i;
    for (i = 0; i < NUM_SONGS; i++)
    {
        snprintf(str, sizeof(str), "  Song %d (%s): %d votes\n",
                 i + 1, songNames[i], votes[i]);
        putsUart0(str);
    }

    uint8_t winner = tallyVotes();

    if (winner == 0)
    {
        putsUart0("Music: No votes received.\n");
        resetVotes();
        return;
    }

    snprintf(str, sizeof(str), "  >> WINNER: Song %d (%s)!\n", winner, songNames[winner - 1]);
    putsUart0(str);
    putsUart0("===================================\n");

    // Publish the winner over MQTT
    // The Python script on the laptop is subscribed to music_play
    // and will play the corresponding audio file
    publishMqtt("music_play", (char*)songNames[winner - 1]);

    // Lock voting until manual reset
    votingLocked = true;
    votingActive = false;
    lastWinner = winner;

    putsUart0("Music: Voting LOCKED. Type 'music reset' for new round.\n");
}

// Check if voting is currently locked
bool isVotingLocked()
{
    return votingLocked;
}

// Check if voting timer is running
bool isVotingActive()
{
    return votingActive;
}

// Get the last winner (1-3) or 0 if none
uint8_t getWinner()
{
    return lastWinner;
}

// Print the current music system status to UART
void displayMusicStatus()
{
    char str[80];

    putsUart0("\n--- Music Voting Status ---\n");

    uint8_t i;
    for (i = 0; i < NUM_SONGS; i++)
    {
        snprintf(str, sizeof(str), "  Song %d: %-20s  Votes: %d\n",
                 i + 1, songNames[i], votes[i]);
        putsUart0(str);
    }

    snprintf(str, sizeof(str), "  Total votes: %d\n", totalVotes);
    putsUart0(str);

    if (votingLocked)
    {
        putsUart0("  Voting: LOCKED (type 'music reset')\n");
        char w[60];
        snprintf(w, sizeof(w), "  Winner: Song %d (%s)\n", lastWinner, songNames[lastWinner - 1]);
        putsUart0(w);
    }
    else if (votingActive)
        putsUart0("  Voting: ACTIVE (timer running)\n");
    else
        putsUart0("  Voting: IDLE (vote to start)\n");

    putsUart0("---------------------------\n");
}
