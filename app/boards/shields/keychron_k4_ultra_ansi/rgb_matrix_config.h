#pragma once
#include "rgb_matrix_types.h"

#define __ NO_LED

led_config_t g_led_config = {
    {
        // Key Matrix to LED Index
        { 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 84, 85, 86, },
        { 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, __, 29, 30, 87, 88, 89, },
        { 31, __, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 90, 91, __, },
        { 46, __, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, __, 59, 92, 93, 94, },
        { 60, __, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, __, 72, 73, 95, 96, __, },
        { 74, 75, 76, __, __, __, 77, __, __, 78, 79, 80, 81, 82, 83, __, 97, 98, 99  }
    },
    {
        // LED Index to Physical Position
         //esc   //f1    //f2      //f3     //f4     //f5     //f6    //f7       //f8      //f9      //f10     //f11     //f12     //del    //home         //end    //pgup    //pgdn   //light
        {0,0},  {12,0},  {24,0},  {36,0},  {48,0},  {60,0},  {72,0},  {84,0},   {96,0},  {108,0},  {120,0},  {132,0},  {144,0},   {156,0},  {168,0},      {186,0},  
        {0,15}, {12,15}, {24,15}, {36,15}, {48,15}, {60,15}, {72,15}, {84,15},  {96,15},  {108,15}, {120,15}, {132,15}, {144,15},           {162,15},     {186,15}, 
        {4,26},          {18,26}, {30,26}, {42,26}, {54,26}, {66,26}, {78,26},  {90,26},  {102,26}, {114,26}, {126,26}, {138,26}, {150,26}, {162,26},     {186,26},  
        {6,38},          {20,38}, {32,38}, {44,38}, {56,38}, {68,38}, {80,38}, {92,38}, {104,38}, {116,38}, {128,38}, {140,38},             {162,38},     {186,38}, 
        {7,49},          {26,49}, {38,49}, {50,49}, {62,49}, {74,49}, {86,49}, {98,49}, {110,49}, {122,49}, {134,49},   {148,49}, {166,52},               {186,49}, 
        {2,61}, {16,61}, {32,61},                            {74,61},                     {118,61}, {130,61}, {142,61}, {158,61}, {168,61}, {180,61},               
        {198,0},{210,0}, {222,0},{198,15}, {210,15},{222,15},{198,26},{210,26},{198,38}, {210,38},{222,32}, {198,49}, {210,49},  {198,61}, {210,61}, {222,55},

    },
    {
        // RGB LED Index to Flag
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,    1, 1, 1, 1, 1,
        1,    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 1, 1, 1, 
        8,    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 1,    1, 1, 1, 1,
        1,    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 1,    1, 1, 1, 1, 
        1, 1, 1,          4,       1, 1, 1, 1, 1, 1,    1, 1, 1
    }
};
#ifdef CONFIG_KEYCHRON_RGB_ENABLE
// Default Color of Per Key RGB
#define DC_RED {  0, 255, 255}
#define DC_BLU {170, 255, 255}
#define DC_YLW {43, 255, 255}

HSV default_per_key_led[RGB_MATRIX_LED_COUNT] = {
    DC_RED, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW,   DC_YLW,   DC_YLW, 
    DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU,           DC_YLW,   DC_YLW,
    DC_YLW,         DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU,   DC_YLW,   DC_YLW,  
    DC_YLW,         DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_RED,             DC_YLW, 
    DC_YLW,         DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_BLU, DC_YLW,           DC_YLW,   DC_YLW,       
    DC_YLW, DC_YLW, DC_YLW,                         DC_BLU,                 DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW,   DC_YLW,           
    DC_YLW, DC_YLW, DC_YLW,  DC_YLW, DC_YLW, DC_YLW,DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW, DC_YLW,   DC_YLW,   DC_RED,
};

// Default mixed RGB region
uint8_t default_region[RGB_MATRIX_LED_COUNT] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,    0, 0, 
    0,    0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0,    1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,    0, 
    0,    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,    0, 0, 
    0, 0, 0,          0,       0, 0, 0, 0, 0, 0,    
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 

};
#endif 

