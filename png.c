#include "png.h"
#include <stdlib.h>

#define MAX_HLIT 286
#define MAX_HDIST 32
#define MAX_CODE_LENGTH 15
#define MAX_IDAT_CHUNK_COUNT 8192

typedef struct bytebuffer_t
{
    struct bytebuffer_t *next;
    u8 *bytes;
    i32 bit_count;
    i32 byte_count;
}
bytebuffer_t;

typedef struct
{
    u32 max_length, sym_count;
    u32 *symbols;
    u32 *lengths;
    u32 *counts;
}
huffman_t;

static void  flush_byte(bytebuffer_t *lb);
static u32   get_bits(bytebuffer_t *lb, u8 n);
static void *get_bytes(bytebuffer_t *lb, i32 n, u8 *bytes, i32 m);
static i32   check_signature(bytebuffer_t *lb);
static u32   correct_endian(u32 a);
static void  huffman_construct(huffman_t *h);
static u32   huffman_decode(bytebuffer_t *bb, huffman_t *h);
static u8    paeth_predictor(i16 a, i16 b, i16 c);
static u8    reverse_filter(u8 *pixels, u8 *decompressed, u32 decomp_size, u32 bpp, u32 width);
static int   fixed_huffman(u32 *litlendist_lengths, huffman_t *litlen_huffman, huffman_t *dist_huffman);
static int   dynamic_huffman(bytebuffer_t *idat, u32 *litlendist_lengths, huffman_t *litlen_huffman, huffman_t *dist_huffman);
static void  decompress_using_huffman(u8 *decompressed, u32 *idx, bytebuffer_t *idat, huffman_t *litlen_huffman, huffman_t *dist_huffman);
#define      get_type(lb, T, buf, m) *(T*)get_bytes(lb, sizeof(T), buf, m)

//
//
//

u8 *load_png_from_file(const char *file_name, u32 *w, u32 *h, u32 *ch)
{
    size_t size = 0;
    u8 *buffer = read_entire_file(file_name, &size);
    if (!buffer) return NULL;
    return load_png_from_memory(buffer, size, w, h, ch);
}

