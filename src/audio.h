#ifndef AUDIO_H
#define AUDIO_H

#include "common.h"

void AudioInit(void);
void AudioPlaySound(int id, float pitch);
void AudioPlayMusic(int id);
void AudioUpdate(void);
void AudioStopMusic(void);
void AudioClose(void);

#endif