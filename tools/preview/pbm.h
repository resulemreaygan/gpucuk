// SPDX-License-Identifier: MIT
/**
 * @file pbm.h
 * @brief Writes the X3 framebuffer to a PBM file, turned to portrait as the user sees the screen.
 * @author Resul Emre Aygan
 */
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "ui/Canvas.h"

constexpr int kPanelW = 792;  /**< Panel width in its native (landscape) orientation. */
constexpr int kPanelH = 528;  /**< Panel height in its native (landscape) orientation. */

/**
 * @brief Writes a framebuffer as a portrait PBM image; exits on error.
 * @param path Output file.
 * @param c Canvas attached to the framebuffer (gives the portrait size).
 * @param fb Framebuffer: 1 bit per pixel, MSB first, 1 = white, landscape.
 */
inline void writePbm(const char* path, const Canvas& c, const uint8_t* fb) {
  FILE* f = fopen(path, "wb");
  if (!f) {
    perror(path);
    exit(1);
  }
  fprintf(f, "P1\n%d %d\n", c.width(), c.height());
  for (int y = 0; y < c.height(); y++) {
    for (int x = 0; x < c.width(); x++) {
      const int phyX = y;
      const int phyY = kPanelH - 1 - x;
      const bool white = fb[phyY * (kPanelW / 8) + phyX / 8] & (0x80 >> (phyX % 8));
      fputc(white ? '0' : '1', f);
    }
    fputc('\n', f);
  }
  fclose(f);
}
