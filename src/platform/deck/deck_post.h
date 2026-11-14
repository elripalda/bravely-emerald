#ifndef GUARD_DECK_POST_H
#define GUARD_DECK_POST_H

#include <stdbool.h>
#include "deck_sunshadow.h"

// Deck post chain on an RGBA8 scene target: bloom, time-of-day grade, vignette, smooth DoF.
// GL (Core 0) thread only. The legacy diorama FBO and Pass_DepthOfField stay the fallback.

typedef struct
{
    int renderW, renderH;  // f->renderW / f->renderH: the logical 960x544 (letterbox viewport call)
    bool indoor;           // no DoF indoors (same rule as Frame_BeginPasses)
    float fade;            // f->fadeAmount: grade, vignette and bloom fade out with it
    float bloom;           // f->light.bloom (per weather, BaseLightFor)
    float daylight, glow;  // VitaDayNight_Get(); indoor passes 1 and 0 (neutral day)
    float sunX;            // screen x (0..1) of the shafts' focus point, from the sun direction
    float moonAmount;      // 0..1 moon ray strength at night (pale blue, slower)
    float time;            // animation clock, seconds
    float sunAmount;       // 0..1 shaft strength: sun and daylight, 0 at night, indoors, in rain, fog, sand, ash
    float zNear, zFar;     // projection planes of the scene (depth texture linearisation)
    float focusDist;       // eye to target distance (view-space depth of the focus plane), 0 = unknown
    bool battle;           // a 3D battle: only the backdrop blur and dim (voxel_battledim)
    const DeckSunResolve *sun; // this frame's sun shadow map (NULL: none)
    float dofTopRows, dofBottomRows, dofTopAlpha, dofBottomAlpha; // VOXEL_DOF_* (voxel_tuning.h:89-92)
} DeckPostFrame;

typedef struct
{
    int quality;   // voxel_quality: 0 low, 1 medium, 2 high
    bool shaders;  // voxel_shaders
    bool grade;    // voxel_grade on (grade + vignette)
    bool bloom;    // voxel_bloom on
    bool blur;     // voxel_blur on
    bool tilt;     // voxel_tiltshift on (needs the depth texture, else the band blur runs)
    bool shafts;   // voxel_shafts on
    bool sun;      // voxel_sunshadow on and the composite can resolve it
    int sunLvl;
    int dimLvl;    // voxel_battledim, -1 off
    bool lut;      // voxel_lut on (a generated colour table replaces the procedural grade)
    int gradeLvl, bloomLvl, blurLvl, tiltLvl, shaftLvl, lutLvl; // resolved levels 0 low .. 2 high (AUTO = quality); -1 when off
} DeckPostConfig;

void DeckPost_Init(int sceneW, int sceneH); // the size arguments are replaced by voxel_res
void DeckPost_Shutdown(void);
DeckPostConfig DeckPost_ReadConfig(void);
// true: this frame's 3D scene goes to the RGBA8 scene target. Call before binding a framebuffer.
// May recreate the scene target when voxel_res changed (960 / 1120 / 1280 wide; the picture is 960:544, the heights 544 / 636 / 726 are that ratio rounded to even).
bool DeckPost_WantScene(int renderW, int renderH, bool indoor, bool battle);
// Size of the scene target: the 3D viewport of the frame (the projection aspect stays 960/544).
int DeckPost_SceneWidth(void);
int DeckPost_SceneHeight(void);
bool DeckPost_SunReady(void); // the composite has the sun shadow resolve and the scene has a depth texture
void DeckPost_BindScene(void);
// Leaves: framebuffer 0 with the letterboxed viewport and scissor, program 0, texture unit 0
// active with GL_TEXTURE_2D enabled, blend/depth test/alpha test off, depth mask off, client
// arrays off, projection glOrtho(0,1,1,0), modelview identity (as Pass_DepthOfField leaves it).
void DeckPost_Run(const DeckPostFrame *in);

#endif
