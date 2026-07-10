# Copyright (c) 2020 The ZMK Contributors
# SPDX-License-Identifier: MIT

add_subdirectory_ifdef(CONFIG_GPIO gpio)
add_subdirectory_ifdef(CONFIG_KSCAN kscan)
add_subdirectory_ifdef(CONFIG_SENSOR sensor)
add_subdirectory_ifdef(CONFIG_DISPLAY display)
<<<<<<< HEAD
add_subdirectory_ifdef(CONFIG_ZMK_RGB_UNDERGLOW led_strip)
=======
add_subdirectory_ifdef(CONFIG_INPUT input)
>>>>>>> ead91ca094c6cd778574f73b9ae011ddab611c66
