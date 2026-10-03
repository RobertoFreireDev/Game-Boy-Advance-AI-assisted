// vars.c - the game's global variables, addressed by VAR_* id.
#include "vars.h"
#include "data.h"

static s32 s_vars[VAR_ARRAY_SIZE];
static u32 s_version;

void vars_reset(void) {
    for (int i = 0; i < VAR_COUNT; i++) s_vars[i] = g_var_initial[i];
    s_version++;
}

s32 vars_get(s16 id) {
    return (id >= 0 && id < VAR_COUNT) ? s_vars[id] : 0;
}

void vars_set(s16 id, s32 value) {
    if (id < 0 || id >= VAR_COUNT || s_vars[id] == value) return;
    s_vars[id] = value;
    s_version++;
}

void vars_add(s16 id, s32 delta) {
    if (id >= 0 && id < VAR_COUNT) vars_set(id, s_vars[id] + delta);
}

u32 vars_version(void) { return s_version; }

s32 *vars_raw(void) { s_version++; return s_vars; }
