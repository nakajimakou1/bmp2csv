#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

typedef struct __attribute__((packed)) {
    char magic[2];
    int32_t size;
    int16_t rsvd1;
    int16_t rsvd2;
    int32_t offset;
} file_hdr;

typedef struct __attribute__((packed)) {
    int32_t size;
    int32_t width;
    int32_t height;
    int16_t plane;
    int16_t depth;
    int32_t compress;
    int32_t image;
    int32_t xres;
    int32_t yres;
    int32_t palette;
    int32_t color;
} info_hdr_v1;

typedef struct __attribute__((packed)) {
    uint16_t blue  : 4,
             green : 4,
             red   : 4,
             alpha : 4;
} pixel_u16;
    
typedef struct __attribute__((packed)) {
    uint8_t blue;
    uint8_t green;
    uint8_t red;
} pixel_u24;
    
typedef struct __attribute__((packed)) {
    uint8_t blue;
    uint8_t green;
    uint8_t red;
    uint8_t alpha;
} pixel_u32;

typedef enum {
    MODE_RGBA,
    MODE_BT601,
    MODE_BT709,
    MODE_BOUND
} PRINTPX_MODE;

PRINTPX_MODE g_mode;

#define RGB(px) px->red,px->green,px->blue
#define RGB_FMT "%02X%02X%02X00,"
#define RGB_END "%02X%02X%02X00\n"

#define RGBA(px) px->red,px->green,px->blue,px->alpha
#define RGBA_FMT "%02X%02X%02X%02X,"
#define RGBA_END "%02X%02X%02X%02X\n"

#define BT601(px) (uint8_t)(px->red*0.299 + px->green*0.587 + px->blue*0.114)
#define BT601_FMT "%02X,"
#define BT601_END "%02X\n"

#define BT709(px) (uint8_t)(px->red*0.2126 + px->green*0.7152 + px->blue*0.0722)
#define BT709_FMT BT601_FMT
#define BT709_END BT601_END

#define PRINTPX(color, format, px) do { \
    switch (g_mode) { \
        case MODE_RGBA:  fprintf(stdout, color##_##format, color(px)); \
                         break; \
        case MODE_BT601: fprintf(stdout, BT601_##format, BT601(px)); \
                         break; \
        case MODE_BT709: fprintf(stdout, BT709_##format, BT709(px)); \
                         break; \
        default:         break; \
    } \
} while (0)

#define PIXEL_TABLE_COLOR(type, color) do { \
    uint32_t i; \
    uint32_t j; \
    pixel_##type *pixel; \
    for (i = 0; i < ihdr->height; i++) { \
        pixel = (pixel_##type *)row; \
        for (j = 0; j + 1 < ihdr->width; j++) { \
            PRINTPX(color, FMT, pixel); \
            pixel++; \
        } \
        PRINTPX(color, END, pixel); \
        row -= width; \
    } \
} while (0)

uint32_t palette_index(int16_t depth, uint32_t *block, uint32_t i)
{
    uint32_t offset;
    uint32_t index;
    
    offset = depth * (i - 1);
    /* swap endian */
    index = ((*block >> 24) & 0xFF) |
            ((*block >>  8) & 0xFF00) |
            ((*block <<  8) & 0xFF0000) |
            ((*block << 24) & 0xFF000000);
    
    return (index & (((1 << depth) - 1) << offset)) >> offset;
}

void pixel_table_row(info_hdr_v1 *ihdr, pixel_u32 *palette, uint32_t *block)
{
    uint32_t i;
    uint32_t count;
    pixel_u32 *pixel;
    
    count = 0;
    
    do {
        for (i = 32 / ihdr->depth; i > 0; i--) {
            pixel = palette + palette_index(ihdr->depth, block, i);
            if (++count == ihdr->width)
                break;
            PRINTPX(RGBA, FMT, pixel);
        }
        block++;
    } while (count != ihdr->width);
    
    PRINTPX(RGBA, END, pixel);
    
    return;
}

void pixel_table_index(info_hdr_v1 *ihdr, uint32_t *row, int32_t width)
{
    uint32_t i;
    pixel_u32 *palette;
    
    palette = (pixel_u32 *)((char *)ihdr + sizeof(info_hdr_v1));
    
    for (i = 0; i < ihdr->height; i++) {
        pixel_table_row(ihdr, palette, row);
        row -= width;
    }
    
    return;
}

void bmp_info_hdr_v1(void *data, int32_t offset)
{
    int32_t pad;
    int32_t width;
    uint32_t *row;
    info_hdr_v1 *ihdr;
    
    ihdr = (info_hdr_v1 *)((char *)data + sizeof(file_hdr));
    if (0 == ihdr->width * ihdr->height) {
        fprintf(stderr, "Invalid dimensions: %d x %d.\n", ihdr->width, ihdr->depth);
        return;
    }
    
    pad = ((ihdr->width * ihdr->depth) % 32) ? 1 : 0;
    width = (ihdr->width * ihdr->depth / 32) + pad;
    /* pixel table starts from left to right, row by row, bottom to top */
    row = (uint32_t *)((char *)data + offset) + (ihdr->height - 1) * width;
    
    switch (ihdr->depth) {
        case  1:
        case  2:
        case  4:
        case  8: pixel_table_index(ihdr, row, width);
                 break;
        case 16: PIXEL_TABLE_COLOR(u16, RGBA);
                 break;
        case 24: PIXEL_TABLE_COLOR(u24, RGB);
                 break;
        case 32: PIXEL_TABLE_COLOR(u32, RGBA);
                 break;
        default:
                 fprintf(stderr, "Color depth %d not supported yet.\n", ihdr->depth);
                 break;
    }
    
    return;
}

void usage(void)
{
    fprintf(stderr, "Usage: ./bmp2csv in.bmp [mode] > out.csv\n\n");
    fprintf(stderr, "mode: %u - RGBA(default)\n", MODE_RGBA);
    fprintf(stderr, "      %u - BT.601/SDTV\n", MODE_BT601);
    fprintf(stderr, "      %u - BT.709/HDTV\n", MODE_BT709);
    return;
}

int main(int argc, char *argv[])
{
    char *path;
    FILE *file;
    void *data;
    int32_t size;

    file_hdr *fhdr;

    switch (argc) {
        case 2:  g_mode = MODE_RGBA;
                 break;
        case 3:  sscanf(argv[2], "%u", &g_mode);
                 if (MODE_BOUND > g_mode)
                     break;
        default: usage();
                 return 1;
    }
    
    path = argv[1];
    
    file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "File %s open failed.\n", path);
        return 2;
    }

    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);

    data = calloc(1, size);
    if (!data) {
        fprintf(stderr, "Allocate memory of %d bytes failed.\n", size);
        fclose(file);
        return 3;
    }

    if (size != fread(data, 1, size, file)) {
        fprintf(stderr, "Incomplete read of file %s.\n", path);
        free(data);
        fclose(file);
        return 4;
    }

    fhdr = (file_hdr *)data;
    if (0 != strncmp(fhdr->magic, "BM", 2) || size != fhdr->size) {
        fprintf(stderr, "Invalid BMP file.\n");
        free(data);
        fclose(file);
        return 5;
    }

    switch (*((int32_t *)((char *)data + sizeof(file_hdr)))) {
        case sizeof(info_hdr_v1):
            bmp_info_hdr_v1(data, fhdr->offset);
            break;
        default:
            fprintf(stderr, "DIB version not supported yet.\n");
            free(data);
            fclose(file);
            return 7;
    }

    free(data);
    fclose(file);
    return 0;
}
