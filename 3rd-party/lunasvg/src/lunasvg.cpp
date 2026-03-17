#include "lunasvg.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvg.h"
#include "nanosvgrast.h"

namespace lunasvg {

Bitmap::Bitmap(int width, int height, int stride, std::vector<unsigned char> pixels)
    : width_(width), height_(height), stride_(stride), pixels_(std::move(pixels)) {}

unsigned char* Bitmap::data() { return pixels_.empty() ? nullptr : pixels_.data(); }

const unsigned char* Bitmap::data() const { return pixels_.empty() ? nullptr : pixels_.data(); }

int Bitmap::width() const { return width_; }

int Bitmap::height() const { return height_; }

int Bitmap::stride() const { return stride_; }

void Bitmap::convertToRGBA() {}

Document::Document(std::string svg) : svg_(std::move(svg)) {}

std::unique_ptr<Document> Document::loadFromFile(const std::string& path) {
  std::ifstream file(path);
  if (!file) return nullptr;

  std::string svg{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  if (svg.empty()) return nullptr;
  return std::unique_ptr<Document>(new Document(std::move(svg)));
}

Bitmap Document::renderToBitmap() const { return renderToBitmap(0, 0); }

Bitmap Document::renderToBitmap(int width, int height) const {
  if (svg_.empty()) return {};

  std::vector<char> xml(svg_.begin(), svg_.end());
  xml.push_back('\0');
  char units[] = "px";

  NSVGimage* image = nsvgParse(xml.data(), units, 96.0f);
  if (!image) return {};

  const float image_w = std::max(image->width, 1.0f);
  const float image_h = std::max(image->height, 1.0f);

  float scale = 1.0f;
  if (width > 0 && height > 0) {
    scale = std::min(static_cast<float>(width) / image_w, static_cast<float>(height) / image_h);
  } else if (width > 0) {
    scale = static_cast<float>(width) / image_w;
  } else if (height > 0) {
    scale = static_cast<float>(height) / image_h;
  }
  scale = std::max(scale, 0.0001f);

  const int out_w = width > 0 ? width : std::max(1, static_cast<int>(std::lround(image_w * scale)));
  const int out_h = height > 0 ? height : std::max(1, static_cast<int>(std::lround(image_h * scale)));
  const int stride = out_w * 4;

  std::vector<unsigned char> pixels(static_cast<size_t>(stride) * static_cast<size_t>(out_h), 0);

  NSVGrasterizer* rasterizer = nsvgCreateRasterizer();
  if (rasterizer) {
    nsvgRasterize(rasterizer, image, 0.0f, 0.0f, scale, pixels.data(), out_w, out_h, stride);
    nsvgDeleteRasterizer(rasterizer);
  }

  nsvgDelete(image);
  return Bitmap(out_w, out_h, stride, std::move(pixels));
}

}  // namespace lunasvg
