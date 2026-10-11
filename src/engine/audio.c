// audio.c - PSG driver: music sequencer + sound effects.
#include <tonc_memmap.h>
#include <tonc_memdef.h>
#include "audio.h"

#define TRIGGER 0x8000

typedef struct {
    const SfxData *sfx;
    u8 step, ticks;
} SfxState;

static const MusicData *s_music;
static u16 s_row;
static fixed s_acc;
static SfxState s_sfx[4];
static const u32 *s_wave;           // waveform currently in wave RAM

// Noise pitch 0 (low rumble) .. 15 (high hiss): (shift << 4) | ratio.
static const u8 s_noise_reg[16] = {
    0xD1, 0xC1, 0xB1, 0xA1, 0x91, 0x81, 0x71, 0x61,
    0x51, 0x41, 0x31, 0x21, 0x11, 0x01, 0x00, 0x08 };

// A soft square-ish wave, used by wave-channel sound effects when no song set one.
static const u32 s_default_wave[4] = { 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000 };

static void wave_load(const u32 *w) {
    if (s_wave == w) return;
    s_wave = w;
    REG_SND3SEL = 0x40;             // stop, select bank 1 for playback -> writes go to bank 0
    REG_WAVE_RAM0 = w[0];
    REG_WAVE_RAM1 = w[1];
    REG_WAVE_RAM2 = w[2];
    REG_WAVE_RAM3 = w[3];
    REG_SND3SEL = 0x80;             // play bank 0, 32 samples
}

static u16 wave_volume(u8 v) {
    static const u16 bits[5] = { 0x0000, 0x6000, 0x4000, 0x8000, 0x2000 };   // 0, 25, 50, 75, 100 %
    return bits[v > 4 ? 4 : v];
}

static void ch_off(int ch) {
    switch (ch) {
    case CH_SQUARE1: REG_SND1CNT = 0; REG_SND1FREQ = TRIGGER; break;
    case CH_SQUARE2: REG_SND2CNT = 0; REG_SND2FREQ = TRIGGER; break;
    case CH_WAVE:    REG_SND3CNT = 0; break;
    case CH_NOISE:   REG_SND4CNT = 0; REG_SND4FREQ = TRIGGER; break;
    }
}

static void ch_note(int ch, u8 note, u8 vol, u8 duty, u8 decay) {
    if (note == NOTE_REST) { ch_off(ch); return; }
    u16 env = (u16)((vol & 15) << 12 | (decay & 7) << 8);
    note &= 127;
    switch (ch) {
    case CH_SQUARE1:
        REG_SND1CNT = env | (duty & 3) << 6;
        REG_SND1FREQ = g_square_rate[note] | TRIGGER;
        break;
    case CH_SQUARE2:
        REG_SND2CNT = env | (duty & 3) << 6;
        REG_SND2FREQ = g_square_rate[note] | TRIGGER;
        break;
    case CH_WAVE:
        if (!s_wave) wave_load(s_default_wave);
        REG_SND3CNT = wave_volume(vol);
        REG_SND3FREQ = g_wave_rate[note] | TRIGGER;
        break;
    case CH_NOISE:
        REG_SND4CNT = env;
        REG_SND4FREQ = s_noise_reg[note & 15] | TRIGGER;
        break;
    }
}

void audio_init(void) {
    REG_SNDSTAT = SSTAT_ENABLE;
    REG_SNDDMGCNT = SDMG_BUILD_LR(SDMG_SQR1 | SDMG_SQR2 | SDMG_WAVE | SDMG_NOISE, 7);
    REG_SNDDSCNT = SDS_DMG100;
    REG_SND1SWEEP = SSW_OFF;
    for (int ch = 0; ch < 4; ch++) ch_off(ch);
}

void audio_play_music(const MusicData *m) {
    if (m == s_music) return;
    audio_stop_music();
    s_music = m;
    s_row = 0;
    s_acc = m->ticks_per_row;       // play the first row right away
    if (m->inst[CH_WAVE].used) wave_load(m->inst[CH_WAVE].wave);
}

void audio_stop_music(void) {
    s_music = NULL;
    for (int ch = 0; ch < 4; ch++)
        if (!s_sfx[ch].sfx) ch_off(ch);
}

static void sfx_step(int ch) {
    const SfxState *st = &s_sfx[ch];
    const SfxStep *s = &st->sfx->steps[st->step];
    ch_note(ch, s->value, s->volume, st->sfx->duty, 0);
}

void audio_play_sfx(const SfxData *s) {
    SfxState *st = &s_sfx[s->channel];
    if (st->sfx && st->sfx->priority > s->priority) return;
    st->sfx = s;
    st->step = 0;
    st->ticks = s->steps[0].ticks;
    sfx_step(s->channel);
}

static void music_row(void) {
    const MusicData *m = s_music;
    if (s_row >= m->row_count) {
        if (!m->loop) { audio_stop_music(); return; }
        s_row = m->loop_row;            // an intro plays once; the loop starts after it
    }
    for (int ch = 0; ch < 4; ch++) {
        if (!m->rows[ch] || !m->inst[ch].used || s_sfx[ch].sfx) continue;
        u8 v = m->rows[ch][s_row];
        if (v == NOTE_HOLD) continue;
        if (ch == CH_WAVE) wave_load(m->inst[CH_WAVE].wave);
        ch_note(ch, v, m->inst[ch].volume, m->inst[ch].duty, m->inst[ch].decay);
    }
    s_row++;
}

void audio_update(void) {
    if (s_music) {
        s_acc += FX_ONE;
        while (s_music && s_acc >= s_music->ticks_per_row) {
            s_acc -= s_music->ticks_per_row;
            music_row();
        }
    }
    for (int ch = 0; ch < 4; ch++) {
        SfxState *st = &s_sfx[ch];
        if (!st->sfx || --st->ticks > 0) continue;
        if (++st->step >= st->sfx->step_count) {
            st->sfx = NULL;
            ch_off(ch);             // the song's next note on this channel takes over
            continue;
        }
        st->ticks = st->sfx->steps[st->step].ticks;
        sfx_step(ch);
    }
}
