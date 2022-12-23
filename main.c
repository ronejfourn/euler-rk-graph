#include <SDL2/SDL.h>
#include "graphics.h"
#include "events.h"
#include "mexp.h"

#define WHITE 0xffd4be98

static mexp_tree_t *expr;
static double f(double x, double y)
{
    double v[2] = {x, y};
    return mexp_eval_tree(expr, v);
}

double euler(double x0, double y0, double h)
{
    return y0 + h * f(x0, y0);
}

double rk2(double x0, double y0, double h)
{
    double k1 = h * f(x0, y0);
    double k2 = h * f(x0 + h, y0 + k1);
    double k  = (k1 + k2) / 2;
    return y0 + k;
}

double rk4(double x0, double y0, double h)
{
    double k1 = h * f(x0, y0);
    double k2 = h * f(x0 + h / 2, y0 + k1 / 2);
    double k3 = h * f(x0 + h / 2, y0 + k2 / 2);
    double k4 = h * f(x0 + h, y0 + k3);
    double k  = (k1 + 2 * k2 + 2 * k3 + k4) / 6;
    return y0 + k;
}

#define MAX_LENGTH 50
#define WINDOW_W 1280
#define WINDOW_H  720
static const u32 eul_color = 0xffea6962;
static const u32 rk2_color = 0xffa9b665;
static const u32 rk4_color = 0xff7daea3;
static void draw_screen_texture(graphics_t *graphics, SDL_Texture *scr_tex, const string_t *input_text);

int main()
{
    const double h = 0.01;
    const double x0 = 0, y0 = 1, x1 = 10;
    const long pt_count = (long)((x1 - x0) / h);

    world_t world;
    events_t events;
    graphics_t graphics;
    SDL_Window *window = NULL;
    SDL_Event event;
    int run = 0;
    char input_buffer[MAX_LENGTH + 1];
    string_t input_text = {input_buffer, 0};
    SDL_Texture *scr_tex;
    SDL_Rect scr_tex_rect = {0, 0, WINDOW_W, 200};
    SDL_Renderer *renderer;
    mexp_parser_t parser;
    mexp_tree_t tree;
    int draw_plot = 0;
    vec2f eul_pts[pt_count];
    vec2f rk2_pts[pt_count];
    vec2f rk4_pts[pt_count];

    if (SDL_Init(SDL_INIT_TIMER | SDL_INIT_VIDEO | SDL_INIT_EVENTS))
        return 0;

    window = SDL_CreateWindow("plot", 0, 0, WINDOW_W, WINDOW_H, SDL_WINDOW_SHOWN);
    if (!window) return 1;
    if (!init_graphics(window, &graphics) || !init_events(&events)) return 1;
    if (!mexp_init_parser(&parser)) return 1;
    if (!mexp_init_tree(&tree)) return 1;

    mexp_add_variable(&parser, 'x');
    mexp_add_variable(&parser, 'y');

    renderer = graphics.renderer;
    SDL_RenderSetLogicalSize(renderer, WINDOW_W, WINDOW_H);

    world.scale = 100.0f;
    world.offset.x = (-WINDOW_W / 2.0f) / world.scale;
    world.offset.y = (-WINDOW_H / 2.0f) / world.scale;

    scr_tex = SDL_CreateTexture(graphics.renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, scr_tex_rect.w, scr_tex_rect.h);
    SDL_SetTextureBlendMode(scr_tex, SDL_BLENDMODE_BLEND);
    if (!scr_tex) {return 1;}

    run = 1;
    draw_screen_texture(&graphics, scr_tex, &input_text);

    while (run)
    {
        u32 begin = SDL_GetTicks();
        int render_text = 0;
        advance_events(&events);
        while (SDL_PollEvent(&event))
        {
            handle_events(&event, &events);
            if (event.type == SDL_QUIT)
                run = 0;
            if (event.type == SDL_TEXTINPUT)
            {
                int len = strlen(event.text.text);
                if (len < MAX_LENGTH - input_text.length)
                {
                    memcpy(input_buffer + input_text.length, event.text.text, len);
                    input_text.length += len;
                    render_text = 1;
                }
            }
        }

        handle_zoom_and_pan(&world, &events);

        if (key_pressed(&events, SDL_SCANCODE_RETURN))
        {
            draw_plot = mexp_generate_tree(&tree, &parser, input_text.data, input_text.length);
            if (draw_plot)
            {
                double x = x0, yeul = y0, yrk2 = y0, yrk4 = y0;
                expr = &tree;
                for (int i = 0; i < pt_count; i ++)
                {
                    eul_pts[i].x = x;
                    rk2_pts[i].x = x;
                    rk4_pts[i].x = x;
                    eul_pts[i].y = -yeul;
                    rk2_pts[i].y = -yrk2;
                    rk4_pts[i].y = -yrk4;
                    yeul = euler(x, yeul, h);
                    yrk2 = rk2(x, yrk2, h);
                    yrk4 = rk4(x, yrk4, h);
                    x += h;
                }
                draw_plot = 1;
            }
        }

        if (key_pressed(&events, SDL_SCANCODE_BACKSPACE))
        {
            if (input_text.length > 0)
                input_buffer[--input_text.length] = 0;
            render_text = 1;
        }

        if (render_text)
            draw_screen_texture(&graphics, scr_tex, &input_text);

        SDL_SetRenderDrawColor(renderer, 0x28, 0x28, 0x28, 0);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, scr_tex, &scr_tex_rect, &scr_tex_rect);

        for (int i = 0; i < 10; i ++)
        {
            char num[1] = {i + '0'};
            string_t t = {num, 1};
            draw_text(&graphics, &world, i, 0.05f, 0.01f, &t, WHITE);
        }

        draw_line(&graphics, &world, -1000, 0, 1000, 0, WHITE);
        draw_line(&graphics, &world, 0, -1000, 0, 1000, WHITE);

        if (draw_plot)
        {
            for (int i = 1; i < pt_count; i ++)
            {
                draw_line(&graphics, &world, eul_pts[i - 1].x, eul_pts[i - 1].y, eul_pts[i].x, eul_pts[i].y, eul_color);
                draw_line(&graphics, &world, rk2_pts[i - 1].x, rk2_pts[i - 1].y, rk2_pts[i].x, rk2_pts[i].y, rk2_color);
                draw_line(&graphics, &world, rk4_pts[i - 1].x, rk4_pts[i - 1].y, rk4_pts[i].x, rk4_pts[i].y, rk4_color);
            }
        }

        SDL_RenderPresent(renderer);
        const u32 delay = 16;
        u32 diff = SDL_GetTicks() - begin;
        if (diff < delay) SDL_Delay(delay - diff);
    }

    destroy_graphics(&graphics);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

