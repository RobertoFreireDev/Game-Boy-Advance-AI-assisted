// camera_target - the camera follows this actor.
#include "camera_target.h"
#include "engine/camera.h"

void bhv_camera_target_init(Actor *a, const void *params) {
    (void)params;
    camera_set_target(a);
}
