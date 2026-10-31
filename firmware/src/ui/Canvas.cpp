// SPDX-License-Identifier: MIT
/**
 * @file Canvas.cpp
 * @brief Pixel, rectangle and UTF-8 text drawing for Canvas.
 * @author Resul Emre Aygan
 */
#include "Canvas.h"

#include <stdio.h>
#include <string.h>

namespace {

// Decodes one UTF-8 sequence; invalid bytes come back as U+FFFD.
uint32_t nextCodepoint(const char*& s) {
  const auto* p = reinterpret_cast<const uint8_t*>(s);
  uint32_t cp;
  int extra;
  if (p[0] < 0x80) {
    cp = p[0];
    extra = 0;
  } else if ((p[0] & 0xE0) == 0xC0) {
    cp = p[0] & 0x1F;
    extra = 1;
  } else if ((p[0] & 0xF0) == 0xE0) {
    cp = p[0] & 0x0F;
    extra = 2;
  } else if ((p[0] & 0xF8) == 0xF0) {
    cp = p[0] & 0x07;
    extra = 3;
  } else {
    s += 1;
    return 0xFFFD;
  }
  for (int i = 1; i <= extra; i++) {
    if ((p[i] & 0xC0) != 0x80) {
      s += i;
      return 0xFFFD;
    }
    cp = (cp << 6) | (p[i] & 0x3F);
  }
  s += extra + 1;
  return cp;
}

}  // namespace

void Canvas::attach(uint8_t* frameBuffer, uint16_t panelWidth, uint16_t panelHeight) {
  fb_ = frameBuffer;
  panelWidth_ = panelWidth;
  panelHeight_ = panelHeight;
  panelWidthBytes_ = panelWidth / 8;
}

void Canvas::clear() { memset(fb_, 0xFF, static_cast<size_t>(panelWidthBytes_) * panelHeight_); }

void Canvas::pixel(int x, int y, bool black) {
  if (x < 0 || y < 0 || x >= width() || y >= height()) return;
  const int phyX = y;
  const int phyY = panelHeight_ - 1 - x;
  uint8_t& byte = fb_[phyY * panelWidthBytes_ + (phyX >> 3)];
  const uint8_t bit = 0x80 >> (phyX & 7);
  if (black) {
    byte &= ~bit;
  } else {
    byte |= bit;
  }
}

void Canvas::fillRect(int x, int y, int w, int h, bool black) {
  for (int yy = y; yy < y + h; yy++) {
    for (int xx = x; xx < x + w; xx++) pixel(xx, yy, black);
  }
}

const Glyph* Canvas::findGlyph(const Font& font, uint32_t codepoint) const {
  int lo = 0;
  int hi = font.glyphCount - 1;
  while (lo <= hi) {
    const int mid = (lo + hi) / 2;
    const uint16_t cp = font.glyphs[mid].codepoint;
    if (cp == codepoint) return &font.glyphs[mid];
    if (cp < codepoint) {
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }
  return codepoint == '?' ? nullptr : findGlyph(font, '?');
}

void Canvas::drawGlyph(int penX, int baseline, const Glyph& g, const Font& font) {
  const int rowBytes = (g.width + 7) / 8;
  const uint8_t* rows = font.bitmap + g.bitmapOffset;
  for (int gy = 0; gy < g.height; gy++) {
    const uint8_t* row = rows + gy * rowBytes;
    for (int gx = 0; gx < g.width; gx++) {
      if (row[gx >> 3] & (0x80 >> (gx & 7))) pixel(penX + g.xOffset + gx, baseline + g.yOffset + gy, true);
    }
  }
}

int Canvas::text(int x, int y, const char* utf8, const Font& font) {
  while (*utf8) {
    const Glyph* g = findGlyph(font, nextCodepoint(utf8));
    if (!g) continue;
    drawGlyph(x, y, *g, font);
    x += g->advance;
  }
  return x;
}

int Canvas::textWidth(const char* utf8, const Font& font) const {
  int w = 0;
  while (*utf8) {
    const Glyph* g = findGlyph(font, nextCodepoint(utf8));
    if (g) w += g->advance;
  }
  return w;
}

void Canvas::textCentered(int y, const char* utf8, const Font& font) {
  text((width() - textWidth(utf8, font)) / 2, y, utf8, font);
}

int Canvas::textWrapped(int x, int y, int maxWidth, const char* utf8, const Font& font) {
  char line[256] = {0};
  char word[128] = {0};
  const char* p = utf8;
  while (true) {
    const char* start = p;
    while (*p && *p != ' ' && *p != '\n') p++;
    const size_t wordLen = p - start;
    if (wordLen >= sizeof(word)) return y;
    memcpy(word, start, wordLen);
    word[wordLen] = '\0';

    char candidate[sizeof(line) + sizeof(word) + 1];
    snprintf(candidate, sizeof(candidate), "%s%s%s", line, line[0] ? " " : "", word);
    if (line[0] && textWidth(candidate, font) > maxWidth) {
      text(x, y, line, font);
      y += font.lineHeight;
      strncpy(line, word, sizeof(line) - 1);
    } else {
      strncpy(line, candidate, sizeof(line) - 1);
    }

    if (*p == '\n' || *p == '\0') {
      text(x, y, line, font);
      y += font.lineHeight;
      line[0] = '\0';
      if (*p == '\0') return y;
    }
    p++;
  }
}
