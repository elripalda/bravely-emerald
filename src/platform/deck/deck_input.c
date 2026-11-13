#include <stdio.h>
#include <string.h>
#include <SDL2/SDL.h>

#include "vita_input.h"
#include "vita_platform.h"
#include "deck_video.h"

// Analog stick deflection thresholds (0-255, center 128)
static const uint8_t STICK_DEADZONE_LOW  = 64U;
static const uint8_t STICK_DEADZONE_HIGH = 192U;

// GBA centered viewport on the 960x544 logical screen (816x544, offset 72px)
static const int32_t GBA_VIEWPORT_OFFSET_X = 72;
static const int32_t GBA_VIEWPORT_WIDTH    = 816;
static const int32_t GBA_VIEWPORT_HEIGHT   = 544;
static const int32_t GBA_NATIVE_WIDTH      = 240;
static const int32_t GBA_NATIVE_HEIGHT     = 160;

// Internal persistent state
static VitaInputState sCurrentState;
static uint32_t sPreviousButtons = 0U;
static SDL_GameController *sPad;

bool VitaInput_Init(void)
{
    SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
    for (int i = 0; i < SDL_NumJoysticks(); i++)
    {
        if (SDL_IsGameController(i))
        {
            sPad = SDL_GameControllerOpen(i);
            break;
        }
    }
    memset(&sCurrentState, 0, sizeof(sCurrentState));
    sPreviousButtons = 0U;
    VITA_LOG("[INPUT] controller %s\n", sPad ? "opened" : "not found");
    return true;
}

static uint8_t AxisToByte(Sint16 v)
{
    return (uint8_t)((v >> 8) + 128);   // -32768..32767 -> 0..255, centre 128
}

static uint32_t DecodePad(SDL_GameController *pad, uint8_t *lx, uint8_t *ly, uint8_t *rx, uint8_t *ry)
{
    uint32_t b = VITA_BTN_NONE;
    *lx = AxisToByte(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX));
    *ly = AxisToByte(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY));
    *rx = AxisToByte(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTX));
    *ry = AxisToByte(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTY));

