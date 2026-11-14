#ifndef GUARD_DECK_BUMP_H
#define GUARD_DECK_BUMP_H

#include <stdbool.h>
#include "deck_gl_shader.h"

// Phase 9: pseudo normal map. A height field from the albedo luminance (3x3 Sobel on the texel neighbours, nearest
// sampling, so the pixel art stays crisp) tilts the surface normal per pixel; the tilt is lit by the time-of-day sun
// plus a cavity term, and comes out as a multiplier around 1.0 on the colour the program already computes.
// One GLSL block shared by the ground, tree/grass and character programs. Key voxel_bump (OFF/AUTO/LOW/MEDIUM/HIGH).
// No parallax: see the plan (docs/superpowers/plans/2026-11-14-deck-phase9-bump.md).

typedef struct
{
    bool ok;                   // the program holds the bump code (else it was built without it)
    GLint par, sun, tex, ground; // uBumpP, uBumpSun, uBumpTex, uGroundP
} DeckBumpUniforms;

// Builds the program with the bump block in front of the fragment shader body (the body starts with "#version 120\n" and
// uses `#ifdef DECK_BUMP` around its bump call, `uBumpMul(...)` below). If that fails it builds the body as it was.
bool DeckBump_Build(DeckGlProgram *out, DeckBumpUniforms *u, const char *name, const char *vs, const char *fs);

// voxel_shaders on and voxel_bump not OFF.
bool DeckBump_Enabled(void);
// voxel_shaders on and voxel_groundrelief not OFF (ground programs only: deckGroundMul in the block).
bool DeckBump_GroundOn(void);

// Once a frame (before the world passes): the sun that lights the relief (same vector the shadows use).
void DeckBump_SetSun(float dx, float dz, float strength);

// With the program bound: strength, sun and the size of the bound texture. slot > 0: height neighbours stay inside the
// slot x slot block of the texel (atlas of equal slots); slot 0: a clear neighbour (alpha 0) takes the centre height.
void DeckBump_Set(const DeckBumpUniforms *u, float texW, float texH, float slot);

// Buildings: a plain textured program (vertex colour x texture x relief) in place of the fixed-function texturing.
// PlainBegin binds it for the bound texture of the given size and returns false (nothing bound) when the setting is OFF
// or the program does not build. The alpha test of the page stays the fixed-function one. PlainEnd unbinds.
bool DeckBump_PlainBegin(float texW, float texH);
void DeckBump_PlainEnd(void);

#endif
