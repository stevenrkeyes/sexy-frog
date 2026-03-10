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

// List files in the root directory of the SD card (prints to Serial)
void listSDFiles();

#endif // AUDIO_PLAYER_H




