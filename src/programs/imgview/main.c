#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "../../libgui/include/puregui.h"
#include "../../libgui/include/pguiw.h"
#include "../../libfs/include/purefs.h"
#include "../../libc/include/purec.h"

#define MAX_IMAGE_WIDTH  2048
#define MAX_IMAGE_HEIGHT 2048
#define MAX_IMAGE_FILE_SIZE (8 * 1024 * 1024)

struct image_data {
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    bool loaded;
    char path[128];
    uint32_t *pixels; // Allocated in 32-bit 0x00RRGGBB format
};

static struct image_data g_image;

static uint16_t read_u16_le(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_u32_le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int32_t read_i32_le(const uint8_t *p) {
    return (int32_t)read_u32_le(p);
}

static bool parse_bmp(const uint8_t *data, uint32_t size, struct image_data *img) {
    if (size < 54) return false;
    uint16_t magic = read_u16_le(data);
    if (magic != 0x4D42) return false; // 'BM'

    uint32_t data_offset = read_u32_le(data + 10);
    int32_t width = read_i32_le(data + 18);
    int32_t height = read_i32_le(data + 22);
    uint16_t bpp = read_u16_le(data + 28);
    uint32_t compression = read_u32_le(data + 30);

    if (width <= 0 || width > MAX_IMAGE_WIDTH) return false;
    bool top_down = false;
    if (height < 0) {
        top_down = true;
        height = -height;
    }
    if (height <= 0 || height > MAX_IMAGE_HEIGHT) return false;
    if (bpp != 24 && bpp != 32 && bpp != 8) return false;
    if (compression != 0 && compression != 3) return false; // BI_RGB or BI_BITFIELDS
    if (data_offset >= size) return false;

    uint32_t total_pixels = (uint32_t)width * (uint32_t)height;
    img->pixels = (uint32_t *)pc_heap_grow(total_pixels * sizeof(uint32_t));
    if (!img->pixels) return false;

    img->width = (uint32_t)width;
    img->height = (uint32_t)height;
    img->bpp = (uint32_t)bpp;

    uint32_t row_stride = 0;
    if (bpp == 24) row_stride = (width * 3 + 3) & ~3U;
    else if (bpp == 32) row_stride = width * 4;
    else if (bpp == 8) row_stride = (width + 3) & ~3U;

    const uint8_t *palette = data + 54;
    if (bpp == 8) {
        // Palette lives between header (54) and pixel data; guard indexes.
        if (data_offset < 54 || data_offset > size) return false;
        uint32_t palette_bytes = data_offset - 54;
        if (palette_bytes < 4) return false;
    }

    for (int32_t r = 0; r < height; r++) {
        int32_t y = top_down ? r : (height - 1 - r);
        uint64_t row_off = (uint64_t)data_offset + (uint64_t)r * row_stride;
        uint32_t px_bytes = (bpp == 24) ? (uint32_t)width * 3 : (bpp == 32) ? (uint32_t)width * 4 : (uint32_t)width;
        if (row_off + px_bytes > size) return false;
        const uint8_t *row = data + row_off;

        for (int32_t x = 0; x < width; x++) {
            uint32_t color = 0;
            if (bpp == 24) {
                uint8_t b = row[x * 3 + 0];
                uint8_t g = row[x * 3 + 1];
                uint8_t r_val = row[x * 3 + 2];
                color = ((uint32_t)r_val << 16) | ((uint32_t)g << 8) | (uint32_t)b;
            } else if (bpp == 32) {
                uint8_t b = row[x * 4 + 0];
                uint8_t g = row[x * 4 + 1];
                uint8_t r_val = row[x * 4 + 2];
                color = ((uint32_t)r_val << 16) | ((uint32_t)g << 8) | (uint32_t)b;
            } else if (bpp == 8) {
                uint8_t idx = row[x];
                uint64_t pal_off = (uint64_t)idx * 4 + 2;
                if (54 + pal_off >= data_offset) return false;
                uint8_t b = palette[idx * 4 + 0];
                uint8_t g = palette[idx * 4 + 1];
                uint8_t r_val = palette[idx * 4 + 2];
                color = ((uint32_t)r_val << 16) | ((uint32_t)g << 8) | (uint32_t)b;
            }
            img->pixels[y * width + x] = color;
        }
    }

    img->loaded = true;
    return true;
}

static bool parse_ppm(const uint8_t *data, uint32_t size, struct image_data *img) {
    if (size < 15 || data[0] != 'P' || data[1] != '6') return false;
    uint32_t idx = 2;
    while (idx < size && (data[idx] == ' ' || data[idx] == '\n' || data[idx] == '\r' || data[idx] == '\t')) idx++;
    if (idx >= size) return false;
    if (data[idx] == '#') {
        while (idx < size && data[idx] != '\n') idx++;
        idx++;
    }
    uint32_t w = 0;
    while (idx < size && data[idx] >= '0' && data[idx] <= '9') {
        w = w * 10 + (data[idx] - '0');
        idx++;
    }
    while (idx < size && (data[idx] == ' ' || data[idx] == '\n' || data[idx] == '\r' || data[idx] == '\t')) idx++;
    uint32_t h = 0;
    while (idx < size && data[idx] >= '0' && data[idx] <= '9') {
        h = h * 10 + (data[idx] - '0');
        idx++;
    }
    while (idx < size && (data[idx] == ' ' || data[idx] == '\n' || data[idx] == '\r' || data[idx] == '\t')) idx++;
    while (idx < size && data[idx] >= '0' && data[idx] <= '9') idx++; // maxval (255)
    if (idx < size && (data[idx] == ' ' || data[idx] == '\n' || data[idx] == '\r' || data[idx] == '\t')) idx++;

    if (w == 0 || w > MAX_IMAGE_WIDTH || h == 0 || h > MAX_IMAGE_HEIGHT) return false;

    img->pixels = (uint32_t *)pc_heap_grow(w * h * sizeof(uint32_t));
    if (!img->pixels) return false;

    img->width = w;
    img->height = h;
    img->bpp = 24;

    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            if (idx + 3 > size) return false;
            uint8_t r = data[idx++];
            uint8_t g = data[idx++];
            uint8_t b = data[idx++];
            img->pixels[y * w + x] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
    }

    img->loaded = true;
    return true;
}

