// main.c - boots the engine. Nothing else lives here: the game is in nodes/.
#include "engine/core.h"

int main(void) {
    core_init();
    core_run();
    return 0;
}