#define HELD(btn) SDL_GameControllerGetButton(pad, btn)
    if (HELD(SDL_CONTROLLER_BUTTON_A))             b |= VITA_BTN_A;
    if (HELD(SDL_CONTROLLER_BUTTON_B))             b |= VITA_BTN_B;
    if (HELD(SDL_CONTROLLER_BUTTON_X))             b |= VITA_BTN_SQUARE;
    if (HELD(SDL_CONTROLLER_BUTTON_Y))             b |= VITA_BTN_TRIANGLE;
    if (HELD(SDL_CONTROLLER_BUTTON_BACK))          b |= VITA_BTN_SELECT;
    if (HELD(SDL_CONTROLLER_BUTTON_START))         b |= VITA_BTN_START;
    if (HELD(SDL_CONTROLLER_BUTTON_LEFTSHOULDER))  b |= VITA_BTN_L;
    if (HELD(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) b |= VITA_BTN_R;
    if (HELD(SDL_CONTROLLER_BUTTON_DPAD_UP)    || *ly < STICK_DEADZONE_LOW)  b |= VITA_BTN_UP;
    if (HELD(SDL_CONTROLLER_BUTTON_DPAD_DOWN)  || *ly > STICK_DEADZONE_HIGH) b |= VITA_BTN_DOWN;
    if (HELD(SDL_CONTROLLER_BUTTON_DPAD_LEFT)  || *lx < STICK_DEADZONE_LOW)  b |= VITA_BTN_LEFT;
    if (HELD(SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || *lx > STICK_DEADZONE_HIGH) b |= VITA_BTN_RIGHT;
#undef HELD
    if (*ry < STICK_DEADZONE_LOW)  b |= VITA_BTN_RS_UP;
    if (*ry > STICK_DEADZONE_HIGH) b |= VITA_BTN_RS_DOWN;
    if (*rx < STICK_DEADZONE_LOW)  b |= VITA_BTN_RS_LEFT;
    if (*rx > STICK_DEADZONE_HIGH) b |= VITA_BTN_RS_RIGHT;

    // Hardware safety: a genuine GBA d-pad never reports opposing directions simultaneously
    if ((b & (VITA_BTN_UP | VITA_BTN_DOWN)) == (VITA_BTN_UP | VITA_BTN_DOWN))
        b &= ~(VITA_BTN_UP | VITA_BTN_DOWN);
    if ((b & (VITA_BTN_LEFT | VITA_BTN_RIGHT)) == (VITA_BTN_LEFT | VITA_BTN_RIGHT))
        b &= ~(VITA_BTN_LEFT | VITA_BTN_RIGHT);
    return b;
}

void VitaInput_Poll(VitaInputState *const outState)
{
    uint32_t currentButtons = VITA_BTN_NONE;
    uint8_t lx = 128U, ly = 128U, rx = 128U, ry = 128U;   // stick centre when no pad is present

    if (sPad != NULL)
    {
        currentButtons = DecodePad(sPad, &lx, &ly, &rx, &ry);
    }
    sCurrentState.buttonsHeld     = currentButtons;
    sCurrentState.buttonsPressed  = currentButtons & ~sPreviousButtons;
    sCurrentState.buttonsReleased = ~currentButtons & sPreviousButtons;
    sPreviousButtons              = currentButtons;

    sCurrentState.leftStickX  = lx;
    sCurrentState.leftStickY  = ly;
    sCurrentState.rightStickX = rx;
    sCurrentState.rightStickY = ry;

    // Touch: Steam Input may turn touch into mouse events, so both are read.
    int vx, vy, vw, vh;
    DeckVideo_GetViewport(&vx, &vy, &vw, &vh);
    SDL_Window *win = SDL_GL_GetCurrentWindow();
    int winW = 1, winH = 1;
    if (win) SDL_GL_GetDrawableSize(win, &winW, &winH);

    float px = 0.0f, py = 0.0f;
    bool down = false;
    if (SDL_GetNumTouchDevices() > 0)
    {
        SDL_TouchID id = SDL_GetTouchDevice(0);
        if (SDL_GetNumTouchFingers(id) > 0)
        {
            SDL_Finger *f = SDL_GetTouchFinger(id, 0);
            px = f->x * (float)winW;
            py = f->y * (float)winH;
            down = true;
        }
    }
    if (!down)
    {
        int mx, my;
        if (SDL_GetMouseState(&mx, &my) & SDL_BUTTON(SDL_BUTTON_LEFT))
        {
            px = (float)mx; py = (float)my; down = true;
        }
    }
    sCurrentState.touchActive = false;
    if (down && px >= vx && px < vx + vw && py >= vy && py < vy + vh)
    {
        sCurrentState.touchActive  = true;
        sCurrentState.touchScreenX = (int32_t)((px - vx) * DECK_LOGICAL_WIDTH / vw);
        sCurrentState.touchScreenY = (int32_t)((py - vy) * DECK_LOGICAL_HEIGHT / vh);

        // Map to GBA coords if inside the active viewport
        const int32_t sx = sCurrentState.touchScreenX;
        const int32_t sy = sCurrentState.touchScreenY;

        if (sx >= GBA_VIEWPORT_OFFSET_X && sx < (GBA_VIEWPORT_OFFSET_X + GBA_VIEWPORT_WIDTH) &&
            sy >= 0 && sy < GBA_VIEWPORT_HEIGHT)
        {
            sCurrentState.touchGbaX = (sx - GBA_VIEWPORT_OFFSET_X) * GBA_NATIVE_WIDTH / GBA_VIEWPORT_WIDTH;
            sCurrentState.touchGbaY = sy * GBA_NATIVE_HEIGHT / GBA_VIEWPORT_HEIGHT;
        }
        else
        {
            sCurrentState.touchGbaX = -1;
            sCurrentState.touchGbaY = -1;
        }
    }

    if (outState != NULL)
    {
        *outState = sCurrentState;
    }
}

uint32_t VitaInput_GetButtonsHeld(void)
{
    return sCurrentState.buttonsHeld;
}

bool VitaInput_ReadTouchScreen(int32_t *const outX, int32_t *const outY)
{
    if (!sCurrentState.touchActive)
    {
        return false;
    }

    if (outX != NULL) *outX = sCurrentState.touchScreenX;
    if (outY != NULL) *outY = sCurrentState.touchScreenY;
    return true;
}

bool VitaInput_ReadTouchGBA(int32_t *const outGbaX, int32_t *const outGbaY)
{
    if (!sCurrentState.touchActive || sCurrentState.touchGbaX < 0 || sCurrentState.touchGbaY < 0)
    {
        return false;
    }

    if (outGbaX != NULL) *outGbaX = sCurrentState.touchGbaX;
    if (outGbaY != NULL) *outGbaY = sCurrentState.touchGbaY;
    return true;
}
