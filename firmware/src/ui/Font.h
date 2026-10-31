// SPDX-License-Identifier: MIT
/**
 * @file Font.h
 * @brief 1-bit bitmap font format written by tools/make_font.py.
 * @author Resul Emre Aygan
 */
#pragma once
#include <stdint.h>

/** One character of a bitmap font. */
struct Glyph {
  uint16_t codepoint;     /**< Unicode code point. */
  uint8_t width;          /**< Bitmap width in pixels. */
  uint8_t height;         /**< Bitmap height in pixels. */
  int8_t xOffset;         /**< From the pen position. */
  int8_t yOffset;         /**< From the baseline; negative is above it. */
  uint8_t advance;        /**< Pen movement after the glyph. */
  uint32_t bitmapOffset;  /**< Into Font::bitmap; rows are byte-aligned, most significant bit first. */
};

/** A bitmap font at one pixel size. */
struct Font {
  const Glyph* glyphs;    /**< Sorted by code point. */
  uint16_t glyphCount;    /**< Number of glyphs. */
  const uint8_t* bitmap;  /**< All glyph rows. */
  uint8_t lineHeight;     /**< Distance between baselines. */
  uint8_t ascent;         /**< Height above the baseline. */
};
