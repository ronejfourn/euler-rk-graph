#pragma once

#include "common.h"
typedef union SDL_Event SDL_Event;

enum
{
    MOD_NONE  = 0,
    MOD_ALT   = 1 << 0,
    MOD_CTRL  = 1 << 1,
    MOD_SHIFT = 1 << 2,
};

enum
{
    BTN_LEFT  = 0,
    BTN_MID   = 1,
    BTN_RIGHT = 2,
};

typedef struct
{
    u8 ckeys[512];
    u8 pkeys[512];
    u8 cbtns[3];
    u8 pbtns[3];
    u8 mods;
    vec2i cursor_screen;
    vec2d cursor_world;
    i8 wheel;
}
events_t;

int init_events    (events_t *events);
void handle_events (const SDL_Event *ev, events_t *app);
void advance_events(events_t *events);
void handle_zoom_and_pan(world_t *world, events_t *events);

static inline u8 key_held (events_t *events, int key)
{ return  events->ckeys[key] &&  events->pkeys[key]; }
static inline u8 key_pressed (events_t *events, int key)
{ return  events->ckeys[key] && !events->pkeys[key]; }
static inline u8 key_released(events_t *events, int key)
{ return !events->ckeys[key] &&  events->pkeys[key]; }

static inline u8 btn_held (events_t *events, int btn)
{ return  events->cbtns[btn] &&  events->pbtns[btn]; }
static inline u8 btn_pressed (events_t *events, int btn)
{ return  events->cbtns[btn] && !events->pbtns[btn]; }
static inline u8 btn_released(events_t *events, int btn)
{ return !events->cbtns[btn] &&  events->pbtns[btn]; }
