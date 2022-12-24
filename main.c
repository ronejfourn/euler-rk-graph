#include <SDL2/SDL.h>
#include "graphics.h"
#include "events.h"
#include "mexp.h"

#ifdef PF_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

#define MAX_LENGTH 256
#define DEFAULT_WINDOW_WIDTH 1280
#define DEFAULT_WINDOW_HEIGHT 720
#define WHITE  0xffd4be98
#define RED    0xffea6962
#define GREEN  0xffa9b665
#define YELLOW 0xffd8a657
#define BLUE   0xff7daea3
static const u32 eul_color = BLUE;
static const u32 rk2_color = GREEN;
static const u32 rk4_color = YELLOW;

static void redraw_static_texture(graphics_t *graphics, SDL_Texture *static_texture, vec2i *geometry, const string_t *prompt, const rect_t *prompt_rect);

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

int main()
{
    const double h = 0.01;
    const double x0 = 0, y0 = 1, x1 = 10;
    const size_t pt_count = (size_t)((x1 - x0) / h);

    vec2i geometry = {DEFAULT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT};
    world_t world;
    events_t events;
    graphics_t graphics;

    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Event event;
    SDL_Texture *static_texture;
    SDL_Rect static_tex_rect = {0, 0, geometry.x, geometry.y};

    char input_buffer[MAX_LENGTH + 1];
    string_t input_text = {input_buffer, 0};
    SDL_Texture *input_texture;
    SDL_Rect input_src_rect = {0, 0, 0, 0}, input_dst_rect = {0, 0, 0, 0};

    static const string_t prompt = {"dy/dx = ", 8};
    rect_t prompt_rect;

    mexp_parser_t parser;
    mexp_tree_t tree;

    // apparently const is not constant expression
    // why windows why ;-;
    // vec2d eul_pts[pt_count], rk2_pts[pt_count], rk4_pts[pt_count];

    vec2d* eul_pts = calloc(pt_count, sizeof(vec2d));
    vec2d* rk2_pts = calloc(pt_count, sizeof(vec2d));
    vec2d* rk4_pts = calloc(pt_count, sizeof(vec2d));

    int run = 0;
    int draw_plot = 0;

    struct { float top, bottom, left, right; } world_bounds;

#ifdef PF_WINDOWS
    SetProcessDPIAware();
#endif

    if (SDL_Init(SDL_INIT_TIMER | SDL_INIT_VIDEO | SDL_INIT_EVENTS))
        return 1;

    window = SDL_CreateWindow("plot", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, geometry.x, geometry.y, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window) return 1;
    if (!init_graphics(window, &graphics) || !init_events(&events)) return 1;
    if (!mexp_init_parser(&parser)) return 1;
    if (!mexp_init_tree(&tree)) return 1;

    mexp_add_variable(&parser, 'x');
    mexp_add_variable(&parser, 'y');

    renderer = graphics.renderer;
    SDL_RenderSetLogicalSize(renderer, geometry.x, geometry.y);

    world.scale = 100.0f;
    world.offset.x = (-geometry.x / 2.0f) / world.scale;
    world.offset.y = (-geometry.y / 2.0f) / world.scale;

    static_texture = SDL_CreateTexture(graphics.renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, geometry.x, geometry.y);
    SDL_SetTextureBlendMode(static_texture, SDL_BLENDMODE_BLEND);
    if (!static_texture) {return 1;}

    prompt_rect = get_text_rect(&graphics, &prompt);
    prompt_rect.x = 20;
    prompt_rect.y = 20;

    {
        string_t dum = {NULL, MAX_LENGTH};
        rect_t dum_rect = get_text_rect(&graphics, &dum);
        input_src_rect.w = dum_rect.w;
        input_src_rect.h = dum_rect.h;
    }

    input_texture = SDL_CreateTexture(graphics.renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, input_src_rect.w, input_src_rect.h);
    SDL_SetTextureBlendMode(input_texture, SDL_BLENDMODE_BLEND);
    if (!input_texture) {return 1;}

    input_dst_rect.x = prompt_rect.x + prompt_rect.w;
    input_dst_rect.y = prompt_rect.y;
    input_dst_rect.w = input_src_rect.w;
    input_dst_rect.h = input_src_rect.h;

    redraw_static_texture(&graphics, static_texture, &geometry, &prompt, &prompt_rect);
    screen_to_worldf(&world, 0, 0, &world_bounds.left, &world_bounds.top);
    screen_to_worldf(&world, geometry.x, geometry.y, &world_bounds.right, &world_bounds.bottom);

    run = 1;
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
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
            {
                geometry.x = event.window.data1;
                geometry.y = event.window.data2;
                SDL_RenderSetLogicalSize(renderer, geometry.x, geometry.y);
                SDL_DestroyTexture(static_texture);
                static_texture = SDL_CreateTexture(graphics.renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, geometry.x, geometry.y);
                SDL_SetTextureBlendMode(static_texture, SDL_BLENDMODE_BLEND);
                if (!static_texture) {return 1;}
                static_tex_rect.w = geometry.x;
                static_tex_rect.h = geometry.y;
                redraw_static_texture(&graphics, static_texture, &geometry, &prompt, &prompt_rect);
                screen_to_worldf(&world, 0, 0, &world_bounds.left, &world_bounds.top);
                screen_to_worldf(&world, geometry.x, geometry.y, &world_bounds.right, &world_bounds.bottom);
            }
        }

        if (handle_zoom_and_pan(&world, &events))
        {
            screen_to_worldf(&world, 0, 0, &world_bounds.left, &world_bounds.top);
            screen_to_worldf(&world, geometry.x, geometry.y, &world_bounds.right, &world_bounds.bottom);
        }

        if (key_pressed(&events, SDL_SCANCODE_R) && (events.mods & MOD_CTRL))
        {
            world.scale = 100.0f;
            world.offset.x = (-geometry.x / 2.0f) / world.scale;
            world.offset.y = (-geometry.y / 2.0f) / world.scale;
            screen_to_worldf(&world, 0, 0, &world_bounds.left, &world_bounds.top);
            screen_to_worldf(&world, geometry.x, geometry.y, &world_bounds.right, &world_bounds.bottom);
        }

        if (key_pressed(&events, SDL_SCANCODE_RETURN))
        {
            draw_plot = mexp_generate_tree(&tree, &parser, input_text.data, input_text.length);
            redraw_static_texture(&graphics, static_texture, &geometry, &prompt, &prompt_rect);
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
            }
            else
            {
                string_t error;
                error.data   = mexp_get_error(&parser);
                error.length = strlen(error.data);
                SDL_SetRenderTarget(graphics.renderer, static_texture);
                SDL_SetRenderDrawBlendMode(graphics.renderer, SDL_BLENDMODE_BLEND);
                sdraw_text(&graphics, prompt_rect.x, prompt_rect.y + prompt_rect.h, &error, RED);
                SDL_SetRenderDrawBlendMode(graphics.renderer, SDL_BLENDMODE_NONE);
                SDL_SetRenderTarget(graphics.renderer, NULL);
            }
        }

        if (key_pressed(&events, SDL_SCANCODE_BACKSPACE))
        {
            if (input_text.length > 0)
                input_buffer[--input_text.length] = 0;
            render_text = 1;
        }

        if (render_text)
        {
            SDL_SetRenderTarget(renderer, input_texture);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
            SDL_RenderClear(renderer);
            sdraw_text(&graphics, 0, 0, &input_text, WHITE);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
            SDL_SetRenderTarget(renderer, NULL);
        }

        SDL_SetRenderDrawColor(renderer, 0x28, 0x28, 0x28, 0);
        SDL_RenderClear(renderer);

        draw_line(&graphics, &world, world_bounds.left, 0, world_bounds.right, 0, WHITE);
        draw_line(&graphics, &world, 0, world_bounds.top, 0, world_bounds.bottom, WHITE);

        if (draw_plot)
        {
            for (int i = 1; i < pt_count; i ++)
            {
                draw_line(&graphics, &world, eul_pts[i - 1].x, eul_pts[i - 1].y, eul_pts[i].x, eul_pts[i].y, eul_color);
                draw_line(&graphics, &world, rk2_pts[i - 1].x, rk2_pts[i - 1].y, rk2_pts[i].x, rk2_pts[i].y, rk2_color);
                draw_line(&graphics, &world, rk4_pts[i - 1].x, rk4_pts[i - 1].y, rk4_pts[i].x, rk4_pts[i].y, rk4_color);
            }
        }

        SDL_RenderCopy(renderer, static_texture, &static_tex_rect, &static_tex_rect);
        SDL_RenderCopy(renderer, input_texture , &input_src_rect, &input_dst_rect);

        {
            char buf[256];
            int len = snprintf(buf, 256, "%f, %f", events.cursor_world.x, -events.cursor_world.y);
            string_t str = {buf, len};
            rect_t rect = get_text_rect(&graphics, &str);
            sdraw_text(&graphics, geometry.x - rect.w - 20, geometry.y - rect.h - 20, &str, WHITE);
        }

        SDL_RenderPresent(renderer);
        const u32 delay = 16;
        u32 diff = SDL_GetTicks() - begin;
        if (diff < delay) SDL_Delay(delay - diff);
    }

    free(eul_pts);
    free(rk2_pts);
    free(rk4_pts);

    SDL_DestroyTexture(static_texture);
    SDL_DestroyTexture(input_texture);
    destroy_graphics(&graphics);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

