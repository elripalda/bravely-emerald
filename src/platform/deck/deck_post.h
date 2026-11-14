#ifndef GUARD_DECK_POST_H
#define GUARD_DECK_POST_H

#include <stdbool.h>

// Deck post chain on an RGBA8 scene target: bloom, time-of-day grade, vignette, smooth DoF.
// GL (Core 0) thread only. The legacy diorama FBO and Pass_DepthOfField stay the fallback.

typedef struct
{
    int renderW, renderH;  // f->renderW / f->renderH (must equal the scene target size)
    bool indoor;           // no DoF indoors (same rule as Frame_BeginPasses)
    float fade;            // f->fadeAmount: grade, vignette and bloom fade out with it
    float bloom;           // f->light.bloom (per weather, BaseLightFor)
    float daylight, glow;  // VitaDayNight_Get(); indoor passes 1 and 0 (neutral day)
    float dofTopRows, dofBottomRows, dofTopAlpha, dofBottomAlpha; // VOXEL_DOF_* (voxel_tuning.h:89-92)
} DeckPostFrame;

typedef struct
{
    int quality;   // voxel_quality: 0 low, 1 medium, 2 high
    bool shaders;  // voxel_shaders
    bool grade;    // voxel_grade on (grade + vignette)
    bool bloom;    // voxel_bloom on
    bool blur;     // voxel_blur on
    int gradeLvl, bloomLvl, blurLvl; // resolved levels 0 low .. 2 high (AUTO = quality); -1 when off
} DeckPostConfig;

void DeckPost_Init(int sceneW, int sceneH);
void DeckPost_Shutdown(void);
DeckPostConfig DeckPost_ReadConfig(void);
// true: this frame's 3D scene goes to the RGBA8 scene target. Call before binding a framebuffer.
bool DeckPost_WantScene(int renderW, int renderH, bool indoor);
void DeckPost_BindScene(void);
// Leaves: framebuffer 0 with the letterboxed viewport and scissor, program 0, texture unit 0
// active with GL_TEXTURE_2D enabled, blend/depth test/alpha test off, depth mask off, client
// arrays off, projection glOrtho(0,1,1,0), modelview identity (as Pass_DepthOfField leaves it).
void DeckPost_Run(const DeckPostFrame *in);

#endif
