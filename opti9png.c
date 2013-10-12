#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <png.h>
#include <arpa/inet.h>  // for ntohl and htonl

png_bytep chunk_names = (png_bytep)"npTc\0npLb", nptc_data = NULL, nplb_data = NULL;
uint32_t nptc_len, nplb_len;
png_bytep *rows, *cols, *new_rows;
uint32_t width, height, new_width, new_height;
uint32_t num_x_divs, num_y_divs, *x_divs, *y_divs;
uint32_t x_scaling = 1, y_scaling = 1, x_div_width, y_div_height;
uint32_t left_reduce = 0, right_reduce = 0, top_reduce = 0, bottom_reduce = 0;

int read_chunk(png_structp png_ptr, png_unknown_chunkp chunk) {
    if (memcmp(chunk->name, chunk_names, 5) == 0) {
        nptc_len = chunk->size;
        nptc_data = (png_bytep)malloc(nptc_len);
        memcpy(nptc_data, chunk->data, nptc_len);
    } else if (memcmp(chunk->name, chunk_names + 5, 5) == 0) {
        nplb_len = chunk->size;
        nplb_data = (png_bytep)malloc(nplb_len);
        memcpy(nplb_data, chunk->data, nplb_len);
    }

    return 1;
}

int gcd(int p, int q) {
    int r;

    while (q) {
        r = p % q;
        p = q;
        q = r;
    }

    return p;
}

int max_scaling(png_bytep *data, int size, int len) {
    int i, num_same = 1, scaling = size;

    for (i = 1; i <= size; i++) {
        if (i < size && num_same < scaling && memcmp(data[i], data[i - 1], len) == 0) {
            num_same++;
        } else {
            scaling = gcd(scaling, num_same);

            if (scaling == 1) {
                break;
            }

            num_same = 1;
        }
    }

    return scaling;
}

void find_reduce(png_bytep *data, uint32_t left, uint32_t right, int size, int len, uint32_t *left_reduce_ptr, uint32_t *right_reduce_ptr) {
    uint32_t left_reduce = 0, right_reduce = 0;

    for (; left; left--) {
        if (memcmp(data[left - 1], data[left], len) == 0) {
            left_reduce++;
        } else {
            break;
        }
    }

    for (; right < size; right++) {
        if (memcmp(data[right - 1], data[right], len) == 0) {
            right_reduce++;
        } else {
            break;
        }
    }

    *left_reduce_ptr = left_reduce;
    *right_reduce_ptr = right_reduce;
}

int read_png(char *filename) {
    FILE *fp;
    png_structp png_ptr;
    png_infop info_ptr;
    uint32_t x, y;

    if (!(fp = fopen(filename, "rb"))) {
        return 0;
    }

    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    info_ptr = png_create_info_struct(png_ptr);

    png_set_keep_unknown_chunks(png_ptr, PNG_HANDLE_CHUNK_IF_SAFE, chunk_names, 2);
    png_set_read_user_chunk_fn(png_ptr, NULL, (png_user_chunk_ptr)read_chunk);

    if (setjmp(png_jmpbuf(png_ptr))) {
        return 0;
    }

    png_init_io(png_ptr, fp);
    png_read_info(png_ptr, info_ptr);
    width = png_get_image_width(png_ptr, info_ptr);
    height = png_get_image_height(png_ptr, info_ptr);
    png_set_expand(png_ptr);
    png_set_gray_to_rgb(png_ptr);
    png_set_add_alpha(png_ptr, 255, PNG_FILLER_AFTER);
    png_read_update_info(png_ptr, info_ptr);

    rows = (png_bytepp)malloc(height * sizeof(png_bytep));
    for (y = 0; y < height; y++) {
        rows[y] = (png_bytep)malloc(width * 4);
    }

    png_read_image(png_ptr, rows);
    png_read_end(png_ptr, NULL);

    cols = (png_bytepp)malloc(width * sizeof(png_bytep));
    for (x = 0; x < width; x++) {
        cols[x] = (png_bytep)malloc(height * 4);
        for (y = 0; y < height; y++) {
            *((png_uint_32 *)cols[x] + y) = *((png_uint_32 *)rows[y] + x);
        }
    }

    return 1;
}

