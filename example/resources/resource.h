//{{NO_DEPENDENCIES}}
// PulpEmbedIPlug resource ids. Used by main.rc / main.rc_mac_dlg /
// main.rc_mac_menu.
//
// Derived from iPlug2's Examples/IPlugEffect template (WDL license; see
// NOTICE.md). The audio/MIDI preference ids (IDC_COMBO_AUDIO_*, IDC_COMBO_MIDI_*)
// and the ID_LIVE_EDIT / ID_SHOW_* / ID_SCREENSHOT / ID_HELP ids are referenced
// by iPlug2's standalone APP framework (IPlugAPP_main.cpp / IPlugAPP_dialog.cpp),
// so they are kept even though PulpEmbedIPlug's trimmed menu no longer exposes
// the IGraphics-only debug commands (this example builds UI NONE).

#define IDR_ACCELERATOR1                40000
#define IDD_DIALOG_MAIN                 40001
#define IDD_DIALOG_PREF                 40002
#define IDI_ICON1                       40003
#define IDR_MENU1                       40004
#define ID_ABOUT                        40005
#define ID_PREFERENCES                  40006
#define ID_QUIT                         40007
#define ID_HELP                         40008
#define IDC_COMBO_AUDIO_DRIVER          40009
#define IDC_COMBO_AUDIO_IN_DEV          40010
#define IDC_COMBO_AUDIO_OUT_DEV         40011
#define IDC_COMBO_AUDIO_BUF_SIZE        40012
#define IDC_COMBO_AUDIO_SR              40013
#define IDC_COMBO_AUDIO_IN_L            40014
#define IDC_COMBO_AUDIO_IN_R            40015
#define IDC_COMBO_AUDIO_OUT_R           40016
#define IDC_COMBO_AUDIO_OUT_L           40017
#define IDC_COMBO_MIDI_IN_DEV           40018
#define IDC_COMBO_MIDI_OUT_DEV          40019
#define IDC_COMBO_MIDI_IN_CHAN          40020
#define IDC_COMBO_MIDI_OUT_CHAN         40021
#define IDC_BUTTON_OS_DEV_SETTINGS      40022
#define IDC_CB_MONO_INPUT               40023
#define IDAPPLY                         40024
#define ID_LIVE_EDIT                    40025
#define ID_SHOW_DRAWN                   40026
#define ID_SHOW_FPS                     40027
#define ID_SHOW_BOUNDS                  40028
#define ID_SCREENSHOT                   40029

// Next default values for new objects
//
#ifdef APSTUDIO_INVOKED
#ifndef APSTUDIO_READONLY_SYMBOLS
#define _APS_NEXT_RESOURCE_VALUE        105
#define _APS_NEXT_COMMAND_VALUE         40001
#define _APS_NEXT_CONTROL_VALUE         1011
#define _APS_NEXT_SYMED_VALUE           101
#endif
#endif
