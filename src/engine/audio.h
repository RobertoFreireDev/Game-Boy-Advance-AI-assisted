// audio.h - PSG sound driver: a music sequencer on the 4 GB-style channels plus sound
// effects. A sound effect borrows its channel; the music takes it back when it ends.
#ifndef ENGINE_AUDIO_H
#define ENGINE_AUDIO_H

#include "data.h"

// Turn the sound hardware on.
void audio_init(void);
// Start a song from the top (does nothing if it is already playing).
void audio_play_music(const MusicData *m);
// Stop the song.
void audio_stop_music(void);
// Play a sound effect (ignored if a higher-priority effect is using that channel).
void audio_play_sfx(const SfxData *s);
// Advance music and sound effects by one tick. Once per frame.
void audio_update(void);

#endif