int process(void) {
    uint32_t x, y, x2, y2;

    if (!nptc_data) {
        return 0;
    }

    num_x_divs = nptc_data[1];
    num_y_divs = nptc_data[2];
    x_divs = (uint32_t *)(nptc_data + 32);
    y_divs = x_divs + num_x_divs;

    for (x = 0, y = num_x_divs + num_y_divs; x < y; x++) {
        x_divs[x] = ntohl(x_divs[x]);
    }

    /* only consider 1 divs for simplicity */
    if (num_x_divs == 2) {
        x_div_width = x_divs[1] - x_divs[0];
        x_scaling = max_scaling(cols + x_divs[0], x_div_width, height * 4);

        if (x_scaling == x_div_width) {
            find_reduce(cols, x_divs[0], x_divs[1], width, height * 4, &left_reduce, &right_reduce);
        }
    }

    if (num_y_divs == 2) {
        y_div_height = y_divs[1] - y_divs[0];
        y_scaling = max_scaling(rows + y_divs[0], y_div_height, width * 4);

        if (y_scaling == y_div_height) {
            find_reduce(rows, y_divs[0], y_divs[1], height, width * 4, &top_reduce, &bottom_reduce);
        }
    }

    if (x_scaling == 1 && y_scaling == 1 && left_reduce == 0 && right_reduce == 0
            && top_reduce == 0 && bottom_reduce == 0) {
        return 0;
    }

    new_width = width - (x_div_width - x_div_width / x_scaling) - left_reduce - right_reduce;
    new_height = height - (y_div_height - y_div_height / y_scaling) - top_reduce - bottom_reduce;

    new_rows = (png_bytepp)malloc(new_height * sizeof(png_bytep));
    for (y = 0; y < new_height; y++) {
        new_rows[y] = (png_bytep)malloc(width * 4);
    }

    y2 = 0;
    for (y = 0; y < height; y++) {
        if ((y >= y_divs[0] - top_reduce && y < y_divs[0])
                || (y >= y_divs[1] && y < y_divs[1] + bottom_reduce)
                || (y >= y_divs[0] && y < y_divs[1] && (y - y_divs[0]) % y_scaling)) {
            continue;
        }
        
        x2 = 0;
        for (x = 0; x < x_divs[0] - left_reduce; x++) {
            *((uint32_t *)new_rows[y2] + x2++) = *((uint32_t *)rows[y] + x);
        }
        for (x = x_divs[0]; x < x_divs[1]; x+= x_scaling) {
            *((uint32_t *)new_rows[y2] + x2++) = *((uint32_t *)rows[y] + x);
        }
        for (x = x_divs[1] + right_reduce; x < width; x++) {
            *((uint32_t *)new_rows[y2] + x2++) = *((uint32_t *)rows[y] + x);
        }

        y2++;
    }

    if (num_x_divs == 2) {
        x_divs[0] -= left_reduce;
        x_divs[1] -= (left_reduce + x_div_width - x_div_width / x_scaling);
    }

    if (num_y_divs == 2) {
        y_divs[0] -= top_reduce;
        y_divs[1] -= (top_reduce + y_div_height - y_div_height / y_scaling);
    }

    for (x = 0, y = num_x_divs + num_y_divs; x < y; x++) {
        x_divs[x] = htonl(x_divs[x]);
    }

    return 1;
}

int write_png(char *filename) {
    FILE *fp;
    png_structp png_ptr;
    png_infop info_ptr;
    png_unknown_chunk unknowns[2];

    if (!(fp = fopen(filename, "wb"))) {
        return 0;
    }

    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    info_ptr = png_create_info_struct(png_ptr);

    png_set_keep_unknown_chunks(png_ptr, PNG_HANDLE_CHUNK_ALWAYS, chunk_names, nplb_data ? 2 : 1);
    
    memcpy(unknowns[0].name, chunk_names, 5);
    unknowns[0].size = nptc_len;
    unknowns[0].data = nptc_data;
    unknowns[0].location = PNG_HAVE_PLTE;

    if (nplb_data) {
        memcpy(unknowns[1].name, chunk_names + 5, 5);
        unknowns[1].size = nplb_len;
        unknowns[1].data = nplb_data;
        unknowns[1].location = PNG_HAVE_PLTE;
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        return 0;
    }

    png_init_io(png_ptr, fp);
    png_set_IHDR(png_ptr, info_ptr, new_width, new_height, 8, PNG_COLOR_TYPE_RGB_ALPHA,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
    png_set_unknown_chunks(png_ptr, info_ptr, unknowns, nplb_data ? 2 : 1);

    png_write_info(png_ptr, info_ptr);
    png_write_image(png_ptr, new_rows);
    png_write_end(png_ptr, NULL);

    return 1;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s input-png output-png\n", argv[0]);
        exit(1);
    }

    if (!read_png(argv[1])) {
        fprintf(stderr, "opti9png: read error\n");
        exit(1);
    }

    if (!process()) {
        fprintf(stderr, "opti9png: not processed\n");
        exit(1);
    }

    if (!write_png(argv[2])) {
        fprintf(stderr, "opti9png: write error\n");
        exit(1);
    }

    printf("width: %d, height: %d, new_width: %d, new_height: %d, x_scaling: %d, y_scaling: %d, left_reduce: %d, right_reduce: %d, top_reduce: %d, bottom_reduce: %d\n",
            width, height, new_width, new_height, x_scaling, y_scaling, left_reduce, right_reduce, top_reduce, bottom_reduce);

    return 0;
}