static uint32_t read_u32_be(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

// ---- Minimal PNG decoder (8-bit, color types 0/2/6, non-interlaced) ----
struct png_bit_reader {
    const uint8_t *data;
    uint32_t size;
    uint32_t byte_pos;
    uint32_t bit_buf;
    uint32_t bit_count;
};

static void png_br_init(struct png_bit_reader *br, const uint8_t *data, uint32_t size) {
    br->data = data;
    br->size = size;
    br->byte_pos = 0;
    br->bit_buf = 0;
    br->bit_count = 0;
}

static bool png_br_fill(struct png_bit_reader *br, uint32_t need) {
    while (br->bit_count < need) {
        if (br->byte_pos >= br->size) return false;
        br->bit_buf |= (uint32_t)br->data[br->byte_pos++] << br->bit_count;
        br->bit_count += 8;
    }
    return true;
}

static bool png_br_bits(struct png_bit_reader *br, uint32_t count, uint32_t *out) {
    if (count == 0) { *out = 0; return true; }
    if (!png_br_fill(br, count)) return false;
    *out = br->bit_buf & (count >= 32 ? 0xFFFFFFFFu : ((1u << count) - 1u));
    br->bit_buf >>= count;
    br->bit_count -= count;
    return true;
}

static void png_br_align(struct png_bit_reader *br) {
    br->bit_buf = 0;
    br->bit_count = 0;
}

#define PNG_HUFF_NODES 1152

struct png_huff {
    int16_t left[PNG_HUFF_NODES];
    int16_t right[PNG_HUFF_NODES];
    int16_t symbol[PNG_HUFF_NODES];
    int16_t root;
    int node_count;
};

static void png_huff_init(struct png_huff *h) {
    for (int i = 0; i < PNG_HUFF_NODES; i++) {
        h->left[i] = -1;
        h->right[i] = -1;
        h->symbol[i] = -1;
    }
    h->root = 0;
    h->node_count = 1;
}

// Canonical Huffman build from code lengths (RFC 1951). Returns false on over-subscription.
static bool png_huff_build(struct png_huff *h, const uint8_t *lengths, uint32_t count) {
    png_huff_init(h);
    uint16_t bl_count[16] = {0};
    for (uint32_t i = 0; i < count; i++) {
        if (lengths[i] > 15) return false;
        if (lengths[i]) bl_count[lengths[i]]++;
    }
    uint16_t next_code[16] = {0};
    uint16_t code = 0;
    for (int bits = 1; bits < 16; bits++) {
        code = (uint16_t)((code + bl_count[bits - 1]) << 1);
        next_code[bits] = code;
    }
    for (uint32_t n = 0; n < count; n++) {
        uint8_t len = lengths[n];
        if (!len) continue;
        uint16_t c = next_code[len]++;
        int node = h->root;
        for (int b = len - 1; b >= 0; b--) {
            int bit = (c >> b) & 1;
            int16_t *edge = bit ? &h->right[node] : &h->left[node];
            if (b == 0) {
                if (*edge != -1) return false;
                if (h->node_count >= PNG_HUFF_NODES) return false;
                *edge = (int16_t)h->node_count;
                h->symbol[h->node_count] = (int16_t)n;
                h->node_count++;
            } else {
                if (*edge == -1) {
                    if (h->node_count >= PNG_HUFF_NODES) return false;
                    *edge = (int16_t)h->node_count;
                    h->node_count++;
                }
                node = *edge;
            }
        }
    }
    return true;
}

static bool png_huff_decode(struct png_bit_reader *br, struct png_huff *h, uint32_t *out) {
    int node = h->root;
    for (;;) {
        uint32_t bit;
        if (!png_br_bits(br, 1, &bit)) return false;
        node = bit ? h->right[node] : h->left[node];
        if (node < 0 || node >= h->node_count) return false;
        if (h->symbol[node] >= 0) { *out = (uint32_t)h->symbol[node]; return true; }
    }
}

static const uint16_t png_len_base[29] = {
    3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258
};
static const uint8_t png_len_extra[29] = {
    0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0
};
static const uint16_t png_dist_base[30] = {
    1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
};
static const uint8_t png_dist_extra[30] = {
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
};

static bool png_inflate(const uint8_t *in, uint32_t in_size, uint8_t *out, uint32_t out_size, uint32_t *out_written) {
    *out_written = 0;
    if (in_size < 6) return false; // zlib header + adler
    if ((in[0] & 0x0F) != 8) return false; // deflate only
    if ((((uint32_t)in[0] << 8) | in[1]) % 31 != 0) return false;
    if (in[1] & 0x20) return false; // preset dictionary not supported

    struct png_bit_reader br;
    png_br_init(&br, in + 2, in_size - 2 - 4); // strip header/trailer for raw deflate
    uint32_t out_pos = 0;
    bool final = false;

    static uint8_t fixed_lit_len[288];
    static uint8_t fixed_dist_len[32];
    static bool fixed_ready = false;
    if (!fixed_ready) {
        for (int i = 0; i <= 143; i++) fixed_lit_len[i] = 8;
        for (int i = 144; i <= 255; i++) fixed_lit_len[i] = 9;
        for (int i = 256; i <= 279; i++) fixed_lit_len[i] = 7;
        for (int i = 280; i <= 287; i++) fixed_lit_len[i] = 8;
        for (int i = 0; i < 32; i++) fixed_dist_len[i] = 5;
        fixed_ready = true;
    }

    struct png_huff lit_huff, dist_huff;
    uint8_t dyn_lit_len[288 + 32];

    while (!final) {
        uint32_t bfinal, btype;
        if (!png_br_bits(&br, 1, &bfinal)) return false;
        if (!png_br_bits(&br, 2, &btype)) return false;
        final = bfinal != 0;

        struct png_huff *lit = &lit_huff;
        struct png_huff *dist = &dist_huff;
        bool has_dist = true;

        if (btype == 0) {
            png_br_align(&br);
            // Stored block: LEN + NLEN then raw bytes. br is bit-based; realign to input bytes.
            uint32_t consumed = br.byte_pos;
            const uint8_t *raw = in + 2 + consumed;
            uint32_t raw_left = (in_size - 2 - 4) - consumed;
            if (raw_left < 4) return false;
            uint32_t len = raw[0] | ((uint32_t)raw[1] << 8);
            uint32_t nlen = raw[2] | ((uint32_t)raw[3] << 8);
            if ((len ^ nlen) != 0xFFFF) return false;
            if (raw_left - 4 < len) return false;
            if (out_pos + len > out_size) return false;
            for (uint32_t i = 0; i < len; i++) out[out_pos++] = raw[4 + i];
            br.byte_pos += 4 + len;
            continue;
        } else if (btype == 1) {
            if (!png_huff_build(lit, fixed_lit_len, 288)) return false;
            if (!png_huff_build(dist, fixed_dist_len, 32)) return false;
        } else if (btype == 2) {
            uint32_t hlit, hdist, hclen;
            if (!png_br_bits(&br, 5, &hlit)) return false;
            if (!png_br_bits(&br, 5, &hdist)) return false;
            if (!png_br_bits(&br, 4, &hclen)) return false;
            hlit += 257; hdist += 1; hclen += 4;
            if (hlit > 288 || hdist > 32) return false;
            static const uint8_t cl_order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
            uint8_t cl_len[19] = {0};
            for (uint32_t i = 0; i < hclen; i++) {
                uint32_t v;
                if (!png_br_bits(&br, 3, &v)) return false;
                cl_len[cl_order[i]] = (uint8_t)v;
            }
            struct png_huff cl_huff;
            if (!png_huff_build(&cl_huff, cl_len, 19)) return false;
            uint32_t total = hlit + hdist;
            if (total > sizeof(dyn_lit_len)) return false;
            for (uint32_t i = 0; i < total;) {
                uint32_t sym;
                if (!png_huff_decode(&br, &cl_huff, &sym)) return false;
                if (sym <= 15) {
                    dyn_lit_len[i++] = (uint8_t)sym;
                } else if (sym == 16) {
                    uint32_t rep;
                    if (i == 0 || !png_br_bits(&br, 2, &rep)) return false;
                    rep += 3;
                    if (i + rep > total) return false;
                    for (uint32_t k = 0; k < rep; k++) dyn_lit_len[i++] = dyn_lit_len[i - 1];
                } else if (sym == 17) {
                    uint32_t rep;
                    if (!png_br_bits(&br, 3, &rep)) return false;
                    rep += 3;
                    if (i + rep > total) return false;
                    for (uint32_t k = 0; k < rep; k++) dyn_lit_len[i++] = 0;
                } else if (sym == 18) {
                    uint32_t rep;
                    if (!png_br_bits(&br, 7, &rep)) return false;
                    rep += 11;
                    if (i + rep > total) return false;
                    for (uint32_t k = 0; k < rep; k++) dyn_lit_len[i++] = 0;
                } else {
                    return false;
                }
            }
            if (!png_huff_build(lit, dyn_lit_len, hlit)) return false;
            if (!png_huff_build(dist, dyn_lit_len + hlit, hdist)) return false;
        } else {
            return false; // btype == 3 reserved
        }
        (void)has_dist;

        for (;;) {
            uint32_t sym;
            if (!png_huff_decode(&br, lit, &sym)) return false;
            if (sym < 256) {
                if (out_pos >= out_size) return false;
                out[out_pos++] = (uint8_t)sym;
            } else if (sym == 256) {
                break;
            } else if (sym <= 285) {
                uint32_t li = sym - 257;
                uint32_t len = png_len_base[li];
                uint32_t eb;
                if (!png_br_bits(&br, png_len_extra[li], &eb)) return false;
                len += eb;
                uint32_t dsym;
                if (!png_huff_decode(&br, dist, &dsym)) return false;
                if (dsym > 29) return false;
                uint32_t dist_val = png_dist_base[dsym];
                if (!png_br_bits(&br, png_dist_extra[dsym], &eb)) return false;
                dist_val += eb;
                if (dist_val == 0 || dist_val > out_pos) return false;
                if (out_pos + len > out_size) return false;
                for (uint32_t k = 0; k < len; k++) out[out_pos] = out[out_pos - dist_val], out_pos++;
            } else {
                return false;
            }
        }
    }

    // Adler-32 check over inflated output.
    {
        uint32_t s1 = 1, s2 = 0;
        for (uint32_t i = 0; i < out_pos; i++) {
            s1 = (s1 + out[i]) % 65521;
            s2 = (s2 + s1) % 65521;
        }
        uint32_t expect = read_u32_be(in + in_size - 4);
        if (((s2 << 16) | s1) != expect) return false;
    }
    *out_written = out_pos;
    return true;
}

static uint8_t png_paeth(uint8_t a, uint8_t b, uint8_t c) {
    int p = (int)a + (int)b - (int)c;
    int pa = p - (int)a; if (pa < 0) pa = -pa;
    int pb = p - (int)b; if (pb < 0) pb = -pb;
    int pc = p - (int)c; if (pc < 0) pc = -pc;
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

static bool parse_png(const uint8_t *data, uint32_t size, struct image_data *img) {
    static const uint8_t png_sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (size < 57) return false;
    for (int i = 0; i < 8; i++) if (data[i] != png_sig[i]) return false;

    uint32_t pos = 8;
    uint32_t width = 0, height = 0;
    uint8_t bit_depth = 0, color_type = 0, interlace = 0;
    bool have_ihdr = false;
    const uint8_t *idat_start = NULL;
    uint32_t idat_total = 0;

    // First pass: IHDR + measure IDAT. Second pass uses gathered IDAT copy.
    while (pos + 8 <= size) {
        uint32_t len = read_u32_be(data + pos);
        const uint8_t *type = data + pos + 4;
        if (pos + 8 + len + 4 < pos || pos + 8 + len + 4 > size) return false;
        const uint8_t *chunk = data + pos + 8;
        if (type[0] == 'I' && type[1] == 'H' && type[2] == 'D' && type[3] == 'R') {
            if (len != 13 || have_ihdr) return false;
            width = read_u32_be(chunk);
            height = read_u32_be(chunk + 4);
            bit_depth = chunk[8];
            color_type = chunk[9];
            if (chunk[10] != 0 || chunk[11] != 0) return false; // compression/filter must be 0
            interlace = chunk[12];
            have_ihdr = true;
        } else if (type[0] == 'I' && type[1] == 'D' && type[2] == 'A' && type[3] == 'T') {
            if (!have_ihdr) return false;
            if (idat_start == NULL) idat_start = chunk;
            idat_total += len;
        } else if (type[0] == 'I' && type[1] == 'E' && type[2] == 'N' && type[3] == 'D') {
            break;
        }
        pos += 8 + len + 4;
    }
    if (!have_ihdr || !idat_start || !idat_total) return false;
    if (width == 0 || width > MAX_IMAGE_WIDTH || height == 0 || height > MAX_IMAGE_HEIGHT) return false;
    if (bit_depth != 8 || interlace != 0) return false;
    uint32_t channels = 0;
    if (color_type == 0) channels = 1;
    else if (color_type == 2) channels = 3;
    else if (color_type == 6) channels = 4;
    else return false; // palette (3) and low depths need PLTE path: unsupported

    uint64_t stride = (uint64_t)width * channels + 1;
    uint64_t raw_size = stride * height;
    if (raw_size > 32 * 1024 * 1024) return false;

    uint8_t *idat = (uint8_t *)pc_heap_grow(idat_total);
    if (!idat) return false;
    uint8_t *raw = (uint8_t *)pc_heap_grow((uint64_t)raw_size);
    if (!raw) return false;

    // Gather IDAT chunks in order.
    pos = 8;
    uint32_t copied = 0;
    while (pos + 8 <= size && copied < idat_total) {
        uint32_t len = read_u32_be(data + pos);
        const uint8_t *type = data + pos + 4;
        const uint8_t *chunk = data + pos + 8;
        if (type[0] == 'I' && type[1] == 'D' && type[2] == 'A' && type[3] == 'T') {
            for (uint32_t i = 0; i < len; i++) idat[copied++] = chunk[i];
        } else if (type[0] == 'I' && type[1] == 'E' && type[2] == 'N' && type[3] == 'D') {
            break;
        }
        pos += 8 + len + 4;
    }
    if (copied != idat_total) return false;

    uint32_t inflated = 0;
    if (!png_inflate(idat, idat_total, raw, (uint32_t)raw_size, &inflated) || inflated != raw_size) return false;

    uint64_t total_pixels = (uint64_t)width * height;
    uint32_t *pixels = (uint32_t *)pc_heap_grow(total_pixels * sizeof(uint32_t));
    if (!pixels) return false;

    for (uint32_t y = 0; y < height; y++) {
        uint8_t *row = raw + (uint64_t)y * stride;
        uint8_t filter = row[0];
        if (filter > 4) return false;
        uint8_t *prev = y ? raw + (uint64_t)(y - 1) * stride : NULL;
        for (uint32_t i = 1; i < stride; i++) {
            uint8_t a = i > channels ? row[i - channels] : 0;
            uint8_t b = prev ? prev[i] : 0;
            uint8_t c = (prev && i > channels) ? prev[i - channels] : 0;
            uint8_t v = row[i];
            switch (filter) {
                case 0: break;
                case 1: v = (uint8_t)(v + a); break;
                case 2: v = (uint8_t)(v + b); break;
                case 3: v = (uint8_t)(v + (a + b) / 2); break;
                case 4: v = (uint8_t)(v + png_paeth(a, b, c)); break;
            }
            row[i] = v;
        }
        for (uint32_t x = 0; x < width; x++) {
            uint8_t *px = row + 1 + (uint64_t)x * channels;
            uint32_t color;
            if (channels == 1) {
                color = ((uint32_t)px[0] << 16) | ((uint32_t)px[0] << 8) | px[0];
            } else {
                color = ((uint32_t)px[0] << 16) | ((uint32_t)px[1] << 8) | px[2];
            }
            pixels[(uint64_t)y * width + x] = color;
        }
    }

    img->pixels = pixels;
    img->width = width;
    img->height = height;
    img->bpp = 24;
    img->loaded = true;
    return true;
}

static bool load_image_file(const char *path, struct image_data *img) {
    if (!path || !img) return false;
    pc_copy(img->path, path, sizeof(img->path));
    img->loaded = false;
    img->pixels = NULL;

    int32_t fd = pc_file_open(path);
    if (fd < 0) return false;

    uint8_t *buf = (uint8_t *)pc_heap_grow(MAX_IMAGE_FILE_SIZE);
    if (!buf) {
        (void)pc_file_close(fd);
        return false;
    }

    int32_t bytes_read = pc_file_read(fd, buf, MAX_IMAGE_FILE_SIZE);
    (void)pc_file_close(fd);

    if (bytes_read <= 0) return false;

    if (parse_bmp(buf, (uint32_t)bytes_read, img)) return true;
    if (parse_png(buf, (uint32_t)bytes_read, img)) return true;
    if (parse_ppm(buf, (uint32_t)bytes_read, img)) return true;

    return false;
}

static void draw_image_view(struct pg_window *window, const struct image_data *img) {
    struct pg_rect client = pg_window_client(window);
    pg_window_clear(window, 0x001B2028); // Dark studio background

    if (!img->loaded || !img->pixels) {
        const char *basename = img->path;
        for (const char *p = img->path; *p; p++) {
            if (*p == '/') basename = p + 1;
        }

        // Check if it's a still-unsupported compressed format (JPEG).
        uint32_t nlen = pc_strlen(basename);
        bool is_jpg = (nlen >= 4 && (pc_strcmp(basename + nlen - 4, ".jpg") == 0 ||
                                     pc_strcmp(basename + nlen - 4, ".JPG") == 0)) ||
                      (nlen >= 5 && pc_strcmp(basename + nlen - 5, ".jpeg") == 0);

        pg_window_rect(window, (struct pg_rect){0, 0, client.width, client.height}, 0x001B2028);
        pg_window_rect(window, (struct pg_rect){0, 0, client.width, 48}, 0x00242B35);
        pg_window_text(window, 8, 14, "Image Viewer", 0x00E0E0E0);

        if (is_jpg) {
            pg_window_text(window, 20, 72, "Cannot display compressed image.", 0x00FF6B6B);
            pg_window_text(window, 20, 96, "JPEG format uses lossy compression - not supported.", 0x00CCAA44);
            pg_window_text(window, 20, 120, "Convert your image to BMP (24-bit) or PNG using:", 0x00AAAAAA);
            pg_window_text(window, 20, 144, "  convert image.jpg image.bmp", 0x0066CCFF);
            pg_window_text(window, 20, 168, "Supported formats: BMP 24/32/8-bit, PNG 8-bit RGB/Gray, PPM P6", 0x00888888);
        } else {
            pg_window_text(window, 20, 72, "Cannot load image file.", 0x00FF6B6B);
            pg_window_text(window, 20, 96, "Supported: BMP (24-bit, 32-bit, 8-bit), PNG (8-bit RGB/RGBA/Gray), PPM (P6)", 0x00CCCCCC);
            pg_window_text(window, 20, 120, "File:", 0x00888888);
            pg_window_text(window, 68, 120, img->path, 0x00AAAAAA);
        }
        return;
    }

    // Top status bar
    char info[128];
    pc_copy(info, "File: ", sizeof(info));
    uint32_t len = pc_strlen(info);
    pc_copy(info + len, img->path, sizeof(info) - len);

    pg_window_rect(window, (struct pg_rect){0, 0, client.width, 24}, 0x00242B35);
    pg_window_text(window, 8, 4, info, 0x00E0E0E0);

    // Render Image Centered inside client area
    uint32_t avail_w = client.width;
    uint32_t avail_h = client.height > 24 ? client.height - 24 : 0;
    uint32_t start_x = (avail_w > img->width) ? (avail_w - img->width) / 2 : 0;
    uint32_t start_y = 24 + ((avail_h > img->height) ? (avail_h - img->height) / 2 : 0);

    uint32_t draw_w = img->width < avail_w ? img->width : avail_w;
    uint32_t draw_h = img->height < avail_h ? img->height : avail_h;

    // Fast Span-based rendering
    for (uint32_t y = 0; y < draw_h; y++) {
        uint32_t py = start_y + y;
        uint32_t x = 0;
        while (x < draw_w) {
            uint32_t color = img->pixels[y * img->width + x];
            uint32_t span = 1;
            while (x + span < draw_w && img->pixels[y * img->width + x + span] == color) {
                span++;
            }
            pg_window_rect(window, (struct pg_rect){start_x + x, py, span, 1}, color);
            x += span;
        }
    }
}

int main(void) {
    char cmdline[128];
    char path[128] = "/src/demo/screenshot.bmp";
    bool explicit_path = false;

    int32_t cmdlen = pc_get_command_line(cmdline, sizeof(cmdline));
    if (cmdlen > 0) {
        // command_line is passed directly as the file path argument
        // strip leading spaces
        uint32_t start = 0;
        while (cmdline[start] == ' ' || cmdline[start] == '\t') start++;
        // strip trailing whitespace/newlines
        uint32_t end = (uint32_t)cmdlen;
        while (end > start && (cmdline[end-1] == ' ' || cmdline[end-1] == '\n' ||
                                cmdline[end-1] == '\r' || cmdline[end-1] == '\t')) end--;
        if (end > start) {
            cmdline[end] = '\0';
            pc_copy(path, cmdline + start, sizeof(path));
            explicit_path = true;
        }
    }

    if (!explicit_path) {
        // Installed systems carry the BMP under an 8.3 alias path as well;
        // live media may only have the PNG. Try each candidate in order.
        static const char *candidates[] = {
            "/src/demo/screenshot.bmp",
            "/demo/screenshot.bmp",
            "/src/demo/image.png",
            "/demo/image.png"
        };
        bool ok = false;
        for (uint32_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
            if (load_image_file(candidates[i], &g_image)) { ok = true; break; }
            g_image.pixels = NULL;
            g_image.loaded = false;
        }
        (void)ok;
    } else {
        (void)load_image_file(path, &g_image);
    }

    struct pc_display_info display;
    if (!pc_display_get_info(&display) || !display.available) return 1;

    uint32_t win_w = g_image.loaded ? g_image.width + 16 : 640;
    uint32_t win_h = g_image.loaded ? g_image.height + 40 : 400;

    if (win_w < 400) win_w = 400;
    if (win_h < 300) win_h = 300;
    if (win_w > display.width - 40) win_w = display.width - 40;
    if (win_h > display.height - 60) win_h = display.height - 60;

    struct pg_window window;
    char title[64];
    pc_copy(title, "Image Viewer - ", sizeof(title));
    uint32_t tlen = pc_strlen(title);
    // Show only filename, not full path in title (prefer actually loaded file)
    const char *title_src = g_image.loaded ? g_image.path : path;
    const char *basename = title_src;
    for (const char *p = title_src; *p; p++) {
        if (*p == '/') basename = p + 1;
    }
    pc_copy(title + tlen, basename, sizeof(title) - tlen);

    if (!pg_window_center(&window, title, win_w, win_h)) return 1;

    struct pg_event event = {.type = PG_EVENT_NONE};
    pg_window_begin(&window);
    draw_image_view(&window, &g_image);
    pg_window_end(&window);

    while (pg_window_is_open(&window)) {
        if (!pg_window_poll_event(&window, &event)) {
            pc_sleep(16);
            continue;
        }
        if (event.type == PG_EVENT_CLOSE) break;
        if (event.type == PG_EVENT_KEY && (event.key == 27 || event.key == 'q' || event.key == 'Q')) break;

        if (event.type == PG_EVENT_REPAINT) {
            pg_window_begin(&window);
            draw_image_view(&window, &g_image);
            pg_window_end(&window);
        }
    }

    pg_window_close(&window);
    return 0;
}