u8 *load_png_from_memory(const u8 *buffer, size_t size, u32 *w, u32 *h, u32 *ch)
{
    *w = 0, *h = 0, *ch = 0;
    bytebuffer_t bbuf;
    bbuf.bytes = (u8 *)buffer;
    bbuf.byte_count = size;
    bbuf.bit_count  = 8;
    bbuf.next = NULL;

    if (!check_signature(&bbuf)) return NULL;

    struct
    {
        u32 w, h;
        u8 bit_depth, color_type, comp_method;
        u8 filt_method, lace_method;
    } ihdr = {0,0,0,0,0,0,0};

    struct
    {
        u32 type, crc;
        bytebuffer_t data;
    }
    chunk;

    u8 bytes[64];

    bytebuffer_t idat_chunks[MAX_IDAT_CHUNK_COUNT];
    memset(idat_chunks, 0, sizeof(idat_chunks));
    u32 idat_count = 0;
    bytebuffer_t idat_head = {NULL, NULL, 0, 0};
    bytebuffer_t *idat_tail = &idat_head;

    while (bbuf.byte_count)
    {
        chunk.data.bit_count  = 8;
        chunk.data.byte_count = get_type(&bbuf, u32, bytes, sizeof(bytes));
        chunk.data.byte_count = correct_endian(chunk.data.byte_count);
        chunk.type = get_type(&bbuf, u32, bytes, sizeof(bytes));
        chunk.data.bytes = bbuf.bytes;
        bbuf.bytes += chunk.data.byte_count;
        bbuf.byte_count -= chunk.data.byte_count;
        chunk.data.next = NULL;
        chunk.crc  = get_type(&bbuf, u32, bytes, sizeof(bytes));

        if (chunk.type == *(u32*)"IHDR")
        {
            ihdr.w = get_type(&chunk.data, u32, bytes, sizeof(bytes));
            ihdr.h = get_type(&chunk.data, u32, bytes, sizeof(bytes));
            ihdr.w = correct_endian(ihdr.w);
            ihdr.h = correct_endian(ihdr.h);
            ihdr.bit_depth   = get_type(&chunk.data, u8, bytes, sizeof(bytes));
            ihdr.color_type  = get_type(&chunk.data, u8, bytes, sizeof(bytes));
            ihdr.comp_method = get_type(&chunk.data, u8, bytes, sizeof(bytes));
            ihdr.filt_method = get_type(&chunk.data, u8, bytes, sizeof(bytes));
            ihdr.lace_method = get_type(&chunk.data, u8, bytes, sizeof(bytes));

            assert(ihdr.bit_depth   == 8);
            assert(ihdr.color_type  == 2 || ihdr.color_type == 6);
            assert(ihdr.comp_method == 0);
            assert(ihdr.filt_method == 0);
            assert(ihdr.lace_method == 0);
        }

        if (chunk.type == *(u32*)"IDAT")
        {
            assert(idat_count < MAX_IDAT_CHUNK_COUNT);
            idat_tail->next = &idat_chunks[idat_count ++];
            idat_tail = idat_tail->next;
            idat_tail->bytes = chunk.data.bytes;
            idat_tail->byte_count = chunk.data.byte_count;
            idat_tail->bit_count  = 8;
            idat_tail->next = NULL;
        }
    }

    if (idat_count == 0) return NULL;
    bytebuffer_t *idat = idat_head.next;
    u8 cmf = get_type(idat, u8, bytes, sizeof(bytes));
    u8 flg = get_type(idat, u8, bytes, sizeof(bytes)); (void) flg;
    u8 cm  = (cmf & ((1 << 4) - 1));
    assert(cm == 8);

    u32 bfinal, btype;
    u32 idx = 0;
    u32 bpp = (ihdr.color_type == 2 ? 3 : 4);
    u32 decomp_size  = ihdr.w * ihdr.h * bpp + ihdr.h;
    u8 *decompressed = (u8*)calloc(decomp_size, 1);
    if (!decompressed) return NULL;

    u32 litlendist_lengths[MAX_HLIT + MAX_HDIST];
    u32 litlen_syms[MAX_HLIT], litlen_counts[MAX_CODE_LENGTH + 1];
    u32 dist_syms[MAX_HDIST], dist_counts[MAX_CODE_LENGTH + 1];

    huffman_t litlen_huffman;
    litlen_huffman.symbols = litlen_syms;
    litlen_huffman.counts  = litlen_counts;

    huffman_t dist_huffman;
    dist_huffman.symbols = dist_syms;
    dist_huffman.counts  = dist_counts;

    do
    {
        bfinal = get_bits(idat, 1);
        btype  = get_bits(idat, 2);

        if (btype == 0)
        {
            // no compression
            flush_byte(idat);
            u16 len   = get_type(idat, u16, bytes, sizeof(bytes));
            u16 nlen  = get_type(idat, u16, bytes, sizeof(bytes)); (void)nlen;
            void *dat = NULL;
            while (len > 0)
            {
                u16 inc = 0;
                if (len > sizeof(bytes))
                {
                    inc = sizeof(bytes);
                    len -= inc;
                }
                else
                {
                    inc = len;
                    len = 0;
                }
                dat = get_bytes(idat, inc, bytes, sizeof(bytes));
                memcpy(decompressed + idx, dat, inc);
                idx += inc;
            }
            continue;
        }

        if (btype == 3)
        {
            free(decompressed);
            return NULL;
        }

        int ok = (btype == 1) ?
            fixed_huffman  (litlendist_lengths, &litlen_huffman, &dist_huffman) :
            dynamic_huffman(idat, litlendist_lengths, &litlen_huffman, &dist_huffman);

        if (!ok)
        {
            free(decompressed);
            return NULL;
        }

        huffman_construct(&litlen_huffman);
        huffman_construct(&dist_huffman);
        decompress_using_huffman(decompressed, &idx, idat, &litlen_huffman, &dist_huffman);
    } while (!bfinal);

    u32 alder32 = get_type(idat, u32, bytes, sizeof(bytes)); (void)alder32; // ignored
    u8 *pixels = (u8 *)calloc(1, bpp * ihdr.w * ihdr.h);
    if (!pixels)
    {
        free(decompressed);
        return NULL;
    }

    if (!reverse_filter(pixels, decompressed, decomp_size, bpp, ihdr.w))
    {
        free(pixels);
        free(decompressed);
        return NULL;
    }

    free(decompressed);

    *w = ihdr.w, *h = ihdr.h, *ch = bpp;
    return pixels;
}

