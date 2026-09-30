#pragma once
#include "rgb_matrix_types.h"

#define __ NO_LED

led_config_t g_led_config = {
    {
        // Key Matrix to LED Index
        { 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15 },
        { 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31 },   
        { 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, __, 46 },
        { 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, __, 59, __, 60 },
        { 61, __, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, __, 73, 74 },
        { 75, 76, 77, 78, __, __, 79, __, __, 80, 81, 82, 83, 84, 85, 86 }
    },
    {
        // LED Index to Physical Position
        {0,0},  {15,0},  {30,0},  {45,0},  {60,0},  {75,0},  {90,0},  {105,0},  {120,0},  {135,0},  {150,0},  {165,0},  {180,0},    {195,0},   {210,0},   {225,0},
        {0,15}, {15,15}, {30,15}, {45,15}, {60,15}, {75,15}, {90,15}, {105,15}, {120,15}, {135,15}, {150,15}, {165,15}, {180,15},   {195,15},  {210,15},  {225,15},
        {4,26}, {22,26}, {37,26}, {52,26}, {67,26}, {82,26}, {97,26}, {112,26}, {127,26}, {142,26}, {157,26}, {172,26}, {187,26},              {202,26},  {225,26},
        {6,38}, {26,38}, {41,38}, {56,38}, {71,38}, {86,38}, {101,38}, {116,38}, {131,38}, {146,38}, {161,38}, {176,38},                        {191,38},  {225,38},
        {2,49},          {33,49}, {48,49}, {62,49}, {77,49}, {92,49}, {107,49}, {123,49}, {138,49}, {153,49}, {168,49}, {183,49},  {198,49},  {213,49},
        {2,61}, {19,61}, {36,61}, {51,61},                   {88,61},                     {135,61}, {150,61}, {165,61}, {180,61},  {195,64},   {210,64},  {225,64}
    },
    {
        // RGB LED Index to Flag
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 1, 1,
        1, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,    1,
        8, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,    1,    1,
        1,    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 1,    1, 1,
        1, 1, 1, 1,       4,       1, 1, 1, 1, 1, 1, 1
    }
};
// Default Color of Per Key RGB
#ifdef CONFIG_KEYCHRON_RGB_ENABLE
#define DC_RED {  0, 255, 255}
#define DC_BLU {170, 255, 255}
#define DC_YLW {43, 255, 255}

HSV default_per_key_led[RGB_MATRIX_LED_COUNT] = {
    DC_RED, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW,  DC_YLW,  DC_YLW,
    DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU,  DC_YLW,  DC_YLW,
    DC_YLW, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_RED,           DC_YLW, 
    DC_YLW, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU,         DC_BLU,           DC_YLW,  
    DC_YLW,         DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU,          DC_YLW,  DC_YLW,          
    DC_YLW, DC_YLW, DC_YLW, DC_YLW,                 DC_BLU,                 DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW,  DC_YLW,  DC_YLW,       
};

// Default mixed RGB region
uint8_t default_region[RGB_MATRIX_LED_COUNT] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  0,  0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  0,  0,
    0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,      0,
    0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0,    0,      0,
    0,    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,     0,  0,
    0, 0, 0, 0,       0,       0, 0, 0, 0, 0,  0,  0,
};
#endif 
