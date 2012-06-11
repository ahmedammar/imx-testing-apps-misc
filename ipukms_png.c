typedef struct rgb888 {
        char r;
        char g;
        char b;
} rgb888_t;

typedef rgb888_t rgb24_t;

typedef struct rgba8888 {
        char r;
        char g;
        char b;
        char a;
} argb8888_t;

int rgba565_to_rgb888(const uint16_t* src, char* dst, size_t pixel)
{
    int i;
    struct rgb888  *to;

    to = (struct rgb888 *) dst;

    i = 0;
    /* traverse pixel of the row */
    while(i++ < pixel) {
        to->r = ((*src & 0xf800) >> 11) << 3;  //r5
        to->g = ((*src & 0x07e0) >>  5) << 2;  //g6
        to->b = ((*src & 0x001f) >>  0) << 3;  //b5
        to++;
        src++;
    }

    return 0;
}


int rgba8888_to_rgb888(const char* src, char* dst, size_t pixel)
{
    int i;
    struct rgba8888  *from;
    struct rgb888  *to;

    from = (struct rgba8888 *) src;
    to = (struct rgb888 *) dst;

    i = 0;
    /* traverse pixel of the row */
    while(i++ < pixel) {

        to->r = from->r;
        to->g = from->g;
        to->b = from->b;

        to++;
        from++;
    }

    return 0;
}

static void
stdio_write_func (png_structp png, png_bytep data, png_size_t size)
{
    FILE *fp;
    size_t ret;

    fp = (FILE*) png_get_io_ptr (png);
    while (size) {
        ret = fwrite (data, 1, size, fp);
        size -= ret;
        data += ret;
      if (size && ferror (fp))
         printf("write: %m\n");
   }
}

static void
png_simple_output_flush_fn (png_structp png_ptr)
{
}

static void
png_simple_error_callback (png_structp png,
                       png_const_charp error_msg)
{
    printf("png error: %s\n", error_msg);
}

static void
png_simple_warning_callback (png_structp png,
                         png_const_charp error_msg)
{
    fprintf(stderr, "png warning: %s\n", error_msg);
}

/* save rgb888 to png format in fp */
int save_png(const char* path, const char* data, int width, int height)
{
    FILE *fp;
    png_byte **volatile rows;
    png_struct *png;
    png_info *info;

    fp = fopen(path, "w");
    if (!fp) {
        printf("Cannot open file %s for write\n", path);
        return -ENONET;
    }

    rows = (png_byte ** volatile) malloc(height * sizeof rows[0]);
    if (!rows) goto oops;

    int i;
    for (i = 0; i < height; i++)
        rows[i] = (png_byte *) data + i * width * 3 /*fb.stride*/;

    png = png_create_write_struct (PNG_LIBPNG_VER_STRING, NULL,
                               png_simple_error_callback,
                               png_simple_warning_callback);
    if (!png)
        printf("png_create_write_struct failed\n");

    info = png_create_info_struct (png);
    if (!info)
        printf("png_create_info_struct failed\n");

    png_set_write_fn (png, fp, stdio_write_func, png_simple_output_flush_fn);
    png_set_IHDR (png, info,
            width,
            height,
#define DEPTH 8
            DEPTH,
        PNG_COLOR_TYPE_RGB,
        PNG_INTERLACE_NONE,
        PNG_COMPRESSION_TYPE_DEFAULT,
        PNG_FILTER_TYPE_DEFAULT);

    png_color_16 white;

    white.gray = (1 << DEPTH) - 1;
    white.red = white.blue = white.green = white.gray;

    png_set_bKGD (png, info, &white);
    png_write_info (png, info);

    png_write_image (png, rows);
    png_write_end (png, info);

    png_destroy_write_struct (&png, &info);

    free (rows);
    return 0;

oops:
    return -1;
}