//
//
//

int fixed_huffman(u32 *litlendist_lengths, huffman_t *litlen_huffman, huffman_t *dist_huffman)
{
#define FIXED_MAX_LIT_VALUE 288
#define FIXED_MAX_DIST_VALUE 31
    i32 i = 0;
    for (; i < FIXED_MAX_LIT_VALUE; i ++)
    {
        u32 v = 0;
        if (i <= 143) v = 8;
        else if (i <= 255) v = 9;
        else if (i <= 279) v = 7;
        else v = 8;
        litlendist_lengths[i] = v;
    }

    for (; i < MAX_HLIT + MAX_HDIST; i++)
        litlendist_lengths[i] = 5;

    litlen_huffman->lengths    = litlendist_lengths;
    litlen_huffman->sym_count  = FIXED_MAX_LIT_VALUE;
    litlen_huffman->max_length = MAX_CODE_LENGTH;

    dist_huffman->lengths    = litlendist_lengths + FIXED_MAX_LIT_VALUE;
    dist_huffman->sym_count  = FIXED_MAX_DIST_VALUE;
    dist_huffman->max_length = MAX_CODE_LENGTH;

    return 1;
#undef FIXED_MAX_LIT_VALUE
#undef FIXED_MAX_DIST_VALUE
}

int dynamic_huffman(bytebuffer_t *idat, u32 *litlendist_lengths, huffman_t *litlen_huffman, huffman_t *dist_huffman)
{
#define MAX_HCLEN 19
#define MAX_CL_CODE_LENGTH 7
    u32 hlit  = get_bits(idat, 5) + 257;
    u32 hdist = get_bits(idat, 5) + 1;
    u32 hclen = get_bits(idat, 4) + 4;

    u8  order[MAX_HCLEN] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    u32 lengths[MAX_HCLEN], symbols[19], counts[MAX_CL_CODE_LENGTH + 1];
    memset(lengths, 0, sizeof(lengths));

    for (u32 i = 0; i < hclen; i ++)
        lengths[order[i]] = get_bits(idat, 3);

    huffman_t codelen_huffman;
    codelen_huffman.lengths = lengths;
    codelen_huffman.symbols = symbols;
    codelen_huffman.counts  = counts;
    codelen_huffman.max_length = MAX_CL_CODE_LENGTH;
    codelen_huffman.sym_count  = MAX_HCLEN;
    huffman_construct(&codelen_huffman);

    u32 index = 0;
    while (index < hlit + hdist)
    {
        i32 symbol = huffman_decode(idat, &codelen_huffman);

        u32 rep = 0;
        if (symbol <= 15)
        {
            rep = 1;
        }
        else if (symbol == 16)
        {
            symbol = litlendist_lengths[index - 1];
            rep = get_bits(idat, 2) + 3;
        }
        else if (symbol == 17)
        {
            symbol = 0;
            rep = get_bits(idat, 3) + 3;
        }
        else if (symbol == 18)
        {
            symbol = 0;
            rep = get_bits(idat, 7) + 11;
        }
        else return 0;

        while (rep --)
            litlendist_lengths[index ++] = symbol;
    }

    litlen_huffman->lengths    = litlendist_lengths;
    litlen_huffman->max_length = MAX_CODE_LENGTH;
    litlen_huffman->sym_count  = hlit;

    dist_huffman->lengths    = litlendist_lengths + hlit;
    dist_huffman->max_length = MAX_CODE_LENGTH;
    dist_huffman->sym_count  = hdist;
    return 1;
#undef MAX_HCLEN
#undef MAX_CL_CODE_LENGTH
}

