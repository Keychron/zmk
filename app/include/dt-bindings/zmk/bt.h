/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define BT_CLR_CMD 0
#define BT_NXT_CMD 1
#define BT_PRV_CMD 2
#define BT_SEL_CMD 3
<<<<<<< HEAD
#define BT_PAIR_CMD 4
// #define BT_FULL_RESET_CMD   4
=======
#define BT_CLR_ALL_CMD 4
#define BT_DISC_CMD 5
>>>>>>> ead91ca094c6cd778574f73b9ae011ddab611c66

/*
Note: Some future commands will include additional parameters, so we
defines these aliases up front.
*/

#define BT_CLR BT_CLR_CMD 0
#define BT_NXT BT_NXT_CMD 0
#define BT_PRV BT_PRV_CMD 0
#define BT_SEL BT_SEL_CMD
<<<<<<< HEAD
#define BT_PAIR BT_PAIR_CMD
=======
#define BT_CLR_ALL BT_CLR_ALL_CMD 0
#define BT_DISC BT_DISC_CMD
>>>>>>> ead91ca094c6cd778574f73b9ae011ddab611c66
