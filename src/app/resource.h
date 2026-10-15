// Resource ids of the Win32 front end: the original's own (PITFALL.EXE's English menu and
// dialogs, FUN_0043ee50's tables for language 0).
#pragma once

// menu 214 (DAT_00451b10): its items (FUN_0043b920)
#define IDR_MENU            214
#define IDM_CEIBA           0x9c41
#define IDM_XIBALBA         0x9c42
#define IDM_TAZAMUL         0x9c43
#define IDM_LOSTCITY        0x9c44
#define IDM_COPANTEMPLE     0x9c46
#define IDM_LAKAMUL         0x9c49
#define IDM_YAXCHILAN       0x9c4a
#define IDM_HELP_CONTENTS   0x9c4d
#define IDM_HELP_SEARCH     0x9c4e
#define IDM_HELP_HOWTO      0x9c4f
#define IDM_PALENQUE        0x9c50
#define IDM_TOMB            0x9c51
#define IDM_BALANKANCHE     0x9c52
#define IDM_JAINA           0x9c53
#define IDM_TIKALRUINS      0x9c54
#define IDM_TIKALTEMPLE     0x9c55
#define IDM_WARRIOR         0x9c57
#define IDM_ABOUT           0x9c59
#define IDM_EXIT            0x9c5a
#define IDM_VAULT1          0x9c5f
#define IDM_VAULT2          0x9c60
#define IDM_VAULT3          0x9c61
#define IDM_ATARI           0x9c62
#define IDM_NEWGAME         0x9c64
#define IDM_PROPERTIES      0x9c66

// dialogs
#define IDD_QUIT            124     // DAT_00452380: Esc, "quit this adventure?"
#define IDD_DOOM            127     // "iddqd"
#define IDD_LOADING         201     // DAT_00451a80
#define IDD_DIFFICULTY      203     // DAT_00451b60
#define IDD_SYSTEM          204     // DAT_00452764
#define IDD_SOUND           205     // DAT_004522f0
#define IDD_ABOUT           212     // DAT_00452590
#define IDD_KEYBOARD        213     // DAT_00452760
#define IDD_JOYSTICK        214     // DAT_00451a84
#define IDD_TESTERS         256     // "paidtoplaygames"

// controls
#define IDC_PICTURE         0x404   // the cheats' and the About's picture frame
#define IDC_DIFF_NORMAL     0x3ed
#define IDC_DIFF_HARD       0x3ee
#define IDC_SCREENSIZE      0x3ee
#define IDC_PRIORITY        0x3f6
#define IDC_SYNC            0x3f7
#define IDC_RAMP_PRIORITY   0x400
#define IDC_SOUNDFX         0x3f0
#define IDC_CDMUSIC         0x3f2
#define IDC_CDVOLUME        0x3f4
#define IDC_WAVVOLUME       0x3f5
#define IDC_CDAMBIENT       0x3ff
#define IDC_RAMP_CD         0x401
#define IDC_RAMP_WAV        0x402
#define IDC_KEY_UP          0x409
#define IDC_KEY_DOWN        0x40a
#define IDC_KEY_LEFT        0x40b
#define IDC_KEY_RIGHT       0x40c
#define IDC_KEY_SHOOT       0x40d
#define IDC_KEY_JUMP        0x40e
#define IDC_KEY_WHIP        0x40f
#define IDC_KEY_SELECT      0x411
#define IDC_ENABLE_KEYBOARD 0x3ef
#define IDC_JOY_JUMP        0x3e8
#define IDC_JOY_SHOOT       0x3e9
#define IDC_JOY_WHIP        0x3ea
#define IDC_JOY_SELECT      0x3eb
#define IDC_ENABLE_JOYSTICK 0x3f8
// The pages' pictures (icons 207 .. 210 of PITFALL.EXE): -1 in the original's templates, given
// ids here so the icons can be set from PITFALL.EXE.
#define IDC_ICON_SYSTEM     2070
#define IDC_ICON_SOUND      2080
#define IDC_ICON_JOYSTICK   2090
#define IDC_ICON_KEYBOARD   2100
