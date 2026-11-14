// Windows only: run-time loader for the OpenGL 1.2+ entry points (see compat/win/GL/gl.h).
#ifdef _WIN32
#include <SDL2/SDL.h>
#include <stdio.h>
#include <GL/gl.h>

// The header turns the gl names into macros; the table below needs the real symbol names, so
// the pointers are filled by name string and the type of each one comes from the X list.
#define WINGL_DEFINE(type, name) type WinGl_##name;
WINGL_FUNCS(WINGL_DEFINE)
#undef WINGL_DEFINE

int WinGl_Load(void)
{
    int missing = 0;
#define WINGL_LOAD(type, name)                                                       \
    do                                                                               \
    {                                                                                \
        WinGl_##name = (type)SDL_GL_GetProcAddress("gl" #name);                      \
        if (WinGl_##name == NULL)                                                    \
        {                                                                            \
            fprintf(stderr, "[WINGL] missing entry point gl%s\n", #name);            \
            missing++;                                                               \
        }                                                                            \
    } while (0);
    WINGL_FUNCS(WINGL_LOAD)
#undef WINGL_LOAD
    return missing;
}
#else
typedef int WinGlUnusedOnThisPlatform;
#endif
