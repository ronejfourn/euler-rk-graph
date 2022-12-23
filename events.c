#include "events.h"
#include "common.h"
#include <SDL2/SDL_events.h>

int init_events(events_t *events)
{
    memset(events->cbtns, 0, sizeof(events->cbtns));
    memset(events->pbtns, 0, sizeof(events->pbtns));
    memset(events->ckeys, 0, sizeof(events->ckeys));
    memset(events->pkeys, 0, sizeof(events->pkeys));
    events->mods = 0;
    events->cursor_screen.x = 0;
    events->cursor_screen.y = 0;
    events->cursor_world.x  = 0;
    events->cursor_world.y  = 0;
    return 1;
}

void advance_events(events_t *events)
{
    memcpy(events->pbtns, events->cbtns, sizeof(events->pbtns));
    memcpy(events->pkeys, events->ckeys, sizeof(events->pkeys));
    events->wheel = 0;
}

void handle_events(const SDL_Event *ev, events_t *events)
{
    switch (ev->type)
    {
        case SDL_MOUSEBUTTONUP:
        case SDL_MOUSEBUTTONDOWN:
        {
            events->cbtns[ev->button.button - 1] = ev->button.state;
            break;
        }
        case SDL_MOUSEMOTION:
        {
            events->cursor_screen.x = ev->motion.x;
            events->cursor_screen.y = ev->motion.y;
            break;
        }
        case SDL_KEYUP:
        case SDL_KEYDOWN:
        {
            if (!ev->key.repeat)
                events->ckeys[ev->key.keysym.scancode] = ev->key.state;
            switch(ev->key.keysym.scancode)
            {
#define CASE(x) case SDL_SCANCODE_L ##x: case SDL_SCANCODE_R ##x: events->mods = ev->key.state ? events->mods | MOD_ ##x : events->mods & (~MOD_ ##x); break
                CASE(ALT);
                CASE(CTRL);
                CASE(SHIFT);
#undef CASE
                default: break;
            }
            break;
        }
        case SDL_MOUSEWHEEL:
        {
            events->wheel = ev->wheel.y;
            break;
        }
    }
}

void handle_zoom_and_pan(world_t *world, events_t *events)
{
    if (btn_pressed(events, BTN_LEFT))
        world->pan = events->cursor_screen;

    if (btn_held(events, BTN_LEFT))
    {
        world->offset.x -= (events->cursor_screen.x - world->pan.x) / world->scale;
        world->offset.y -= (events->cursor_screen.y - world->pan.y) / world->scale;
        world->pan = events->cursor_screen;
    }

    vec2f before_zoom, after_zoom;
    screen_to_worldf(world, events->cursor_screen.x, events->cursor_screen.y, &before_zoom.x, &before_zoom.y);
    if (events->wheel > 0) world->scale *= 1.1f;
    if (events->wheel < 0) world->scale *= 0.9f;
    screen_to_worldf(world, events->cursor_screen.x, events->cursor_screen.y, &after_zoom.x, &after_zoom.y);
    world->offset.x += before_zoom.x - after_zoom.x;
    world->offset.y += before_zoom.y - after_zoom.y;

    screen_to_worldf(world, events->cursor_screen.x, events->cursor_screen.y, &events->cursor_world.x, &events->cursor_world.y);
}
