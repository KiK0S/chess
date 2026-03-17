#pragma once

#include <memory>
#include <string>
#include <vector>

namespace lunasvg {

class Bitmap {
 public:
  Bitmap() = default;
  Bitmap(int width, int height, int stride, std::vector<unsigned char> pixels);

  unsigned char* data();
  const unsigned char* data() const;
  int width() const;
  int height() const;
  int stride() const;
  void convertToRGBA();

 private:
  int width_ = 0;
  int height_ = 0;
  int stride_ = 0;
  std::vector<unsigned char> pixels_{};
};

class Document {
 public:
  static std::unique_ptr<Document> loadFromFile(const std::string& path);

  Bitmap renderToBitmap() const;
  Bitmap renderToBitmap(int width, int height) const;

 private:
  explicit Document(std::string svg);

  std::string svg_{};
};

}  // namespace lunasvg
