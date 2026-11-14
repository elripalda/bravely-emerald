#ifndef _3DS_PLATFORM_H_VOXEL_STUB_
#define _3DS_PLATFORM_H_VOXEL_STUB_
#include <stdint.h>

static inline int32_t CtrSettings_VoxelZoom(void)
{
    return 100;
}

#ifdef PLATFORM_DECK
#include "platform/vita_extras.h"
/* Deck: voxel_pitch (CAMERA TILT), 0 classic 40, 1 36, 2 32 (default), 3 28, 4 24. Read every frame by the camera,
 * so a change applies at once. Never below 24: the picture's top edge must stay under the horizon (fov 35). */
static inline int32_t CtrSettings_VoxelPitch(void)
{
    static const int32_t kDeg[5] = { 40, 36, 32, 28, 24 };
    const int v = VitaExtras_GetFast(FAST_EXTRA_VOXEL_PITCH);

    return kDeg[(v < 0 || v > 4) ? 2 : v];
}
#else
static inline int32_t CtrSettings_VoxelPitch(void)
{
    return 40;
}
#endif

#endif // _3DS_PLATFORM_H_VOXEL_STUB_
