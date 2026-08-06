#include "audio.h"
#include <raylib.h>
#include <raymath.h>
#include <stdlib.h>
#include <math.h>

static Sound sounds[MAX_SOUNDS];
static Music music[MAX_MUSIC];
static int currentMusic = -1;

static Music LoadMusicStreamFromWave(Wave wave) {
    unsigned int dataSize = wave.frameCount * wave.channels * (wave.sampleSize / 8);
    unsigned int fileSize = 36 + dataSize;
    unsigned char *wavData = malloc(44 + dataSize);
    if (!wavData) return (Music){0};

    memcpy(wavData, "RIFF", 4);
    memcpy(wavData + 4, &fileSize, 4);
    memcpy(wavData + 8, "WAVE", 4);
    memcpy(wavData + 12, "fmt ", 4);
    unsigned int fmtSize = 16;
    memcpy(wavData + 16, &fmtSize, 4);
    unsigned short audioFormat = 1;
    memcpy(wavData + 20, &audioFormat, 2);
    unsigned short channels = wave.channels;
    memcpy(wavData + 22, &channels, 2);
    unsigned int sampleRate = wave.sampleRate;
    memcpy(wavData + 24, &sampleRate, 4);
    unsigned int byteRate = wave.sampleRate * wave.channels * (wave.sampleSize / 8);
    memcpy(wavData + 28, &byteRate, 4);
    unsigned short blockAlign = wave.channels * (wave.sampleSize / 8);
    memcpy(wavData + 32, &blockAlign, 2);
    unsigned short bitsPerSample = wave.sampleSize;
    memcpy(wavData + 34, &bitsPerSample, 2);
    memcpy(wavData + 36, "data", 4);
    memcpy(wavData + 40, &dataSize, 4);
    memcpy(wavData + 44, wave.data, dataSize);

    Music m = LoadMusicStreamFromMemory(".wav", wavData, 44 + dataSize);
    free(wavData);
    return m;
}

static Wave GenerateSineWave(float frequency, float duration, float volume) {
    int sampleRate = 44100;
    int sampleCount = (int)(sampleRate * duration);
    Wave wave = {0};
    wave.frameCount = sampleCount;
    wave.sampleRate = sampleRate;
    wave.sampleSize = 16;
    wave.channels = 1;
    wave.data = malloc(sampleCount * sizeof(short));

    short *samples = (short *)wave.data;
    for (int i = 0; i < sampleCount; i++) {
        float t = (float)i / sampleRate;
        float envelope = 1.0f;
        if (i < sampleCount * 0.1f) envelope = i / (sampleCount * 0.1f);
        else if (i > sampleCount * 0.9f) envelope = (sampleCount - i) / (sampleCount * 0.1f);
        float sample = sinf(2.0f * PI * frequency * t) * envelope * volume;
        samples[i] = (short)(sample * 32767.0f);
    }
    return wave;
}

static Wave GenerateSquareWave(float frequency, float duration, float volume) {
    int sampleRate = 44100;
    int sampleCount = (int)(sampleRate * duration);
    Wave wave = {0};
    wave.frameCount = sampleCount;
    wave.sampleRate = sampleRate;
    wave.sampleSize = 16;
    wave.channels = 1;
    wave.data = malloc(sampleCount * sizeof(short));

    short *samples = (short *)wave.data;
    for (int i = 0; i < sampleCount; i++) {
        float t = (float)i / sampleRate;
        float envelope = 1.0f;
        if (i < sampleCount * 0.05f) envelope = i / (sampleCount * 0.05f);
        else if (i > sampleCount * 0.8f) envelope = (sampleCount - i) / (sampleCount * 0.2f);
        float sample = (sinf(2.0f * PI * frequency * t) > 0 ? 1.0f : -1.0f) * envelope * volume;
        samples[i] = (short)(sample * 32767.0f);
    }
    return wave;
}

