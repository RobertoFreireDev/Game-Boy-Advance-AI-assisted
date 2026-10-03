// solid_platform - others stand on it (and ride along when it moves).
#include "solid_platform.h"
#include "behavior_params.h"

void bhv_solid_platform_init(Actor *a, const void *params) {
    const Params_solid_platform *p = params;
    a->solid_mode = p->one_way ? SOLID_ONE_WAY : SOLID_FULL;
}
