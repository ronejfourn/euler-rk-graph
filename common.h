#pragma once

#include <stddef.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>

typedef int8_t  i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef struct {i32 x, y;} vec2i;
#define vec2i_fmt "%d, %d"
#define vec2i_arg(v) (v).x, (v).y
typedef struct {float x, y;} vec2f;
#define vec2f_fmt "%f, %f"
#define vec2f_arg(v) (v).x, (v).y
typedef struct {double x, y;} vec2d;
#define vec2d_fmt "%lf, %lf"
#define vec2d_arg(v) (v).x, (v).y
typedef struct {const char *data; u32 length;} string_t;
#define string_fmt "%.*s"
#define string_arg(s) (s).length, (s).data

typedef struct world_t
{
    vec2d offset;
    vec2i pan;
    double scale;
}
world_t;

typedef struct { int x, y, w, h; } rect_t;

int point_in_rect (vec2i point, const rect_t *rect);
int fpoint_in_rect(vec2f point, const rect_t *rect);
u8 *read_entire_file(const char *file_name, size_t *size);
void world_to_screeni(const world_t *w, int wx, int wy, int *sx, int *sy);
void screen_to_worldi(const world_t *w, int sx, int sy, int *wx, int *wy);
void world_to_screenf(const world_t *w, float wx, float wy, float *sx, float *sy);
void screen_to_worldf(const world_t *w, float sx, float sy, float *wx, float *wy);
void world_to_screend(const world_t *w, double wx, double wy, double *sx, double *sy);
void screen_to_worldd(const world_t *w, double sx, double sy, double *wx, double *wy);