void decompress_using_huffman(u8 *decompressed, u32 *idx, bytebuffer_t *idat, huffman_t *litlen_huffman, huffman_t *dist_huffman)
{
    const u16 len_base [] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258, };
    const u8 len_extra [] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0, };
    const u16 dist_base[] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577, };
    const u8 dist_extra[] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, };

    for (;;)
    {
        /* decode literal/length value from input stream */
        u32 value = huffman_decode(idat, litlen_huffman);

        if (value < 256)
        {
            /* copy value (literal byte) to output stream */
            decompressed[(*idx)++] = value;
            continue;
        }

        if (value == 256) /* end of block */
            break;

        assert (value >= 257 && value <= 285);
        u16 len_index = value - 257;
        u32 len = len_base[len_index];
        u8 extra_bits = len_extra[len_index];
        len += get_bits(idat, extra_bits);

        /* decode distance from input stream */
        u32 dist_index = huffman_decode(idat, dist_huffman);
        u32 dist = dist_base[dist_index];
        extra_bits = dist_extra[dist_index];
        dist += get_bits(idat, extra_bits);

        /* move backwards distance bytes in the output */
        u8 *src = decompressed + *idx - dist;
        while (len --)
        {
            /* stream, and copy length bytes from this */
            /* position to the output stream. */
            decompressed[(*idx)++] = *src;
            src++;
        }
    }
}

u8 reverse_filter(u8 *pixels, u8 *decompressed, u32 decomp_size, u32 bpp, u32 width)
{
    u8 *filt = NULL, type = 0;
    u32 scanline_len = bpp * width;

    u8 *recon = pixels, *prev = NULL;

    enum { ftNone = 0, ftSub, ftUp, ftAverage, ftPaeth };
    u32 decompline_len = scanline_len + 1;

    type = *decompressed;
    filt = decompressed + 1;

    if (type == ftNone || type == ftUp)
        memcpy(recon, filt, scanline_len);
    else if (type == ftSub || type == ftPaeth)
    {
        for (u32 x = 0; x < bpp; x ++)
            recon[x] = filt[x];
        for (u32 x = bpp; x < scanline_len; x ++)
            recon[x] = filt[x] + recon[x - bpp];
    }
    else if (type == ftAverage)
    {
        u8 a = 0;
        for (u32 x = 0; x < bpp; x ++)
            recon[x] = filt[x];
        for (u32 x = bpp; x < scanline_len; x ++)
        {
            a = recon[x - bpp];
            recon[x] = filt[x] + a / 2;
        }
    }
    else { return 0; }

    prev = recon;
    recon += scanline_len;

    for (u32 i = decompline_len; i < decomp_size; i += decompline_len)
    {
        type = *(decompressed + i);
        filt = decompressed + i + 1;

        if (type == ftNone)
            memcpy(recon, filt, scanline_len);
        else if (type == ftSub)
        {
            for (u32 x = 0; x < bpp; x ++)
                recon[x] = filt[x];
            for (u32 x = bpp; x < scanline_len; x ++)
                recon[x] = filt[x] + recon[x - bpp];
        }
        else if (type == ftUp)
        {
            for (u32 x = 0; x < scanline_len; x ++)
                recon[x] = filt[x] + prev[x];
        }
        else if (type == ftAverage)
        {
            u8 a, b;
            a = 0;
            for (u32 x = 0; x < bpp; x ++)
            {
                b = prev[x];
                recon[x] = filt[x] + (a + b) / 2;
            }
            for (u32 x = bpp; x < scanline_len; x ++)
            {
                a = recon[x - bpp];
                b = prev[x];
                recon[x] = filt[x] + (a + b) / 2;
            }
        }
        else if (type == ftPaeth)
        {
            u8 a, b, c;
            a = 0;
            c = 0;
            for (u32 x = 0; x < bpp; x ++)
            {
                b = prev[x];
                recon[x] = filt[x] + paeth_predictor(a, b, c);
            }
            for (u32 x = bpp; x < scanline_len; x ++)
            {
                a = recon[x - bpp];
                b = prev[x];
                c = prev[x - bpp];
                recon[x] = filt[x] + paeth_predictor(a, b, c);
            }
        }
        else { return 0; }

        prev = recon;
        recon += scanline_len;
    }

    return 1;
}

u8 paeth_predictor(i16 a, i16 b, i16 c)
{
#define ABS(c) ((c) < 0 ? -(c) : (c))
    i16 p = a + b - c;
    i16 pa = ABS(p - a);
    i16 pb = ABS(p - b);
    i16 pc = ABS(p - c);
    u8 Pr;
    if (pa <= pb && pa <= pc) Pr = a;
    else if (pb <= pc) Pr = b;
    else Pr = c;
    return Pr;
#undef ABS
}

