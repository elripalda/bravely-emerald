#ifndef DECK_COMPAT_VITAGL_H
#define DECK_COMPAT_VITAGL_H

#include <GL/gl.h>
#include <GL/glext.h>
// prototypes only: pulling in SDL here clashes with game macros (min/max) in files that include vitaGL.h
void DeckVideo_Swap(void);
void DeckVideo_SetSwapInterval(int interval);

// vitaGL setup calls have no desktop equivalent: the window and context come from DeckVideo_Init.
#define SHARK_OPT_FAST 0
#define SCE_GXM_MULTISAMPLE_NONE 0
#define SCE_KERNEL_CPU_MASK_USER_2 0

static inline void vglSetupGarbageCollector(int a, int b) { (void)a; (void)b; }
static inline void vglSetupRuntimeShaderCompiler(int a, int b, int c, int d) { (void)a; (void)b; (void)c; (void)d; }
static inline void vglUseLowPrecision(int v) { (void)v; }
static inline void vglUseVramForUSSE(int v) { (void)v; }
static inline void vglUseCachedMem(int v) { (void)v; }
static inline void vglUseTripleBuffering(int v) { (void)v; }
static inline void vglSwapResolution(int w, int h) { (void)w; (void)h; }
static inline void vglSwapBuffers(int hasCommonDialog) { (void)hasCommonDialog; DeckVideo_Swap(); }
static inline void eglSwapInterval(void *display, int interval) { (void)display; DeckVideo_SetSwapInterval(interval); }
// replaces vglGetTexDataPointer: no direct texel access on desktop GL, callers fall back to glTexSubImage2D
static inline void *vglGetTexDataPointer(unsigned int target) { (void)target; return 0; }
// replaces vglMemFree/vglMemTotal: Vita memory pools do not exist on desktop, the perf HUD reads 0
enum { VGL_MEM_VRAM, VGL_MEM_RAM, VGL_MEM_SLOW, VGL_MEM_BUDGET, VGL_MEM_EXTERNAL, VGL_MEM_ALL, VGL_MEM_PHYCONT };
static inline unsigned int vglMemFree(int type) { (void)type; return 0; }
static inline unsigned int vglMemTotal(int type) { (void)type; return 0; }
int vglInitWithCustomSizes(int, int, int, int, int, int, int, int);

#endif
