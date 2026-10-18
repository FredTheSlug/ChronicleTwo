#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>

std::filesystem::path MoviePortPath(const char *name);

// Driver port 5: GetCSndPortNo never maps a game port to it, so no sequence or sound effect plays there during a movie.
inline constexpr int kMoviePortAudioPort = 5;

// The movie time the playing movie last advanced to, in 90 kHz units.
int64_t MoviePortClock();

// How long past its container's length a movie may wait for pictures before it ends anyway: 2 s in 90 kHz units.
inline constexpr int64_t kMoviePortOverrun = 2 * 90000;

// True once a movie of length (90 kHz units, 0 when unknown) has played kMoviePortOverrun past it.
bool MoviePortOverran(int64_t elapsed, int64_t length);

// The host time the real-time movie clock reads; null restores std::chrono::steady_clock::now. For tests.
using MoviePortNow = std::chrono::steady_clock::time_point (*)();
void MoviePortSetNow(MoviePortNow now);
