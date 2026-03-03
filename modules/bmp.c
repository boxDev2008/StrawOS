#include <libc.h>

#define READ_U16(p) ((uint16_t)((p)[0] | ((p)[1] << 8)))
#define READ_U32(p) ((uint32_t)((p)[0] | ((p)[1] << 8) | ((p)[2] << 16) | ((p)[3] << 24)))


#pragma pack(push, 1)

typedef struct {
    uint16_t bfType;      // 'BM'
    uint32_t bfSize;
    uint16_t bfReserved1;
    uint16_t bfReserved2;
    uint32_t bfOffBits;
} BMPFileHeader;

typedef struct {
    uint32_t biSize;
    int32_t  biWidth;
    int32_t  biHeight;
    uint16_t biPlanes;
    uint16_t biBitCount;
    uint32_t biCompression;
    uint32_t biSizeImage;
    int32_t  biXPelsPerMeter;
    int32_t  biYPelsPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
} BMPInfoHeader;

#pragma pack(pop)

#define BI_RGB 0

uint32_t* bmp_load_from_memory(
    const void* data,
    size_t data_size,
    int* out_width,
    int* out_height
) {
    if (!data || data_size < sizeof(BMPFileHeader) + sizeof(BMPInfoHeader))
        return NULL;

    const uint8_t* bytes = (const uint8_t*)data;

    const BMPFileHeader* file =
        (const BMPFileHeader*)bytes;

    if (file->bfType != 0x4D42) // 'BM'
        return NULL;

    const BMPInfoHeader* info =
        (const BMPInfoHeader*)(bytes + sizeof(BMPFileHeader));

    if (info->biCompression != BI_RGB)
        return NULL;

    if (info->biBitCount != 24 && info->biBitCount != 32)
        return NULL;

    int width  = info->biWidth;
    int height = info->biHeight < 0 ? -info->biHeight : info->biHeight;
    int bottom_up = info->biHeight > 0;

    const uint8_t* pixel_data = bytes + file->bfOffBits;

    int bytes_per_pixel = info->biBitCount / 8;
    int row_stride = ((width * bytes_per_pixel + 3) / 4) * 4;

    if (file->bfOffBits + row_stride * height > data_size)
        return NULL;

    uint32_t* pixels = (uint32_t*)malloc(width * height * sizeof(uint32_t));
    if (!pixels)
        return NULL;

    for (int y = 0; y < height; ++y) {
        int src_y = bottom_up ? (height - 1 - y) : y;
        const uint8_t* src_row = pixel_data + src_y * row_stride;

        for (int x = 0; x < width; ++x) {
            const uint8_t* p = src_row + x * bytes_per_pixel;

            uint8_t b = p[0];
            uint8_t g = p[1];
            uint8_t r = p[2];
            uint8_t a = (bytes_per_pixel == 4) ? p[3] : 255;

            pixels[y * width + x] =
                (a << 24) |
                (r << 16) |
                (g << 8)  |
                (b);
        }
    }

    *out_width  = width;
    *out_height = height;
    return pixels;
}

uint32_t* load_bmp_from_file(
    const char *filename,
    int* out_width,
    int* out_height
) {
    int fd = open(filename, O_RDONLY);
    if (fd < 0) return NULL;

    stat_t file_stat;
    if (stat(filename, &file_stat) != 0) {
        close(fd);
        return NULL;
    }

    size_t size = file_stat.st_size;
    uint8_t* data = (uint8_t*)malloc(size);
    if (!data) {
        close(fd);
        return NULL;
    }

    read(fd, data, size);

    uint32_t *result = bmp_load_from_memory(data, size, out_width, out_height);

    free(data);
    close(fd);
    return result;
}