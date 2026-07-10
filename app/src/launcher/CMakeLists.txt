
target_sources_ifdef(CONFIG_ZMK_LAUNCHER app PRIVATE launcher.c)
target_sources_ifdef(CONFIG_ZMK_LAUNCHER app PRIVATE dynamic_keymap.c)
target_sources_ifdef(CONFIG_ZMK_LAUNCHER app PRIVATE map_keycode.c)
target_sources_ifdef(CONFIG_ZMK_LAUNCHER app PRIVATE send_string.c)
target_sources_ifdef(CONFIG_ZMK_LAUNCHER app PRIVATE mousekey.c)