static Wave GenerateNoiseBurst(float duration, float volume) {
    int sampleRate = 44100;
    int sampleCount = (int)(sampleRate * duration);
    Wave wave = {0};
    wave.frameCount = sampleCount;
    wave.sampleRate = sampleRate;
    wave.sampleSize = 16;
    wave.channels = 1;
    wave.data = malloc(sampleCount * sizeof(short));

    short *samples = (short *)wave.data;
    for (int i = 0; i < sampleCount; i++) {
        float envelope = 1.0f;
        if (i < sampleCount * 0.02f) envelope = i / (sampleCount * 0.02f);
        else if (i > sampleCount * 0.5f) envelope = (sampleCount - i) / (sampleCount * 0.5f);
        float sample = ((float)rand() / RAND_MAX * 2.0f - 1.0f) * envelope * volume;
        samples[i] = (short)(sample * 32767.0f);
    }
    return wave;
}

static Wave GenerateFootstep(void) {
    Wave wave = {0};
    wave.frameCount = 44100 * 0.15f;
    wave.sampleRate = 44100;
    wave.sampleSize = 16;
    wave.channels = 1;
    wave.data = malloc(wave.frameCount * sizeof(short));

    short *samples = (short *)wave.data;
    for (int i = 0; i < wave.frameCount; i++) {
        float t = (float)i / wave.sampleRate;
        float envelope = expf(-t * 30.0f);
        float freq = 120.0f + 80.0f * sinf(t * 50.0f);
        float sample = sinf(2.0f * PI * freq * t) * envelope * 0.4f;
        sample += ((float)rand() / RAND_MAX * 2.0f - 1.0f) * envelope * 0.1f;
        samples[i] = (short)(sample * 32767.0f);
    }
    return wave;
}

static Wave GenerateGunshot(float baseFreq, float noiseMix, float duration) {
    int sampleRate = 44100;
    int sampleCount = (int)(sampleRate * duration);
    Wave wave = {0};
    wave.frameCount = sampleCount;
    wave.sampleRate = sampleRate;
    wave.sampleSize = 16;
    wave.channels = 1;
    wave.data = malloc(sampleCount * sizeof(short));

    short *samples = (short *)wave.data;
    for (int i = 0; i < sampleCount; i++) {
        float t = (float)i / sampleRate;
        float envelope = expf(-t * 80.0f);
        float tone = sinf(2.0f * PI * baseFreq * t) * envelope * (1.0f - noiseMix);
        float noise = ((float)rand() / RAND_MAX * 2.0f - 1.0f) * envelope * noiseMix;
        float sample = (tone + noise) * 0.6f;
        samples[i] = (short)(sample * 32767.0f);
    }
    return wave;
}

static Wave GeneratePoliceSiren(void) {
    int sampleRate = 44100;
    float duration = 2.0f;
    int sampleCount = (int)(sampleRate * duration);
    Wave wave = {0};
    wave.frameCount = sampleCount;
    wave.sampleRate = sampleRate;
    wave.sampleSize = 16;
    wave.channels = 1;
    wave.data = malloc(sampleCount * sizeof(short));

    short *samples = (short *)wave.data;
    for (int i = 0; i < sampleCount; i++) {
        float t = (float)i / sampleRate;
        float freq = 600.0f + 400.0f * sinf(2.0f * PI * 2.0f * t);
        float sample = sinf(2.0f * PI * freq * t) * 0.3f;
        samples[i] = (short)(sample * 32767.0f);
    }
    return wave;
}

static Wave GenerateExplosion(void) {
    int sampleRate = 44100;
    float duration = 1.5f;
    int sampleCount = (int)(sampleRate * duration);
    Wave wave = {0};
    wave.frameCount = sampleCount;
    wave.sampleRate = sampleRate;
    wave.sampleSize = 16;
    wave.channels = 1;
    wave.data = malloc(sampleCount * sizeof(short));

    short *samples = (short *)wave.data;
    for (int i = 0; i < sampleCount; i++) {
        float t = (float)i / sampleRate;
        float envelope = expf(-t * 3.0f);
        float lowRumble = sinf(2.0f * PI * 60.0f * t) * envelope * 0.5f;
        float noise = ((float)rand() / RAND_MAX * 2.0f - 1.0f) * envelope * 0.7f;
        float sample = (lowRumble + noise) * 0.8f;
        samples[i] = (short)(sample * 32767.0f);
    }
    return wave;
}