void draw_screen_texture(graphics_t *graphics, SDL_Texture *scr_tex, const string_t *input_text)
{
    static const string_t prompt   = {"dy/dx = ", 8};
    static const string_t eul_text = {"EULER", 5};
    static const string_t rk2_text = {"RK2", 3};
    static const string_t rk4_text = {"RK4", 3};
    rect_t prompt_rect = get_text_rect(graphics, &prompt);
    prompt_rect.x = 20;
    prompt_rect.y = 20;

    rect_t eul_rect = get_text_rect(graphics, &eul_text);
    rect_t rk2_rect = get_text_rect(graphics, &rk2_text);
    rect_t rk4_rect = get_text_rect(graphics, &rk4_text);
    vec2i input_draw_pos = {prompt_rect.x + prompt_rect.w, prompt_rect.y};

    int yoffs = eul_rect.h;
    SDL_Renderer *renderer = graphics->renderer;
    SDL_SetRenderTarget(renderer, scr_tex);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
    sdraw_text(graphics, input_draw_pos.x, input_draw_pos.y, input_text, WHITE);
    sdraw_text(graphics, prompt_rect.x, prompt_rect.y, &prompt, WHITE);
    sdraw_text(graphics, WINDOW_W - eul_rect.w - 20, yoffs * 0 + 20, &eul_text, eul_color);
    sdraw_text(graphics, WINDOW_W - rk2_rect.w - 20, yoffs * 1 + 20, &rk2_text, rk2_color);
    sdraw_text(graphics, WINDOW_W - rk4_rect.w - 20, yoffs * 2 + 20, &rk4_text, rk4_color);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderTarget(renderer, NULL);
}
