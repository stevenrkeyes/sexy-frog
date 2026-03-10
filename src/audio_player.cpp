#include "audio_player.h"
#include <SD.h>
#include <SPI.h>
#include <string.h>
#include "max98357.h"

// I2S pins for audio output (adjust based on your Audio BFF)
#define I2S_BCLK_PIN 29  // Bit clock
#define I2S_LRCLK_PIN 28 // Left/Right clock (word select)
#define I2S_DOUT_PIN 3  // Data out


// Audio buffer size
#define AUDIO_BUFFER_SIZE 20000

static bool audioInitialized = false;
static uint8_t s_audioBuffer[AUDIO_BUFFER_SIZE];
static int16_t s_sampleBuffer[AUDIO_BUFFER_SIZE / 2];

bool initAudioPlayer() {
  Serial.println("Initializing audio player...");
  
  // Initialize SD card
  Serial.print("Initializing SD card...");
  if (!SD.begin(32000000UL, D0)) {
    Serial.println("SD card initialization failed!");
    return false;
  }
  Serial.println("SD card initialized.");

  // Initialize MAX98357A (I2S DAC+amp).
  Serial.print("Initializing MAX98357A (I2S)... ");
  if (!max98357::begin(I2S_BCLK_PIN, I2S_LRCLK_PIN, I2S_DOUT_PIN)) {
    Serial.println("failed");
    return false;
  }
  Serial.println("ok");
  
  audioInitialized = true;
  return true;
}

void listSDFiles() {
  if (!audioInitialized) {
    Serial.println("SD card not initialized - cannot list files");
    return;
  }
  Serial.println("SD card root directory:");
  Serial.println("----------------------------");
  File root = SD.open("/");
  if (!root) {
    Serial.println("Failed to open root directory");
    return;
  }
  if (!root.isDirectory()) {
    Serial.println("Root is not a directory");
    root.close();
    return;
  }
  File entry = root.openNextFile();
  while (entry) {
    if (entry.isDirectory()) {
      Serial.print("  [DIR]  ");
      Serial.println(entry.name());
    } else {
      Serial.print("  ");
      Serial.print(entry.name());
      Serial.print("  (");
      Serial.print(entry.size());
      Serial.println(" bytes)");
    }
    entry.close();
    entry = root.openNextFile();
  }
  root.close();
  Serial.println("----------------------------");
}

