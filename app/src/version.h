#ifndef __VERSION_H__
#define __VERSION_H__

#if (CONFIG_SHIELD_KEYCHRON_B1_US || CONFIG_SHIELD_KEYCHRON_B1_UK)
#define VERSION_MAJOR   1
#define VERSION_MINOR   0
#define PATCHLEVEL  	4
#elif CONFIG_SHIELD_KEYCHRON_B1_JIS
#define VERSION_MAJOR   1
#define VERSION_MINOR   0
#define PATCHLEVEL  	5
#elif CONFIG_SHIELD_KEYCHRON_B1_JIS_N
#define VERSION_MAJOR   1
#define VERSION_MINOR   0
#define PATCHLEVEL  	2
#elif CONFIG_SHIELD_KEYCHRON_B1_US_N
#define VERSION_MAJOR   1
#define VERSION_MINOR   0
#define PATCHLEVEL  	2
#elif CONFIG_SHIELD_KEYCHRON_B1_UK_N
#define VERSION_MAJOR   1
#define VERSION_MINOR   0
#define PATCHLEVEL  	1
#elif CONFIG_SHIELD_KEYCHRON_B2_US_N
#define VERSION_MAJOR   1
#define VERSION_MINOR   0
#define PATCHLEVEL  	1
#elif CONFIG_SHIELD_KEYCHRON_B2_UK_N
#define VERSION_MAJOR   1
#define VERSION_MINOR   0
#define PATCHLEVEL  	1
#elif CONFIG_SHIELD_KEYCHRON_B2_JIS_N
#define VERSION_MAJOR   1
#define VERSION_MINOR   0
#define PATCHLEVEL  	1
#elif CONFIG_SHIELD_KEYCHRON_B2_KR_N
#define VERSION_MAJOR   1
#define VERSION_MINOR   0
#define PATCHLEVEL  	1
#elif CONFIG_SHIELD_KEYCHRON_B2
#define VERSION_MAJOR   1
#define VERSION_MINOR   0
#define PATCHLEVEL  	0
#elif (CONFIG_SHIELD_KEYCHRON_B6_US||CONFIG_SHIELD_KEYCHRON_B6_UK)
#define VERSION_MAJOR   1
#define VERSION_MINOR   3
#define PATCHLEVEL  	3
#elif CONFIG_SHIELD_KEYCHRON_B6_JIS
#define VERSION_MAJOR   1
#define VERSION_MINOR   3
#define PATCHLEVEL  	4
#elif CONFIG_SHIELD_KEYCHRON_B6_US_N
#define VERSION_MAJOR   1
#define VERSION_MINOR   0
#define PATCHLEVEL  	1
#elif CONFIG_SHIELD_KEYCHRON_B6_JIS_N
#define VERSION_MAJOR   1
#define VERSION_MINOR   0
#define PATCHLEVEL  	2
#elif CONFIG_SHIELD_KEYCHRON_B6_UK_N
#define VERSION_MAJOR   1
#define VERSION_MINOR   0
#define PATCHLEVEL  	1
#else  
#define VERSION_MAJOR   1
#define VERSION_MINOR   0
#define PATCHLEVEL  	0
#endif 
#define VERSION_TWEAK   0
#define EXTRAVERSION   "ZK"

#define ZMK_VERSION  (VERSION_MAJOR<<16| VERSION_MINOR<<8 | PATCHLEVEL)//0x00010209
// #define VIA_FIRMWARE_VERSION ZMK_VERSION

#define USB_DEC_TO_BCD(dec)	((((dec) / 10) << 4) | ((dec) % 10))

/** USB Device release number (bcdDevice Descriptor field) */
#define USB_BCD_VER		(USB_DEC_TO_BCD(VERSION_MAJOR) << 8 | \
				 USB_DEC_TO_BCD((VERSION_MINOR*10)+PATCHLEVEL))
#define VER_STR1(a1,a2,a3,a4) "v"#a1"."#a2"."#a3"-"a4
#define VER_STR(a1,a2,a3,a4) VER_STR1(a1,a2,a3,a4)

#define APP_VERSION_STRING VER_STR(VERSION_MAJOR,VERSION_MINOR,PATCHLEVEL,EXTRAVERSION)

//upate version.h 's timestamp in ps shell
//(Get-Item ".\src\version.h").LastWriteTime=$(Get-Date -format o)
//read version.h 's timestamp
 //Get-Item .\src\version.h | Format-List CreationTime, LastAccessTime, LastWriteTime
#endif 