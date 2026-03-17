#ifndef STB_IMAGE_H
#define STB_IMAGE_H

#include <cstdlib>

#ifndef STBI_rgb_alpha
#define STBI_rgb_alpha 4
#endif

void stbi_set_flip_vertically_on_load(int flag_true_if_should_flip);
unsigned char* stbi_load(const char* filename, int* x, int* y, int* comp, int req_comp);
unsigned char* stbi_load_from_memory(const unsigned char* buffer, int len, int* x, int* y,
                                     int* comp, int req_comp);
const char* stbi_failure_reason(void);
void stbi_image_free(void* retval_from_stbi_load);

#ifdef STB_IMAGE_IMPLEMENTATION

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <png.h>

namespace {

struct StbiMemoryReader {
  const unsigned char* data = nullptr;
  size_t size = 0;
  size_t offset = 0;
};

int stbi_g_flip_vertically_on_load = 0;
std::string stbi_g_failure_reason = "unknown";

void stbi_set_failure_reason(const char* reason) {
  stbi_g_failure_reason = reason ? reason : "unknown";
}

void stbi_png_read(png_structp png_ptr, png_bytep out_bytes, png_size_t byte_count_to_read) {
  auto* reader = static_cast<StbiMemoryReader*>(png_get_io_ptr(png_ptr));
  if (!reader || reader->offset + byte_count_to_read > reader->size) {
    png_error(png_ptr, "png read overflow");
    return;
  }
  std::memcpy(out_bytes, reader->data + reader->offset, byte_count_to_read);
  reader->offset += byte_count_to_read;
}

unsigned char* stbi_load_png_bytes(const unsigned char* buffer, int len, int* x, int* y, int* comp,
                                   int req_comp) {
  if (!buffer || len <= 0) {
    stbi_set_failure_reason("invalid image buffer");
    return nullptr;
  }
  if (req_comp != 0 && req_comp != STBI_rgb_alpha) {
    stbi_set_failure_reason("only RGBA output is supported");
    return nullptr;
  }
  if (len < 8 || png_sig_cmp(const_cast<png_bytep>(buffer), 0, 8) != 0) {
    stbi_set_failure_reason("not a PNG file");
    return nullptr;
  }

  png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  if (!png) {
    stbi_set_failure_reason("png_create_read_struct failed");
    return nullptr;
  }

  png_infop info = png_create_info_struct(png);
  if (!info) {
    png_destroy_read_struct(&png, nullptr, nullptr);
    stbi_set_failure_reason("png_create_info_struct failed");
    return nullptr;
  }

  if (setjmp(png_jmpbuf(png)) != 0) {
    png_destroy_read_struct(&png, &info, nullptr);
    stbi_set_failure_reason("PNG decode failed");
    return nullptr;
  }

  StbiMemoryReader reader{buffer, static_cast<size_t>(len), 0};
  png_set_read_fn(png, &reader, stbi_png_read);
  png_read_info(png, info);

  int width = static_cast<int>(png_get_image_width(png, info));
  int height = static_cast<int>(png_get_image_height(png, info));
  int bit_depth = png_get_bit_depth(png, info);
  int color_type = png_get_color_type(png, info);

  if (bit_depth == 16) png_set_strip_16(png);
  if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
  if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) {
    png_set_expand_gray_1_2_4_to_8(png);
  }
  if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
  if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
    png_set_gray_to_rgb(png);
  }
  if (!(color_type & PNG_COLOR_MASK_ALPHA)) {
    png_set_add_alpha(png, 0xFF, PNG_FILLER_AFTER);
  }

  png_read_update_info(png, info);

  const size_t row_bytes = static_cast<size_t>(width) * static_cast<size_t>(STBI_rgb_alpha);
  const size_t total_bytes = row_bytes * static_cast<size_t>(height);
  auto* pixels = static_cast<unsigned char*>(std::malloc(total_bytes));
  if (!pixels) {
    png_destroy_read_struct(&png, &info, nullptr);
    stbi_set_failure_reason("out of memory");
    return nullptr;
  }

  std::vector<png_bytep> rows(static_cast<size_t>(height));
  for (int row = 0; row < height; ++row) {
    const int dst_row = stbi_g_flip_vertically_on_load ? (height - 1 - row) : row;
    rows[static_cast<size_t>(row)] = pixels + static_cast<size_t>(dst_row) * row_bytes;
  }

  png_read_image(png, rows.data());
  png_read_end(png, info);
  png_destroy_read_struct(&png, &info, nullptr);

  if (x) *x = width;
  if (y) *y = height;
  if (comp) *comp = STBI_rgb_alpha;
  stbi_set_failure_reason("ok");
  return pixels;
}

}  // namespace

void stbi_set_flip_vertically_on_load(int flag_true_if_should_flip) {
  stbi_g_flip_vertically_on_load = flag_true_if_should_flip;
}

unsigned char* stbi_load(const char* filename, int* x, int* y, int* comp, int req_comp) {
  if (!filename) {
    stbi_set_failure_reason("invalid filename");
    return nullptr;
  }

  std::ifstream file(filename, std::ios::binary);
  if (!file) {
    stbi_set_failure_reason("could not open image");
    return nullptr;
  }

  std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(file),
                                   std::istreambuf_iterator<char>()};
  if (bytes.empty()) {
    stbi_set_failure_reason("image file is empty");
    return nullptr;
  }

  return stbi_load_png_bytes(bytes.data(), static_cast<int>(bytes.size()), x, y, comp, req_comp);
}

unsigned char* stbi_load_from_memory(const unsigned char* buffer, int len, int* x, int* y,
                                     int* comp, int req_comp) {
  return stbi_load_png_bytes(buffer, len, x, y, comp, req_comp);
}

const char* stbi_failure_reason(void) { return stbi_g_failure_reason.c_str(); }

void stbi_image_free(void* retval_from_stbi_load) { std::free(retval_from_stbi_load); }

#endif  // STB_IMAGE_IMPLEMENTATION

#endif  // STB_IMAGE_H
