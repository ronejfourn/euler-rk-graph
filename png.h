#pragma once

#include "common.h"
u8 *load_png_from_file(const char *file_name, u32 *w, u32 *h, u32 *ch);
u8 *load_png_from_memory(const u8 *buffer, size_t size, u32 *w, u32 *h, u32 *ch);
