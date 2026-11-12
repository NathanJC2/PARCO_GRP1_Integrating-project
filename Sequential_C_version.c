#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

// reads one token at a time from a PGM image file
static int read_pgm_token(FILE *file, char *token, size_t token_size)
{
    int ch;
    size_t length = 0;
    // skips the whitespace and comments until valid token is found
    do {
        ch = fgetc(file);
        if (ch == '#') {
            do {
                ch = fgetc(file);
            } while (ch != '\n' && ch != EOF);
        }
    } while (ch != EOF && isspace((unsigned char)ch));
    // checks for end of file
    if (ch == EOF) {
        return 0;
    }
    // extracts tokens until whitespace of eof is found
    while (ch != EOF && !isspace((unsigned char)ch)) {
        if (length + 1 >= token_size) {
            return 0;
        }
        token[length++] = (char)ch;
        ch = fgetc(file);
    }
    // handles windows-style line endings
    if (ch == '\r') {
        int next = fgetc(file);
        if (next != '\n' && next != EOF) {
            ungetc(next, file);
        }
    }
    // terminates the token string
    token[length] = '\0';
    return length > 0;
}

// Converts string into numerical value representing width or height
static int parse_size(const char *token, size_t *value)
{
    char *end;
    unsigned long long parsed;
    // convert the string into unsigned long long integer
    errno = 0;
    parsed = strtoull(token, &end, 10);
    // validatation of converted dimension
    if (errno != 0 || *token == '\0' || *token == '-' || *end != '\0' ||
        parsed == 0 || parsed > SIZE_MAX) {
        return 0;
    }
    // store the results
    *value = (size_t)parsed;
    return 1;
}

// converts the string into grayscale
static int parse_pixel(const char *token, uint8_t *value)
{
    char *end;
    unsigned long parsed;
    // convert the string into unsigned long integer
    errno = 0;
    parsed = strtoul(token, &end, 10);
    // validation
    if (errno != 0 || *token == '\0' || *token == '-' || *end != '\0' ||
        parsed > 255) {
        return 0;
    }
    // storing
    *value = (uint8_t)parsed;
    return 1;
}

// loads PGM image from file into memory
static int read_pgm(const char *path, uint8_t **pixels,
                    size_t *width, size_t *height)
{
    FILE *file = fopen(path, "rb");
    char token[64];
    size_t pixel_count;
    uint8_t *data = NULL;
    int is_binary;
    int success = 0;
    // checks if file can be opened
    if (file == NULL) {
        fprintf(stderr, "Cannot open input image '%s'.\n", path);
        return 0;
    }
    // read the image format
    if (!read_pgm_token(file, token, sizeof(token))) {
        fprintf(stderr, "Invalid PGM file '%s': missing image format.\n", path);
        goto cleanup;
    }
    if (strcmp(token, "P5") == 0) {
        is_binary = 1;
    } else if (strcmp(token, "P2") == 0) {
        is_binary = 0;
    } else {
        fprintf(stderr, "Unsupported image format in '%s' (expected P2 or P5 PGM).\n",
                path);
        goto cleanup;
    }
    // read the image dimensions
    if (!read_pgm_token(file, token, sizeof(token)) ||
        !parse_size(token, width) ||
        !read_pgm_token(file, token, sizeof(token)) ||
        !parse_size(token, height)) {
        fprintf(stderr, "Invalid PGM dimensions in '%s'.\n", path);
        goto cleanup;
    }
    // checker for integer overflow
    if (*width > SIZE_MAX / *height) {
        fprintf(stderr, "Image dimensions in '%s' are too large.\n", path);
        goto cleanup;
    }
    // calculate the total pixel count
    pixel_count = *width * *height;

    // Read the maximum pixel value
    if (!read_pgm_token(file, token, sizeof(token)) ||
        strcmp(token, "255") != 0) {
        fprintf(stderr, "Unsupported maximum pixel value in '%s' (expected 255).\n",
                path);
        goto cleanup;
    }

    // allocates memory
    data = (uint8_t *)malloc(pixel_count);
    
    if (data == NULL) {
        fprintf(stderr, "Not enough memory to read '%s'.\n", path);
        goto cleanup;
    }
    // read binary pixel data
    if (is_binary) {
        if (fread(data, 1, pixel_count, file) != pixel_count) {
            fprintf(stderr, "Image data in '%s' is incomplete.\n", path);
            goto cleanup;
        }

    // read ascii pixel data
    } else {
        size_t i;
        for (i = 0; i < pixel_count; ++i) {
            if (!read_pgm_token(file, token, sizeof(token)) ||
                !parse_pixel(token, &data[i])) {
                fprintf(stderr, "Invalid or incomplete pixel data in '%s'.\n", path);
                goto cleanup;
            }
        }
    }

    // Return the image data
    *pixels = data;
    data = NULL;
    success = 1;

    // releases allocated memory and closes the file yah
cleanup:
    free(data);
    fclose(file);
    return success;
}