void redraw_static_texture(graphics_t *graphics, SDL_Texture *static_texture, vec2i *geometry, const string_t *prompt, const rect_t *prompt_rect)
{
    static const string_t eul_text = {"EULER", 5};
    static const string_t rk2_text = {"RK2", 3};
    static const string_t rk4_text = {"RK4", 3};

    rect_t eul_rect = get_text_rect(graphics, &eul_text);
    rect_t rk2_rect = get_text_rect(graphics, &rk2_text);
    rect_t rk4_rect = get_text_rect(graphics, &rk4_text);

    int yoffs = eul_rect.h;
    SDL_SetRenderTarget(graphics->renderer, static_texture);
    SDL_SetRenderDrawBlendMode(graphics->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(graphics->renderer, 0, 0, 0, 0);
    SDL_RenderClear(graphics->renderer);
    sdraw_text(graphics, prompt_rect->x, prompt_rect->y, prompt, WHITE);
    sdraw_text(graphics, geometry->x - eul_rect.w - 20, yoffs * 0 + 20, &eul_text, eul_color);
    sdraw_text(graphics, geometry->x - rk2_rect.w - 20, yoffs * 1 + 20, &rk2_text, rk2_color);
    sdraw_text(graphics, geometry->x - rk4_rect.w - 20, yoffs * 2 + 20, &rk4_text, rk4_color);
    SDL_SetRenderDrawBlendMode(graphics->renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderTarget(graphics->renderer, NULL);
}
