#pragma once

#include "common.h"
typedef struct SDL_Window   SDL_Window;
typedef struct SDL_Texture  SDL_Texture;
typedef struct SDL_Renderer SDL_Renderer;

typedef struct graphics_t
{
    SDL_Renderer *renderer;

    struct
    {
        SDL_Texture *texture;
        i32 gw, gh, nc;
        u8 max, min;
    } font;
} graphics_t;

int  init_graphics(SDL_Window *window, graphics_t *graphics);
void destroy_graphics(graphics_t *graphics);
rect_t get_text_rect(const graphics_t *graphics, const string_t *text);
SDL_Texture *create_texture(graphics_t *graphics, void *data, u32 w, u32 h, u32 ch);

void sdraw_text(graphics_t *graphics, i32 x, i32 y, const string_t *text, u32 color);
void sdraw_rect(graphics_t *graphics, i32 x, i32 y, i32 w, i32 h, u32 color);
void sfill_rect(graphics_t *graphics, i32 x, i32 y, i32 w, i32 h, u32 color);
void sdraw_rounded_rect(graphics_t *graphics, i32 x, i32 y, i32 w, i32 h, i32 r, u32 color);
void sfill_rounded_rect(graphics_t *graphics, i32 x, i32 y, i32 w, i32 h, i32 r, u32 color);
void sdraw_line(graphics_t *graphics, i32 sx, i32 sy, i32 ex, i32 ey, u32 color);

void draw_text(graphics_t *graphics, world_t *world, float x, float y, float scale, const string_t *text, u32 color);
void draw_rect(graphics_t *graphics, world_t *world, float x, float y, float w, float h, u32 color);
void fill_rect(graphics_t *graphics, world_t *world, float x, float y, float w, float h, u32 color);
void draw_rounded_rect(graphics_t *graphics, world_t *world, float x, float y, float w, float h, float r, u32 color);
void fill_rounded_rect(graphics_t *graphics, world_t *world, float x, float y, float w, float h, float r, u32 color);
void draw_line(graphics_t *graphics, world_t *world, float sx, float sy, float ex, float ey, u32 color);

#define draw_rectr(G, W, R, C) draw_rect((G), (W), (R).x, (R).y, (R).w, (R).h, (C))
#define fill_rectr(G, W, R, C) fill_rect((G), (W), (R).x, (R).y, (R).w, (R).h, (C))
#define draw_rounded_rectr(G, W, R, S, C) draw_rounded_rect((G), (W), (R).x, (R).y, (R).w, (R).h, (S), (C))
#define fill_rounded_rectr(G, W, R, S, C) fill_rounded_rect((G), (W), (R).x, (R).y, (R).w, (R).h, (S), (C))