bool playSoundFile(const char* filename) {
  if (!audioInitialized) {
    Serial.println("Audio player not initialized!");
    return false;
  }
  
  Serial.print("Opening file: ");
  Serial.println(filename);

  File audioFile = SD.open(filename);
  if (audioFile) {
    Serial.println("File opened successfully");
  }
  if (!audioFile) {
    Serial.print("Error: Could not open file: ");
    Serial.println(filename);
    return false;
  }

  // RIFF header: "RIFF" (4) + file size (4) + "WAVE" (4)
  uint8_t riff[12];
  if (audioFile.read(riff, 12) != 12) {
    Serial.println("Error: Failed to read RIFF header");
    audioFile.close();
    return false;
  }
  if (strncmp((const char *)riff, "RIFF", 4) != 0) {
    Serial.println("Error: Not a RIFF file");
    audioFile.close();
    return false;
  }
  if (strncmp((const char *)riff + 8, "WAVE", 4) != 0) {
    Serial.println("Error: Not a WAVE file");
    audioFile.close();
    return false;
  }

  // Parse chunks: 4-byte ID + 4-byte size (little-endian), then chunk data
  uint16_t numChannels = 1;
  size_t dataStart = 0;   // file offset where data chunk payload starts
  size_t dataSize = 0;    // size of data chunk payload in bytes
  bool foundFmt = false;
  bool foundData = false;

  while (audioFile.available()) {
    char chunkId[5] = { 0 };
    uint8_t sizeBuf[4];
    if (audioFile.read((uint8_t *)chunkId, 4) != 4 || audioFile.read(sizeBuf, 4) != 4) {
      break;
    }
    uint32_t chunkSize = (uint32_t)sizeBuf[0] | ((uint32_t)sizeBuf[1] << 8) |
                        ((uint32_t)sizeBuf[2] << 16) | ((uint32_t)sizeBuf[3] << 24);

    if (memcmp(chunkId, "fmt ", 4) == 0) {
      // Format chunk: at least 16 bytes for PCM (audio format, num chans, sample rate, etc.)
      foundFmt = true;
      uint8_t fmtBuf[16];
      size_t toRead = chunkSize < 16 ? chunkSize : 16;
      if ((size_t)audioFile.read(fmtBuf, toRead) != toRead) {
        Serial.println("Error: Failed to read fmt chunk");
        audioFile.close();
        return false;
      }
      numChannels = (uint16_t)fmtBuf[2] | ((uint16_t)fmtBuf[3] << 8);
      if (chunkSize > 16) {
        audioFile.seek(audioFile.position() + (chunkSize - 16));
      }
      Serial.print("WAV chunk: fmt  channels=");
      Serial.println(numChannels);
    } else if (memcmp(chunkId, "data", 4) == 0) {
      dataStart = audioFile.position();
      dataSize = chunkSize;
      foundData = true;
      Serial.print("WAV chunk: data size=");
      Serial.println((unsigned long)dataSize);
      break;  // start playback from here
    } else if (memcmp(chunkId, "LIST", 4) == 0) {
      Serial.println("WAV chunk: LIST (skipped)");
      audioFile.seek(audioFile.position() + chunkSize);
    } else if (memcmp(chunkId, "bext", 4) == 0) {
      Serial.println("WAV chunk: bext (skipped)");
      audioFile.seek(audioFile.position() + chunkSize);
    } else if (memcmp(chunkId, "id3 ", 4) == 0) {
      Serial.println("WAV chunk: id3 (skipped)");
      audioFile.seek(audioFile.position() + chunkSize);
    } else {
      Serial.print("WAV chunk: ");
      Serial.print(chunkId[0]);
      Serial.print(chunkId[1]);
      Serial.print(chunkId[2]);
      Serial.print(chunkId[3]);
      Serial.println(" (skipped)");
      audioFile.seek(audioFile.position() + chunkSize);
    }
  }

  if (!foundFmt || !foundData) {
    Serial.println("Error: Missing fmt or data chunk");
    audioFile.close();
    return false;
  }

  bool isStereo = (numChannels == 2);
  Serial.println("Playing audio file...");

  // Seek to start of data payload and play
  audioFile.seek(dataStart);
  size_t bytesRead;
  size_t totalPlayed = 0;
  unsigned long startTime = millis();

  while (audioFile.available() && totalPlayed < dataSize) {
    size_t toRead = AUDIO_BUFFER_SIZE;
    if (dataSize - totalPlayed < toRead) {
      toRead = dataSize - totalPlayed;
    }
    bytesRead = audioFile.read(s_audioBuffer, toRead);
    if (bytesRead == 0) {
      break;
    }
    totalPlayed += bytesRead;

    {
      // Interpret as 16-bit little-endian PCM and stream to I2S.
      // If stereo, use only the first channel (L) and downmix to mono.
      size_t mono_sample_count = 0;

      if (isStereo) {
        // Each frame: L (2 bytes), R (2 bytes)
        const size_t frame_count = bytesRead / 4;
        if (frame_count == 0) {
          continue;
        }
        mono_sample_count = frame_count;
        for (size_t i = 0; i < frame_count; i++) {
          const size_t base = i * 4;
          s_sampleBuffer[i] = (int16_t)((uint16_t)s_audioBuffer[base] | ((uint16_t)s_audioBuffer[base + 1] << 8));
        }
      } else {
        // Mono: 2 bytes per sample
        const size_t sample_count = bytesRead / 2;
        if (sample_count == 0) {
          continue;
        }
        mono_sample_count = sample_count;
        for (size_t i = 0; i < sample_count; i++) {
          s_sampleBuffer[i] = (int16_t)((uint16_t)s_audioBuffer[2 * i] | ((uint16_t)s_audioBuffer[2 * i + 1] << 8));
        }
      }

      if (!max98357::writeMono16(s_sampleBuffer, mono_sample_count)) {
        Serial.println("Audio write failed");
        break;
      }
    }
  }
  
  audioFile.close();
  
  unsigned long duration = millis() - startTime;
  Serial.print("Playback complete. Duration: ");
  Serial.print(duration);
  Serial.println(" ms");
  
  return true;
}
