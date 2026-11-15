/*
 * PPA-1 ROM packer - bitmap and binary resource installer.
 * (c) 2026 Adam Cir (Adava), Adava Software, Adava Development
 *
 * Input: a 64 KiB program produced by ppa-1_asm --program,
 *        or a legacy 2 MiB EEPROM image (backwards compatibility).
 * Output: a complete 2 MiB EEPROM image with ROM bank 0000 for code,
 *         optional files and BMPs in ROM banks 0001..001F.
 * BMP: uncompressed Windows BMP, indexed 8-bit, BGR 24-bit or BGRA 32-bit.
 * All images share one fixed 256-color VGA palette: 16 EGA + RGB 6x8x5.
 *
 * PIMG v1 per-bank layout, 16-bit little endian values:
 * 0000..0003  "PIMG"
 * 0004        version 1
 * 0005        format 1 (8-bit indexed)
 * 0006..0007  width
 * 0008..0009  height
 * 000A..000B  pixel length (= width * height)
 * 000C..000D  pixel offset 0400h
 * 000E..000F  VGA palette offset 0010h
 * 0010..030F  VGA RGB triplets (256 * 3)
 * 0400..      top-to-bottom row-major indexed pixels
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#define ROM_BANKS 32u
#define BANK_BYTES 65536u
#define ROM_BYTES (ROM_BANKS * BANK_BYTES)
#define PIXEL_OFFSET 0x0400u
#define PAL_OFFSET 0x0010u
#define MAX_WIDTH 320u
#define MAX_HEIGHT 200u

static uint8_t rom[ROM_BYTES];
static uint8_t occupied[ROM_BYTES];

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)((unsigned)p[0] | ((unsigned)p[1] << 8));
}
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void wr16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
static void fail(const char *why, const char *path) {
    fprintf(stderr, "ppa-rompack: %s%s%s\n", why, path ? ": " : "",
            path ? path : "");
    exit(1);
}
static uint8_t *read_file(const char *path, size_t *bytes) {
    FILE *f = fopen(path, "rb");
    if (!f) fail(strerror(errno), path);
    if (fseek(f, 0, SEEK_END) != 0) fail("seek failed", path);
    long sz = ftell(f);
    if (sz < 0) fail("ftell failed", path);
    rewind(f);
    uint8_t *p = malloc((size_t)sz ? (size_t)sz : 1u);
    if (!p) fail("out of memory", path);
    if (fread(p, 1, (size_t)sz, f) != (size_t)sz)
        fail("read failed", path);
    if (fclose(f) != 0) fail("close failed", path);
    *bytes = (size_t)sz;
    return p;
}
static unsigned bank_hex(const char *s, size_t n) {
    char text[16], *end;
    if (n == 0 || n >= sizeof(text)) fail("invalid ROM bank", s);
    memcpy(text, s, n);
    text[n] = '\0';
    errno = 0;
    unsigned long v = strtoul(text, &end, 16);
    if (errno || end == text || *end || v < 1u || v >= ROM_BANKS)
        fail("bank must be hexadecimal 0001..001F (0000 is protected)", text);
    return (unsigned)v;
}
static unsigned offset_hex(const char *s, size_t n) {
    char text[16], *end;
    if (n == 0 || n >= sizeof(text)) fail("invalid offset", s);
    memcpy(text, s, n);
    text[n] = '\0';
    errno = 0;
    unsigned long v = strtoul(text, &end, 16);
    if (errno || end == text || *end || v >= BANK_BYTES)
        fail("offset must be hexadecimal 0000..FFFF", text);
    return (unsigned)v;
}
static void place(unsigned bank, unsigned off, const uint8_t *data,
                  size_t n, const char *name) {
    if (n > BANK_BYTES - off)
        fail("resource does not fit inside a single ROM bank", name);
    size_t at = (size_t)bank * BANK_BYTES + off;
    for (size_t i = 0; i < n; i++) {
        if (occupied[at + i] || rom[at + i] != 0xFF)
            fail("ROM collision; pack into a clean assembler image", name);
    }
    memcpy(rom + at, data, n);
    memset(occupied + at, 1, n);
}
static unsigned quantize(uint8_t r, uint8_t g, uint8_t b) {
    unsigned rr = ((unsigned)r * 5u + 127u) / 255u;
    unsigned gg = ((unsigned)g * 7u + 127u) / 255u;
    unsigned bb = ((unsigned)b * 4u + 127u) / 255u;
    return 16u + rr * 40u + gg * 5u + bb;
}
static void make_palette(uint8_t *dst) {
    static const uint8_t ega[16][3] = {
        {0,0,0},{0,0,170},{0,170,0},{0,170,170},
        {170,0,0},{170,0,170},{170,85,0},{170,170,170},
        {85,85,85},{85,85,255},{85,255,85},{85,255,255},
        {255,85,85},{255,85,255},{255,255,85},{255,255,255}
    };
    memcpy(dst, ega, sizeof(ega));
    for (unsigned r = 0; r < 6; ++r)
        for (unsigned g = 0; g < 8; ++g)
            for (unsigned b = 0; b < 5; ++b) {
                unsigned idx = 16u + r * 40u + g * 5u + b;
                dst[3u * idx]     = (uint8_t)(r * 255u / 5u);
                dst[3u * idx + 1] = (uint8_t)(g * 255u / 7u);
                dst[3u * idx + 2] = (uint8_t)(b * 255u / 4u);
            }
}
static void install_bmp(unsigned bank, const char *path) {
    size_t file_size;
    uint8_t *bmp = read_file(path, &file_size);
    if (file_size < 54 || bmp[0] != 'B' || bmp[1] != 'M')
        fail("not a Windows BMP", path);
    uint32_t pixel_start = rd32(bmp + 10);
    uint32_t dib_size = rd32(bmp + 14);
    if (dib_size < 40 || (uint64_t)14 + dib_size > file_size)
        fail("unsupported BMP DIB header", path);
    int32_t signed_w = (int32_t)rd32(bmp + 18);
    int32_t signed_h = (int32_t)rd32(bmp + 22);
    unsigned bpp = rd16(bmp + 28);
    unsigned compression = rd32(bmp + 30);
    if (rd16(bmp + 26) != 1 || compression != 0 ||
        (bpp != 8 && bpp != 24 && bpp != 32))
        fail("GIMP: export as uncompressed 8/24/32-bit BMP", path);
    if (signed_w <= 0 || signed_h == 0 || signed_h == INT32_MIN)
        fail("invalid BMP dimensions", path);
    unsigned w = (unsigned)signed_w;
    unsigned h = (unsigned)(signed_h < 0 ? -signed_h : signed_h);
    if (w > MAX_WIDTH || h > MAX_HEIGHT || (uint64_t)w * h > 64000u)
        fail("BMP maximum dimensions are 320x200", path);
    uint64_t stride = (((uint64_t)w * bpp + 31u) / 32u) * 4u;
    if ((uint64_t)pixel_start + stride * h > file_size)
        fail("truncated BMP pixel array", path);
    const uint8_t *indexed_pal = NULL;
    size_t ncolors = 0;
    if (bpp == 8) {
        uint64_t pal_start = 14u + dib_size;
        if (pixel_start < pal_start)
            fail("BMP palette overlaps pixels", path);
        ncolors = (pixel_start - (size_t)pal_start) / 4u;
        unsigned used = rd32(bmp + 46);
        if (used && used < ncolors) ncolors = used;
        if (ncolors > 256) ncolors = 256;
        if (!ncolors) fail("missing 8-bit BMP color table", path);
        indexed_pal = bmp + pal_start;
    }

    uint8_t packed[PIXEL_OFFSET + 64000u];
    memset(packed, 0xFF, sizeof(packed));
    memcpy(packed, "PIMG", 4);
    packed[4] = 1; packed[5] = 1;
    wr16(packed + 6, (uint16_t)w);
    wr16(packed + 8, (uint16_t)h);
    wr16(packed + 10, (uint16_t)(w * h));
    wr16(packed + 12, PIXEL_OFFSET);
    wr16(packed + 14, PAL_OFFSET);
    make_palette(packed + PAL_OFFSET);
    for (unsigned y = 0; y < h; ++y) {
        unsigned in_y = signed_h > 0 ? (h - 1u - y) : y;
        const uint8_t *row = bmp + pixel_start + (size_t)in_y * (size_t)stride;
        for (unsigned x = 0; x < w; ++x) {
            uint8_t r, g, b;
            if (bpp == 8) {
                unsigned id = row[x];
                if (id >= ncolors) fail("invalid BMP palette index", path);
                b = indexed_pal[id * 4u];
                g = indexed_pal[id * 4u + 1u];
                r = indexed_pal[id * 4u + 2u];
            } else {
                const uint8_t *px = row + x * (bpp / 8u);
                b = px[0]; g = px[1]; r = px[2];
            }
            packed[PIXEL_OFFSET + (size_t)y * w + x] =
                (uint8_t)quantize(r, g, b);
        }
    }
    place(bank, 0, packed, PIXEL_OFFSET + (size_t)w * h, path);
    fprintf(stdout, "BMP: ROM %04X:0000 %ux%u 8bpp (%zu bytes) <- %s\n",
            bank, w, h, PIXEL_OFFSET + (size_t)w * h, path);
    free(bmp);
}
static void install_file(unsigned bank, unsigned offset, const char *path) {
    size_t bytes;
    uint8_t *data = read_file(path, &bytes);
    if (!bytes) fail("refusing empty resource", path);
    place(bank, offset, data, bytes, path);
    fprintf(stdout, "FILE: ROM %04X:%04X %zu bytes <- %s\n",
            bank, offset, bytes, path);
    free(data);
}
static void usage(const char *app) {
    fprintf(stderr,
        "Usage: %s PROGRAM_FILE OUTPUT_ROM [operations...]\n"
        "  --bmp BANK:FILE         Convert GIMP BMP to PIMG resource, bank 0001..001F\n"
        "  --put BANK:OFFSET:FILE  Insert arbitrary binary data at an ROM address\n"
        "  --info                  Print bank metadata after packing\n"
        "PROGRAM_FILE is 64 KiB (from assembler --program), or legacy 2 MiB.\n"
        "No assets are required: ROMPACK still builds the complete 2 MiB image.\n"
        "BANK and OFFSET are hexadecimal, bank 0000 is reserved for ASM code.\n"
        "Example: %s out/main.prg out/main.bin --bmp 0001:assets/cab_ecs.bmp\n",
        app, app);
}
int main(int argc, char **argv) {
    if (argc < 3) { usage(argv[0]); return 1; }
    size_t bytes;
    uint8_t *input = read_file(argv[1], &bytes);
    /* Assemble always produces a program first. For legacy callers, a
     * complete EEPROM is also accepted as the input image. */
    memset(rom, 0xFF, sizeof(rom));
    if (bytes == BANK_BYTES) {
        memcpy(rom, input, BANK_BYTES);
        fprintf(stdout, "Program: 64 KiB -> EEPROM bank 0000\n");
    } else if (bytes == ROM_BYTES) {
        memcpy(rom, input, ROM_BYTES);
        fprintf(stdout, "Legacy EEPROM input: 2 MiB\n");
    } else {
        fail("input must be a 64 KiB program or legacy 2 MiB ROM", argv[1]);
    }
    free(input);
    int edits = 0;
    for (int i = 3; i < argc; ++i) {
        if (!strcmp(argv[i], "--info")) {
            for (unsigned b = 0; b < ROM_BANKS; ++b) {
                size_t off = (size_t)b * BANK_BYTES;
                if (!memcmp(rom + off, "PIMG", 4))
                    printf("Bank %04X: PIMG v%u %ux%u\n", b,
                           rom[off + 4], rd16(rom + off + 6),
                           rd16(rom + off + 8));
            }
        } else if (!strcmp(argv[i], "--bmp")) {
            if (++i >= argc) fail("missing --bmp BANK:FILE", NULL);
            char *split = strchr(argv[i], ':');
            if (!split || !split[1]) fail("expected BANK:FILE", argv[i]);
            unsigned b = bank_hex(argv[i], (size_t)(split - argv[i]));
            install_bmp(b, split + 1);
            edits++;
        } else if (!strcmp(argv[i], "--put")) {
            if (++i >= argc) fail("missing --put BANK:OFFSET:FILE", NULL);
            char *c1 = strchr(argv[i], ':');
            char *c2 = c1 ? strchr(c1 + 1, ':') : NULL;
            if (!c1 || !c2 || !c2[1]) fail("expected BANK:OFFSET:FILE", argv[i]);
            unsigned b = bank_hex(argv[i], (size_t)(c1 - argv[i]));
            unsigned off = offset_hex(c1 + 1, (size_t)(c2 - c1 - 1));
            install_file(b, off, c2 + 1);
            edits++;
        } else {
            fail("unrecognized option", argv[i]);
        }
    }
    if (!edits) fprintf(stdout, "No assets requested; program-only ROM.\n");
    FILE *f = fopen(argv[2], "wb");
    if (!f) fail(strerror(errno), argv[2]);
    if (fwrite(rom, 1, ROM_BYTES, f) != ROM_BYTES || fclose(f) != 0)
        fail("writing ROM output failed", argv[2]);
    printf("ROMPACK complete: %s (2 MiB / 32 ROM banks)\n", argv[2]);
    return 0;
}
