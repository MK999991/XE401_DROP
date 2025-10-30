#ifndef MILES_CODES_H
#define MILES_CODES_H

#include <cstdint>

struct MILES_Code {
    const char* description;
    uint8_t pattern[11]; // 11-bit MILES word; index 0..10
};

// ---- Example codes (replace with your real ones) ----
const MILES_Code PLAYER_UNIVERSAL_KILL = {
    "Universal Kill",
    {1, 1, 0, 0, 0, 1, 0, 1, 1, 0, 1}
};

const MILES_Code PLAYER_ID_001 = {
    "Player ID 001",
    {1, 0, 0, 1, 0, 0, 1, 1, 0, 1, 0}
};

const MILES_Code PLAYER_ID_002 = {
    "Player ID 002",
    {1, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0}
};

const MILES_Code EVENT_PAUSE = {
    "Pause / Reset",
    {1, 1, 0, 0, 0, 1, 0, 1, 0, 1, 1}
};

const MILES_Code EVENT_END_EXERCISE = {
    "End Exercise",
    {1, 1, 0, 0, 0, 1, 1, 1, 1, 1, 0}
};

#endif // MILES_CODES_H