i32 check_signature(bytebuffer_t *lb)
{
    u8 sig[8] = {0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A};
    u8 buf[8];
    return *(uint64_t*)sig == *(uint64_t*)get_bytes(lb, 8, buf, 8);
}

u32 correct_endian(u32 a)
{
#define BSWAP(a) (a << 24) | ((a & 0xff00) << 8) | ((a >> 8) & 0xff00) | (a >> 24)
    i32 x = 1;
    char k = *(char *)&x;
    if (k) return BSWAP(a);
    else return a;
#undef BSWAP
}

void flush_byte(bytebuffer_t *lb)
{
    if (lb->byte_count > 0)
    {
        lb->bytes ++;
        lb->byte_count --;
        lb->bit_count = 8;
    }
    else if (lb->next != NULL)
    {
        lb->bit_count  = 8;
        lb->byte_count = lb->next->byte_count;
        lb->bytes = lb->next->bytes;
        lb->next  = lb->next->next;
    }
    else lb->bit_count = 0;
}

void *get_bytes(bytebuffer_t *lb, i32 n, u8 *bytes, i32 m)
{
    memset(bytes, 0, m);
    assert (n <= m);
    if (lb->bit_count != 8)
        flush_byte(lb);

    u32 i = 0;
    while (n > 0)
    {
        if (n > lb->byte_count)
        {
            memcpy(bytes + i, lb->bytes, lb->byte_count);
            i += lb->byte_count;
            n -= lb->byte_count;
            lb->byte_count = 0;
            lb->bit_count  = 0;
            lb->bytes = NULL;
            if (lb->next != NULL)
            {
                lb->bytes = lb->next->bytes;
                lb->byte_count = lb->next->byte_count;
                lb->bit_count  = 8;
                lb->next = lb->next->next;
            }
        }
        else
        {
            memcpy(bytes + i, lb->bytes, n);
            lb->bytes += n;
            lb->byte_count -= n;
            i += n;
            n -= n;
        }
    }

    return bytes;
}

u32 get_bits(bytebuffer_t *lb, u8 n)
{
    assert(n <= 32);
    u32 ret = 0, i = 0;

    while (n > 0)
    {
        if (lb->byte_count <= 0)
        {
            if (lb->next == NULL)
                break;
            bytebuffer_t *nb = lb->next;
            lb->byte_count = nb->byte_count;
            lb->bytes = nb->bytes;
            lb->next = nb->next;
            lb->bit_count = 8;
        }

        u8 bits = *lb->bytes >> (8 - lb->bit_count);
        if (n > lb->bit_count)
        {
            ret |= bits << i;
            i += lb->bit_count;
            n -= lb->bit_count;

            lb->bytes ++;
            lb->byte_count --;
            lb->bit_count = 8;
        }
        else if (lb->bit_count > 0)
        {
            bits &= (1 << n) - 1;
            ret |= bits << i;
            lb->bit_count -= n;
            i += n;
            n -= n;
        }
        else break;
    }

    return ret;
}

void huffman_construct(huffman_t *h)
{
    u32 base[MAX_CODE_LENGTH + 1];
    memset(h->counts , 0, sizeof(u32) * (h->max_length + 1));
    for (u32 i = 0; i < h->sym_count; i ++)
        h->counts[h->lengths[i]]++;
    h->counts[0] = 0;

    i32 acc = 0;
    for (u32 i = 0; i <= h->max_length; i++)
    {
        base[i] = acc;
        acc += h->counts[i];
    }

    for (u32 i = 0; i < h->sym_count; i++)
    {
        i32 len = h->lengths[i];
        if (len) h->symbols[base[len]++] = i;
    }
}

u32 huffman_decode(bytebuffer_t *bb, huffman_t *h)
{
    u32 code = 0, base = 0, first = 0;
    for (u32 len = 1; len <= h->max_length; len++)
    {
        first = (first + h->counts[len - 1]) << 1;
        code |= get_bits(bb, 1);
        i32 count = h->counts[len];
        if (count)
        {
            i32 off = code - first;
            if (off >= 0 && off < count)
                return h->symbols[base + off];
            base += h->counts[len];
        }
        code <<= 1;
    }

    return -1;
}
