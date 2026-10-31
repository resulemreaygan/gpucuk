// SPDX-License-Identifier: MIT
/**
 * @file Canvas.h
 * @brief Portrait drawing surface over the panel's landscape 1-bit framebuffer.
 *
 * Same rotation as CrossPoint Reader's GfxRenderer::Portrait. Plain C++, so it also runs on the
 * computer (tools/preview).
 *
 * @author Resul Emre Aygan
 */
#pragma once
#include <stdint.h>

#include "Font.h"

/** Draws pixels, lines and UTF-8 text in portrait coordinates. */
class Canvas {
 public:
  /**
   * @brief Uses an existing framebuffer.
   * @param frameBuffer Landscape framebuffer, 1 bit per pixel, 1 = white.
   * @param panelWidth Panel width in its native (landscape) orientation.
   * @param panelHeight Panel height in its native orientation.
   */
  void attach(uint8_t* frameBuffer, uint16_t panelWidth, uint16_t panelHeight);

  /** @return Portrait width in pixels. */
  int width() const { return panelHeight_; }
  /** @return Portrait height in pixels. */
  int height() const { return panelWidth_; }

  /** @brief Fills the whole surface with white. */
  void clear();

  /**
   * @brief Sets one pixel; coordinates outside the surface are ignored.
   * @param x Column.
   * @param y Row.
   * @param black True for black, false for white.
   */
  void pixel(int x, int y, bool black);

  /**
   * @brief Fills a rectangle.
   * @param x Left edge.
   * @param y Top edge.
   * @param w Width.
   * @param h Height.
   * @param black True for black, false for white.
   */
  void fillRect(int x, int y, int w, int h, bool black);

  /**
   * @brief Draws a black horizontal line.
   * @param x Left end.
   * @param y Top row.
   * @param w Length.
   * @param thickness Height in pixels.
   */
  void hLine(int x, int y, int w, int thickness = 1) { fillRect(x, y, w, thickness, true); }

  /**
   * @brief Draws UTF-8 text; characters missing from the font are drawn as '?'.
   * @param x Pen start.
   * @param y Baseline.
   * @param utf8 Text.
   * @param font Font.
   * @return Pen x after the last glyph.
   */
  int text(int x, int y, const char* utf8, const Font& font);

  /**
   * @brief Measures UTF-8 text.
   * @param utf8 Text.
   * @param font Font.
   * @return Width in pixels.
   */
  int textWidth(const char* utf8, const Font& font) const;

  /**
   * @brief Draws UTF-8 text centered horizontally.
   * @param y Baseline.
   * @param utf8 Text.
   * @param font Font.
   */
  void textCentered(int y, const char* utf8, const Font& font);

  /**
   * @brief Draws UTF-8 text word-wrapped into [x, x + maxWidth); '\n' starts a new line.
   * @param x Left edge.
   * @param y Baseline of the first line.
   * @param maxWidth Available width.
   * @param utf8 Text.
   * @param font Font.
   * @return Baseline y of the line after the text.
   */
  int textWrapped(int x, int y, int maxWidth, const char* utf8, const Font& font);

 private:
  const Glyph* findGlyph(const Font& font, uint32_t codepoint) const;
  void drawGlyph(int penX, int baseline, const Glyph& g, const Font& font);

  uint8_t* fb_ = nullptr;
  uint16_t panelWidth_ = 0;
  uint16_t panelHeight_ = 0;
  uint16_t panelWidthBytes_ = 0;
};
