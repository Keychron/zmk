#pragma once
#include "rgb_matrix_types.h"

#define __ NO_LED

led_config_t g_led_config = {
    {
        // Key Matrix to LED Index
        { 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, __, 13, 14, 15 },
        { 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32 },
        { 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49 },
        { 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, __, __, __, __ },
        { 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, __, __, 76, __ },
        { 77, 78, 79, __, __, __, 80, __, __, __, 81, 82, 83, 84, 85, 86, 87 }
    },
    {
        // LED Index to Physical Position        
        {0,0},  {18,0},  {33,0},  {48,0},  {62,0},  {81,0},  {95,0},  {110,0},  {125,0},  {143,0},  {158,0},  {173,0},  {187,0},           {224,0},   {238,0},   {252,0},
        {0,15}, {15,15}, {29,15}, {44,15}, {59,15}, {73,15}, {88,15}, {103,15}, {118,15}, {132,15}, {147,15}, {162,15}, {176,15}, {198,15},{224,15},  {238,15},  {252,15},
        {4,26}, {22,26}, {37,26}, {51,26}, {66,26}, {81,26}, {95,26}, {110,26}, {125,26}, {140,26}, {154,26}, {169,26}, {184,26}, {198,26},{224,26},  {238,26},  {252,26},
        {6,38}, {23,38}, {38,38}, {55,38}, {70,38}, {84,38}, {99,38}, {114,38}, {129,38}, {143,38}, {158,38}, {173,38}, {188,38},           
        {2,49}, {20,49}, {33,49}, {48,49}, {62,49}, {77,49}, {92,49}, {106,49}, {121,49}, {136,49}, {151,49}, {165,49}, {194,49},                     {238,49},
        {2,61}, {20,61}, {39,61},                            {94,61},                               {151,61}, {166,61}, {187,61}, {200,61},{224,61},  {238,61},  {252,61}
    },
    {
        // RGB LED Index to Flag
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,    1, 1, 1,
        1, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 1, 1, 1, 1,
        1, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 1, 1, 1, 1,
        8, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,   
        1, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 1,       1,
        1, 1, 1,          4,          1, 1, 1, 1, 1, 1, 1
    }
};

#ifdef CONFIG_KEYCHRON_RGB_ENABLE
// Default Color of Per Key RGB
#define DC_RED {  0, 255, 255}
#define DC_BLU {170, 255, 255}
#define DC_YLW {43, 255, 255}
#define DC_NUL {0,0,0}


HSV default_per_key_led[RGB_MATRIX_LED_COUNT] = {
    DC_RED, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW,         DC_YLW, DC_YLW, DC_YLW, 
    DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_YLW, DC_YLW, DC_YLW, DC_YLW,
    DC_YLW, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_RED, DC_YLW, DC_YLW, DC_YLW,
    DC_YLW, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU,                
    DC_YLW, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_YLW,                 DC_YLW,          
    DC_YLW, DC_YLW, DC_YLW,                         DC_BLU,                         DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW,      
};

// Default mixed RGB region
uint8_t default_region[RGB_MATRIX_LED_COUNT] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,    0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,       
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,       0,
    0, 0, 0,          0,          0, 0, 0, 0, 0, 0, 0,
};
#endif 
