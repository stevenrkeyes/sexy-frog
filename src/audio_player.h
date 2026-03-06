#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#include <Arduino.h>

// Initialize audio player (SD card and I2S/audio output)
// Returns true if successful, false otherwise
bool initAudioPlayer();

// Play a sound file from SD card
// filename: path to the audio file (e.g., "sound.wav")
// Returns true if successful, false otherwise
bool playSoundFile(const char* filename);

#endif // AUDIO_PLAYER_H




