/*****************************************************************************
 Name    : RetroPlatform.c
 Project : VICE
 Support : http://www.retroplatform.com
 Legal   : Copyright 2008-2019 Cloanto Corporation - All rights reserved. This
         : file is multi-licensed under the terms of the Mozilla Public License
         : version 2.0 as published by Mozilla Corporation and the GNU General
         : Public License, version 2 or later, as published by the Free
         : Software Foundation.
 Created : 2008-11-28 16:05:30
 Updated : 2019-02-28 15:44:00
 Comment : 
 *****************************************************************************/

#ifndef __CLOANTO_RETROPLATFORM_H__
#define __CLOANTO_RETROPLATFORM_H__

#ifdef WIN32
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#else
typedef int bool;
#endif

extern bool retroplatform_guest_mode, retroplatform_started_up, retroplatform_mouse, retroplatform_send_mouse_events;
extern void *retroplatform_active_window;

extern HRESULT retroplatform_initialize(const char *pszInfo);
extern void set_retroplatform_enabled_tapes(const char *pszEnabledTapes);
extern void set_retroplatform_screen_mode(const char *pszScreenMode);
extern void set_retroplatform_scale2x(const char *pszScale2X);
extern void set_retroplatform_dpiawareness(const char *pszAwareness);
extern void enable_retroplatform_virtualmouse(const char *pszEnable);
extern void set_retroplatform_window(void *window, int index);
extern void retroplatform_uninitialize(void);
extern unsigned long retroplatform_parent(void);
extern void retroplatform_startup(void);
extern void retroplatform_enable_drive_status(unsigned int enable);
extern void retroplatform_display_drive_led(int drivenum, int status, int write_mode);
extern void retroplatform_blink_tape_led(int time, int write_mode);
extern void retroplatform_enumerate_input_devices(void);
extern void retroplatform_report_turbo_cpu(void);

extern void retroplatform_wm_activateapp(WPARAM wParam, LPARAM lParam);
extern void retroplatform_wm_activate(WPARAM wParam, LPARAM lParam);
extern void retroplatform_wm_enable(WPARAM wParam);
extern bool retroplatform_wm_nccalcsize(void *window, WPARAM wParam, LPARAM lParam, LRESULT *plResult);
extern bool retroplatform_wm_ncpaint(void *window, WPARAM wParam, LPARAM lParam);
extern void retroplatform_wm_close(void);
extern void retroplatform_capture_mouse(void);
extern void retroplatform_lbuttondown(WPARAM wParam, LPARAM lParam);
extern void retroplatform_mouse_event(void *window, UINT uMessage, WPARAM wParam, LPARAM lParam);
extern void retroplatform_monitor_active(unsigned int active);

#define RP_CURSORPOS_SYSTEM_NONE	0
#define RP_CURSORPOS_SYSTEM_C64		1
#define RP_CURSORPOS_SYSTEM_C128	2
#define RP_CURSORPOS_SYSTEM_VIC20	3
#define RP_CURSORPOS_SYSTEM_PET2001	4
#define RP_CURSORPOS_SYSTEM_PET		5
#define RP_CURSORPOS_SYSTEM_CBM5x0	6
#define RP_CURSORPOS_SYSTEM_CBM6x0	7
#define RP_CURSORPOS_SYSTEM_PLUS4	8

typedef BYTE (CALLBACK *PFN_MEM_BANK_READ)(int bank, WORD addr, LPARAM lParam);
typedef void (CALLBACK *PFN_MEM_BANK_WRITE)(int bank, WORD addr, BYTE byte, LPARAM lParam);

#ifdef __cplusplus
}
#endif

#endif

#endif // __CLOANTO_RETROPLATFORM_H__