// saves processed image into pgm file
static int write_pgm(const char *path, const uint8_t *pixels,
                     size_t width, size_t height)
{
    // opens file
    FILE *file = fopen(path, "wb");
    size_t pixel_count = width * height;
    int success = 0;
    // checker
    if (file == NULL) {
        fprintf(stderr, "Cannot open output image '%s'.\n", path);
        return 0;
    }
    // write the image header and pixel data
    if (fprintf(file, "P5\n%zu %zu\n255\n", width, height) < 0 ||
        fwrite(pixels, 1, pixel_count, file) != pixel_count) {
        fprintf(stderr, "Failed to write output image '%s'.\n", path);
    } else {
        success = 1;
    }
    // close the file
    if (fclose(file) != 0) {
        fprintf(stderr, "Failed to finish writing output image '%s'.\n", path);
        success = 0;
    }
    return success;
}

// performs the actual sobel edfe detection
int sobel_sequential(const uint8_t *input, uint8_t *output,
                     size_t width, size_t height)
{
    size_t y; // tracks the rows being processed
    size_t pixel_count; // stores the total number of image pixels

    // checker for valid inputs
    if (input == NULL || output == NULL || width == 0 || height == 0 ||
        width > SIZE_MAX / height || input == output) {
        return 0;
    }

    // total number of pixels
    pixel_count = width * height;
    // initialize the output buffer
    memset(output, 0, pixel_count);
    // handle images smaller than 3x3
    if (width < 3 || height < 3) {
        return 1;
    }

    // iterate over the image rows, declare the row offets, iterate through colums
    for (y = 1; y < height - 1; ++y) {
        size_t x;
        size_t top = (y - 1) * width;
        size_t middle = y * width;
        size_t bottom = (y + 1) * width;

        for (x = 1; x < width - 1; ++x) {
            // calculate gradients in x and y
            int gx =
                -(int)input[top + x - 1] + (int)input[top + x + 1]
                - 2 * (int)input[middle + x - 1]
                + 2 * (int)input[middle + x + 1]
                -(int)input[bottom + x - 1] + (int)input[bottom + x + 1];
            int gy =
                -(int)input[top + x - 1] - 2 * (int)input[top + x]
                -(int)input[top + x + 1]
                +(int)input[bottom + x - 1] + 2 * (int)input[bottom + x]
                +(int)input[bottom + x + 1];

            // calculate the sequred magnitude
            uint32_t squared_magnitude =
                (uint32_t)(gx * gx) + (uint32_t)(gy * gy);
            // calculate actual magnitude
            uint32_t magnitude =
                (uint32_t)(sqrt((double)squared_magnitude) + 0.5);

            // final pixel
            output[middle + x] = (uint8_t)(magnitude > 255 ? 255 : magnitude);
        }
    }

    return 1;
}

int main(int argc, char **argv)
{
    uint8_t *input = NULL;
    uint8_t *output = NULL;
    size_t width;
    size_t height;
    clock_t start;
    clock_t end;

    if (argc != 3) {
        fprintf(stderr, "Usage: %s <input.pgm> <output.pgm>\n", argv[0]);
        return EXIT_FAILURE;
    }

    // load image
    if (!read_pgm(argv[1], &input, &width, &height)) {
        return EXIT_FAILURE;
    }

    // allocate output memory & memory checker
    output = (uint8_t *)malloc(width * height);
    if (output == NULL) {
        fprintf(stderr, "Not enough memory to create the output image.\n");
        free(input);
        return EXIT_FAILURE;
    }

    // start clock and sobel algorithm
    start = clock();
    if (!sobel_sequential(input, output, width, height)) {
        fprintf(stderr, "Could not process the image.\n");
        free(output);
        free(input);
        return EXIT_FAILURE;
    }
    end = clock(); // ends the clock

    // saved the output image
    if (!write_pgm(argv[2], output, width, height)) {
        free(output);
        free(input);
        return EXIT_FAILURE;
    }

    // print the time taken for algorithm
    if (start != (clock_t)-1 && end != (clock_t)-1) {
        printf("Sequential Sobel kernel: %.6f seconds (%zu x %zu)\n",
               (double)(end - start) / CLOCKS_PER_SEC, width, height);
    } else {
        printf("Sequential Sobel kernel completed (%zu x %zu); timing unavailable.\n",
               width, height);
    }

    free(output);
    free(input);
    return EXIT_SUCCESS;
}