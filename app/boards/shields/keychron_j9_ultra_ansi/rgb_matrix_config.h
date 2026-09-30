#pragma once
#include "rgb_matrix_types.h"

#define __ NO_LED

led_config_t g_led_config = {
    {
        // Key Matrix to LED Index
        { 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15 },
        { 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, __, 30 },   
        { 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, __, 45 },
        { 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, __, 58, __, 59 },
        { 60, __, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, __, 71, 72, 73 },
        { 74, 75, 76, __, __, __, 77, __, __, __, 78, 79, 80, 81, 82, 83 }
    },
    {
        // LED Index to Physical Position
        {0,0},  {15,0},  {30,0},  {45,0},  {60,0},  {75,0},  {90,0},  {105,0},  {120,0},  {135,0},  {150,0},  {165,0},  {180,0},    {195,0},   {210,0},   {225,0},
        {0,15}, {15,15}, {30,15}, {45,15}, {60,15}, {75,15}, {90,15}, {105,15}, {120,15}, {135,15}, {150,15}, {165,15}, {180,15},              {202,15},  {225,15},
        {4,26}, {22,26}, {37,26}, {52,26}, {67,26}, {82,26}, {97,26}, {112,26}, {127,26}, {142,26}, {157,26}, {172,26}, {187,26},              {202,26},  {225,26},
        {6,38}, {24,38}, {39,38}, {54,38}, {69,38}, {84,38}, {99,38}, {114,38}, {129,38}, {144,38}, {159,38}, {174,38},                        {202,38},  {225,38},
        {7,49},          {31,49}, {46,49}, {61,49}, {76,49}, {91,49}, {106,49}, {121,49}, {136,49}, {151,49}, {166,49}, {190,49},              {210,49},  {225,49},
        {2,61}, {19,61}, {36,61},                            {88,61},                               {150,61}, {165,61}, {180,61},  {195,64},   {210,64},  {225,64}
    },
    {
        // RGB LED Index to Flag
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 1,    1,
        1, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,    1,
        8, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,    1,    1,
        1,    4, 4, 4, 4, 4, 4, 4, 4, 4, 4,    1, 1, 1,
        1, 1, 1,          4,          1, 1, 1, 1, 1, 1
    }
};
// Default Color of Per Key RGB
#ifdef CONFIG_KEYCHRON_RGB_ENABLE
#define DC_RED {  0, 255, 255}
#define DC_BLU {170, 255, 255}
#define DC_YLW {43, 255, 255}

HSV default_per_key_led[RGB_MATRIX_LED_COUNT] = {
    DC_RED, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW,  DC_YLW,  DC_YLW,
    DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_YLW,           DC_YLW,
    DC_YLW, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_YLW,           DC_YLW, 
    DC_YLW, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU,         DC_RED,           DC_YLW,  
    DC_YLW,         DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU,         DC_YLW,  DC_YLW,  DC_YLW,          
    DC_YLW, DC_YLW, DC_YLW,                         DC_BLU,                         DC_YLW, DC_YLW, DC_YLW, DC_YLW,  DC_YLW,  DC_YLW,       
};

// Default mixed RGB region
uint8_t default_region[RGB_MATRIX_LED_COUNT] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  0,  0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,      0,
    0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,      0,
    0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0,    0,      0,
    0,    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,    0,  0,  0,
    0, 0, 0,          0,          0, 0, 0, 0,  0,  0,
};
#endif 
