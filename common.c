#include "common.h"
#include <stdio.h>
#include <stdlib.h>

u8 *read_entire_file(const char *file_name, size_t *size)
{
    *size = 0;
    u8 * bf = NULL;
    FILE *fp = fopen(file_name, "rb");
    if (!fp) { return NULL; }
    fseek(fp, 0, SEEK_END);
    i32 sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz == -1)
    {
        fclose(fp);
        return NULL;
    }
    bf = (u8*)calloc(sz + 1, 1);
    if (!bf)
    {
        fclose(fp);
        return NULL;
    }
    fread(bf, 1, sz, fp);
    *size = sz;
    return bf;
}

int point_in_rect(vec2i point, const rect_t *rect)
{
    return point.x >= rect->x && point.x < rect->x + rect->w &&
           point.y >= rect->y && point.y < rect->y + rect->h;
}

int fpoint_in_rect(vec2f point, const rect_t *rect)
{
    return point.x >= rect->x && point.x < rect->x + rect->w &&
           point.y >= rect->y && point.y < rect->y + rect->h;
}

void world_to_screeni(const world_t *w, int wx, int wy, int *sx, int *sy)
{
    *sx = (int)((wx - w->offset.x) * w->scale);
    *sy = (int)((wy - w->offset.y) * w->scale);
}

void screen_to_worldi(const world_t *w, int sx, int sy, int *wx, int *wy)
{
    *wx = (float)(sx) / w->scale + w->offset.x;
    *wy = (float)(sy) / w->scale + w->offset.y;
}

void world_to_screenf(const world_t *w, float wx, float wy, float *sx, float *sy)
{
    *sx = (wx - w->offset.x) * w->scale;
    *sy = (wy - w->offset.y) * w->scale;
}

void screen_to_worldf(const world_t *w, float sx, float sy, float *wx, float *wy)
{
    *wx = sx / w->scale + w->offset.x;
    *wy = sy / w->scale + w->offset.y;
}
