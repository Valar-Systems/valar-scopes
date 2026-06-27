#pragma once

// Screen geometry for the active variant. Replaces the per-file `constexpr int SCREEN_SIZE`
// that used to be duplicated in main.cpp and AircraftManager.cpp, so the radar UI scales to
// whatever round panel the SKU carries (240 on the C3, 412/466/480 on the S3 boards).

#include "variants/Variant.h"

constexpr int SCREEN_SIZE = variant::SCREEN_SIZE;
constexpr int SCREEN_SIZE_DIV_2 = SCREEN_SIZE / 2;

// Banded rendering: heap-tight single-core boards (C3) draw the frame as N half-height
// bands into a small backbuffer to free the contiguous heap a TLS handshake needs. Boards
// with PSRAM for a full framebuffer render in a single full-screen band. Must divide SCREEN_SIZE.
constexpr int BAND_H = variant::BANDED_RENDER ? (SCREEN_SIZE / 2) : SCREEN_SIZE;