static Wave GenerateMatatuHorn(void) {
    int sampleRate = 44100;
    float duration = 0.8f;
    int sampleCount = (int)(sampleRate * duration);
    Wave wave = {0};
    wave.frameCount = sampleCount;
    wave.sampleRate = sampleRate;
    wave.sampleSize = 16;
    wave.channels = 1;
    wave.data = malloc(sampleCount * sizeof(short));

    short *samples = (short *)wave.data;
    for (int i = 0; i < sampleCount; i++) {
        float t = (float)i / sampleRate;
        float envelope = 1.0f;
        if (i > sampleCount * 0.7f) envelope = (sampleCount - i) / (sampleCount * 0.3f);
        float f1 = 350.0f, f2 = 440.0f;
        float sample = (sinf(2.0f * PI * f1 * t) + sinf(2.0f * PI * f2 * t)) * 0.5f * envelope * 0.4f;
        samples[i] = (short)(sample * 32767.0f);
    }
    return wave;
}

static Wave GenerateAmbientCrowd(void) {
    int sampleRate = 44100;
    float duration = 5.0f;
    int sampleCount = (int)(sampleRate * duration);
    Wave wave = {0};
    wave.frameCount = sampleCount;
    wave.sampleRate = sampleRate;
    wave.sampleSize = 16;
    wave.channels = 1;
    wave.data = malloc(sampleCount * sizeof(short));

    short *samples = (short *)wave.data;
    for (int i = 0; i < sampleCount; i++) {
        float t = (float)i / sampleRate;
        float sample = 0.0f;
        for (int h = 0; h < 8; h++) {
            float freq = 100.0f + h * 150.0f + sinf(t * 0.5f + h) * 20.0f;
            sample += sinf(2.0f * PI * freq * t) * (0.05f / (h + 1));
        }
        sample += ((float)rand() / RAND_MAX * 2.0f - 1.0f) * 0.02f;
        samples[i] = (short)(sample * 32767.0f);
    }
    return wave;
}

static Wave GenerateMusicLoop(int musicId) {
    int sampleRate = 44100;
    float duration = 4.0f;
    int sampleCount = (int)(sampleRate * duration);
    Wave wave = {0};
    wave.frameCount = sampleCount;
    wave.sampleRate = sampleRate;
    wave.sampleSize = 16;
    wave.channels = 2;
    wave.data = malloc(sampleCount * 2 * sizeof(short));

    short *samples = (short *)wave.data;
    float *freqs = NULL;
    int numFreqs = 0;

    switch (musicId) {
        case 0:
            freqs = (float[]){55.0f, 82.41f, 110.0f, 164.81f};
            numFreqs = 4;
            break;
        case 1:
            freqs = (float[]){110.0f, 146.83f, 164.81f, 220.0f};
            numFreqs = 4;
            break;
        case 2:
            freqs = (float[]){130.81f, 174.61f, 196.0f, 261.63f};
            numFreqs = 4;
            break;
        case 3:
            freqs = (float[]){61.74f, 92.5f, 123.47f, 185.0f};
            numFreqs = 4;
            break;
        case 4:
            freqs = (float[]){49.0f, 73.42f, 98.0f, 146.83f};
            numFreqs = 4;
            break;
    }

    for (int i = 0; i < sampleCount; i++) {
        float t = (float)i / sampleRate;
        float left = 0.0f, right = 0.0f;
        for (int f = 0; f < numFreqs; f++) {
            float freq = freqs[f];
            float env = 0.3f + 0.2f * sinf(2.0f * PI * 0.5f * t + f);
            left += sinf(2.0f * PI * freq * t) * env * (1.0f / (f + 1));
            right += sinf(2.0f * PI * freq * 1.01f * t) * env * (1.0f / (f + 1));
        }
        if (musicId == 1 || musicId == 2) {
            float kickEnv = expf(-fmodf(t, 0.5f) * 20.0f);
            left += sinf(2.0f * PI * 60.0f * t) * kickEnv * 0.4f;
            right += sinf(2.0f * PI * 60.0f * t) * kickEnv * 0.4f;
        }
        if (musicId == 3) {
            float noise = ((float)rand() / RAND_MAX * 2.0f - 1.0f) * 0.1f;
            left += noise;
            right += noise;
        }
        samples[i * 2] = (short)(left * 0.4f * 32767.0f);
        samples[i * 2 + 1] = (short)(right * 0.4f * 32767.0f);
    }
    return wave;
}

