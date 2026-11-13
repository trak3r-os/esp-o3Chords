#pragma once

// Golden framebuffer snapshots. A golden file is a literal
// picture of what the OLED would show: '#' = lit pixel, '.' = unlit, one
// 128-character line per row, top to bottom — a screen rendered through
// the real drivers::fb:: pixel math (shared by the hardware driver and the
// native test stub, per oled_framebuffer.h) dumped exactly as the panel
// would show it.
//
// Update path is explicit only: set UPDATE_GOLDEN=1 in the environment to
// (re)write a golden file instead of comparing against it. Never do this to
// make a failing test pass without first confirming the new pixels are
// actually correct — the whole point of a golden is that it does not drift
// silently.
#include "drivers/oled_ssd13xx.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

namespace golden {

inline std::string dump_screen(const uint8_t* buf, uint8_t width, uint8_t height) {
  std::string out;
  out.reserve(static_cast<size_t>(width + 1) * height);
  for (uint8_t y = 0; y < height; ++y) {
    for (uint8_t x = 0; x < width; ++x) {
      const uint8_t page = y >> 3, bit = y & 7;
      const bool on = (buf[static_cast<size_t>(page) * width + x] >> bit) & 1;
      out += on ? '#' : '.';
    }
    out += '\n';
  }
  return out;
}

inline std::string dump_screen(const drivers::OledSsd13xx& oled) {
  return dump_screen(oled.framebuffer(), oled.framebuffer_width(), oled.framebuffer_height());
}

// test/CMakeLists.txt passes the absolute test/golden/ directory in
// O3_GOLDEN_DIR, so the snapshots resolve from any build directory.
#ifndef O3_GOLDEN_DIR
#define O3_GOLDEN_DIR "../golden"
#endif
inline std::string golden_path(const char* name) {
  return std::string(O3_GOLDEN_DIR "/") + name + ".txt";
}

// Compares `actual` against test/golden/<name>.txt, or rewrites it when
// UPDATE_GOLDEN=1 is set. Returns true on match (or successful rewrite);
// on mismatch or a missing file, fills `message` with a reason to print.
inline bool check(const char* name, const std::string& actual, std::string& message) {
  const std::string path = golden_path(name);
  if (std::getenv("UPDATE_GOLDEN")) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) { message = "could not write golden file: " + path; return false; }
    out << actual;
    return true;
  }
  std::ifstream in(path);
  if (!in) {
    message = "golden file missing: " + path +
               " (run with UPDATE_GOLDEN=1 to create it, after confirming the pixels are correct)";
    return false;
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  const std::string expected = ss.str();
  if (expected != actual) {
    message = "golden mismatch: " + path + " (run with UPDATE_GOLDEN=1 to update, after confirming the new pixels are correct)";
    return false;
  }
  return true;
}

}  // namespace golden