void AudioInit(void) {
    InitAudioDevice();
    SetMasterVolume(0.7f);

    Wave footstepWave = GenerateFootstep();
    sounds[0] = LoadSoundFromWave(footstepWave);
    UnloadWave(footstepWave);

    Wave pistolWave = GenerateGunshot(180.0f, 0.6f, 0.2f);
    sounds[1] = LoadSoundFromWave(pistolWave);
    UnloadWave(pistolWave);

    Wave akWave = GenerateGunshot(120.0f, 0.7f, 0.15f);
    sounds[2] = LoadSoundFromWave(akWave);
    UnloadWave(akWave);

    Wave deathWave = GenerateSineWave(150.0f, 0.5f, 0.4f);
    sounds[3] = LoadSoundFromWave(deathWave);
    UnloadWave(deathWave);

    Wave pickupWave = GenerateSquareWave(880.0f, 0.1f, 0.3f);
    sounds[4] = LoadSoundFromWave(pickupWave);
    UnloadWave(pickupWave);

    Wave missionWave = GenerateSineWave(440.0f, 0.3f, 0.4f);
    sounds[5] = LoadSoundFromWave(missionWave);
    UnloadWave(missionWave);

    Wave sirenWave = GeneratePoliceSiren();
    sounds[6] = LoadSoundFromWave(sirenWave);
    UnloadWave(sirenWave);

    Wave explosionWave = GenerateExplosion();
    sounds[7] = LoadSoundFromWave(explosionWave);
    UnloadWave(explosionWave);

    Wave matatuWave = GenerateMatatuHorn();
    sounds[8] = LoadSoundFromWave(matatuWave);
    UnloadWave(matatuWave);

    Wave crowdWave = GenerateAmbientCrowd();
    sounds[9] = LoadSoundFromWave(crowdWave);
    UnloadWave(crowdWave);

    for (int i = 0; i < MAX_MUSIC; i++) {
        Wave musicWave = GenerateMusicLoop(i);
        music[i] = LoadMusicStreamFromWave(musicWave);
        music[i].looping = true;
        UnloadWave(musicWave);
    }
}

void AudioPlaySound(int id, float pitch) {
    if (id < 0 || id >= MAX_SOUNDS) return;
    SetSoundPitch(sounds[id], pitch);
    PlaySound(sounds[id]);
}

void AudioPlayMusic(int id) {
    if (id < 0 || id >= MAX_MUSIC) return;
    if (currentMusic >= 0 && currentMusic != id) {
        StopMusicStream(music[currentMusic]);
    }
    currentMusic = id;
    PlayMusicStream(music[id]);
}

void AudioUpdate(void) {
    if (currentMusic >= 0) {
        UpdateMusicStream(music[currentMusic]);
    }
}

void AudioStopMusic(void) {
    if (currentMusic >= 0) {
        StopMusicStream(music[currentMusic]);
        currentMusic = -1;
    }
}

void AudioClose(void) {
    for (int i = 0; i < MAX_SOUNDS; i++) {
        UnloadSound(sounds[i]);
    }
    for (int i = 0; i < MAX_MUSIC; i++) {
        UnloadMusicStream(music[i]);
    }
    CloseAudioDevice();
}