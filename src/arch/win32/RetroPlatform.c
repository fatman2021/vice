/*****************************************************************************
 Name    : RetroPlatform.h
 Project : VICE
 Support : http://www.retroplatform.com
 Legal   : Copyright 2008-2019 Cloanto Corporation - All rights reserved. This
         : file is multi-licensed under the terms of the Mozilla Public License
         : version 2.0 as published by Mozilla Corporation and the GNU General
         : Public License, version 2 or later, as published by the Free
         : Software Foundation.
 Created : 2008-11-28 16:02:50
 Updated : 2019-02-28 15:48:00
 Comment : RetroPlatform-specific code
 *****************************************************************************/

#include "RetroPlatform.h"
#include "RetroPlatformIPC.h"
#include "RetroPlatformGuestIPC.h"
#include "winmain.h"
#include "machine.h"
#include "screenshot.h"
#include "videoarch.h"
#include "video.h"
#include "viewport.h"
#include "resources.h"
#include "fullscrn.h"
#include "drive.h"
#include "kbd.h"
#include "keyboard.h"
#include "tape.h"
#include "attach.h"
#include "joy.h"
#include "joystick.h"
#include "joyport.h"
#include "mouse.h"
#include "alarm.h"
#include "maincpu.h"
#include "vice-event.h"
#include "lib.h"
#include "util.h"
#include "res.h"
#include "ui.h"
#include "uiapi.h"
#include "uidatasette.h"
#include "dinput_handle.h"
#include "userport_joystick.h"
#include "interrupt.h"
#include "machine.h"
#include "monitor.h"
#include <windowsx.h>
#include <commctrl.h>

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef _countof
#define _countof(a) (sizeof(a) / sizeof(a[0]))
#endif

// special row value
#define RW_EXTRA_FUNC		0xFF

// column values when row is == RW_EXTRA_FUNC
#define CL_FUNC_RESTORE		0
#define CL_FUNC_COLUMN4080	1
#define CL_FUNC_CAPS		2

#define ALLOW_SHIFT  8

struct rp_cmdname_s {
    LPCSTR name;
    int command;
};
typedef struct rp_cmdname_s rp_cmdname_t;

enum
{
	CN_KEY_RAW_DOWN,
	CN_KEY_RAW_UP,
    CN_DISPLAY_SWITCH,
	CN_CART_FREEZE,
    CN_TAPE_STOP,
    CN_TAPE_PLAY,
    CN_TAPE_FFORWARD,
    CN_TAPE_REWIND,
    CN_TAPE_RECORD,
    CN_TAPE_RESET,
    CN_TAPE_RESET_COUNTER,
};
static const rp_cmdname_t cmdnames[] =
{
    { "KEY_RAW_DOWN", CN_KEY_RAW_DOWN },
    { "KEY_RAW_UP", CN_KEY_RAW_UP },
    { "DISPLAY_SWITCH", CN_DISPLAY_SWITCH },
    { "CART_FREEZE", CN_CART_FREEZE },
    { "TAPE_STOP", CN_TAPE_STOP },
    { "TAPE_PLAY", CN_TAPE_PLAY },
    { "TAPE_FAST_FORWARD", CN_TAPE_FFORWARD },
    { "TAPE_REWIND", CN_TAPE_REWIND },
    { "TAPE_RECORD", CN_TAPE_RECORD },
    { "TAPE_RESET", CN_TAPE_RESET },
    { "TAPE_RESET_COUNTER", CN_TAPE_RESET_COUNTER },
};

static char *event_string;
static const char *event_string_ptr;
static CRITICAL_SECTION event_string_critical_section;

static void parse_events(void);
static alarm_t *event_pause_alarm;

STICKYKEYS saved_sticky_keys = {sizeof(STICKYKEYS), 0};
TOGGLEKEYS saved_toggle_keys = {sizeof(TOGGLEKEYS), 0};
FILTERKEYS saved_filter_keys = {sizeof(FILTERKEYS), 0};    
BOOL got_sticky_keys;
BOOL got_toggle_keys;
BOOL got_filter_keys;

static HMODULE user32;
typedef BOOL (WINAPI *PFN_EnumDisplayMonitors)(HDC hdc, LPCRECT lprcClip, MONITORENUMPROC lpfnEnum, LPARAM dwData);
static PFN_EnumDisplayMonitors pfnEnumDisplayMonitors;

static const char platform_dll_path[] = "..\\Player\\Platforms\\CBMPlatform.dll";
static HMODULE platform_dll;
typedef HRESULT (APIENTRY *PFN_RPSetCBMCursorPosition)(DWORD dwSystem, LONG lCursorX, LONG lCursorY, PFN_MEM_BANK_READ pfnBankRead, PFN_MEM_BANK_WRITE pfnBankWrite, LPARAM lParam);
static PFN_RPSetCBMCursorPosition pfnRPSetCBMCursorPosition;
static BOOL virtualmouse_enabled;
static DWORD window_clickactive_time[2];
static position_t cursor_pos;
static DWORD cursor_pos_system;

extern HWND window_handles[2];
extern int number_of_windows;

#define KEYLAYOUT_COUNT 4 // layout 1, 2, 3, custom
#define KEYSET_COUNT    5 // north, east, south, west, fire

bool retroplatform_guest_mode = FALSE;
bool retroplatform_started_up = FALSE;
bool retroplatform_mouse = FALSE; // mouse inserted in port 1 or 2
bool retroplatform_send_mouse_events = FALSE;
void *retroplatform_active_window; // currently active window (C128 windows swapping)
static DWORD retroplatform_mouse_buttons_state = 0;
static BOOL mouseleave_requested = FALSE;

// interprocess communication info
static RPGUESTINFO retroplatform_guest_info;

// startup settings
static DWORD retroplatform_screen_mode;
static DWORD retroplatform_screen_modes[2];
static bool retroplatform_screen_initialized[2];
static int retroplatform_screen_index;
static RECT fullwindow_padding;

static bool changing_screen_mode = FALSE;
static bool fullwindow_screen_mode = FALSE;
static int enabled_drives;
static int enabled_tapes;
static bool turbo_cpu = FALSE;
static bool scale_2x = FALSE;

static BOOL scheduled_refresh = FALSE;
#define SCHEDULED_REFRESH_TIMER_ID  2500

static DWORD input_port_device[2] = { RP_INPUTDEVICE_EMPTY, RP_INPUTDEVICE_EMPTY };
static DWORD multitap_port_device[2] = { RP_INPUTDEVICE_EMPTY, RP_INPUTDEVICE_EMPTY };
static const char mouse_id[] = "Mouse1";

extern BYTE _kbd_extended_key_tab[];

struct MonitorInfo
{
	int nCount;
	int nScreen;
    RECT rcMonitor;
};


static const char *get_double_size_resource_name(void)
{
    switch (machine_class)
    {
		case VICE_MACHINE_C64:
		case VICE_MACHINE_C128:
		case VICE_MACHINE_C64DTV:
		case VICE_MACHINE_C64SC:
        case VICE_MACHINE_CBM5x0:
			return "VICIIDoubleSize";
		case VICE_MACHINE_VIC20:
			return "VICDoubleSize";
        case VICE_MACHINE_PET:
        case VICE_MACHINE_CBM6x0:
			return "CrtcDoubleSize";
        case VICE_MACHINE_PLUS4:
			return "TEDDoubleSize";
	}
	return NULL;
}

static const char *get_double_scan_resource_name(void)
{
    switch (machine_class)
    {
		case VICE_MACHINE_C64:
		case VICE_MACHINE_C128:
		case VICE_MACHINE_C64DTV:
		case VICE_MACHINE_C64SC:
        case VICE_MACHINE_CBM5x0:
			return "VICIIDoubleScan";
		case VICE_MACHINE_VIC20:
			return "VICDoubleScan";
        case VICE_MACHINE_PET:
        case VICE_MACHINE_CBM6x0:
			return "CrtcDoubleScan";
        case VICE_MACHINE_PLUS4:
			return "TEDDoubleScan";
	}
	return NULL;
}

static int set_double_size(int double_size)
{
	const char *name;
	name = get_double_size_resource_name();
	return name ? resources_set_int(name, double_size) : -1;
}

static int set_double_scan(int double_scan)
{
	const char *name;
	name = get_double_scan_resource_name();
	return name ? resources_set_int(name, double_scan) : -1;
}

static bool needs_1x_double_scan(void)
{
    int val;
    switch (machine_class)
    {
        case VICE_MACHINE_PET:
            if (resources_get_int("VideoSize", &val) < 0)
                val = 40;
			return (val != 40); // CBM 8032
        case VICE_MACHINE_CBM6x0:
			return TRUE;
	}
	return FALSE;
}

#ifdef PATCH_CBM7X0_RATIO
static bool is_cbm7x0(void)
{
    int val;
    if (machine_class == VICE_MACHINE_CBM6x0)
    {
		if (resources_get_int("ModelLine", &val) >= 0 && val == 0)
			return TRUE;
	}
	return FALSE;
}
#endif

void retroplatform_capture_mouse(void)
{
    resources_set_int("Mouse", 1);
    RPSendMessage(RP_IPC_TO_HOST_MOUSECAPTURE, RP_MOUSECAPTURE_CAPTURED, 0, NULL, 0, &retroplatform_guest_info, NULL);
}

static void retroplatform_release_mouse(void)
{
    resources_set_int("Mouse", 0);
    RPSendMessage(RP_IPC_TO_HOST_MOUSECAPTURE, 0, 0, NULL, 0, &retroplatform_guest_info, NULL);
}

static void get_1x_size(const video_canvas_t *c, int *w, int *h)
{
	int cw, ch;

    cw = c->draw_buffer->visible_width;
    ch = c->draw_buffer->visible_height;

#ifdef PATCH_CBM7X0_RATIO
	if (is_cbm7x0())
		ch = (cw * 3) / 4; // 4:3 monitor ratio
	else 
#endif
	if (c->videoconfig->scaley == c->videoconfig->scalex * 2) // machine with corrected aspect ratio (e.g. CBM 8032, CBM 6x0)
		ch *= 2;

	*w = cw;
	*h = ch;
}

static BOOL CALLBACK enum_display_proc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData)
{
    struct MonitorInfo *pmi = (struct MonitorInfo *)dwData;
    if (pmi)
    {
		if (pmi->nCount == pmi->nScreen)
		{
	        MONITORINFO mi;
	        mi.cbSize = sizeof(mi);
	        if (GetMonitorInfo(hMonitor, &mi))
                pmi->rcMonitor = mi.rcMonitor;
			return FALSE;
        }
		pmi->nCount += 1;
    }
    return TRUE;
}

static bool get_display_rect(unsigned long display, RECT *prc)
{
    struct MonitorInfo mi;
    memset(&mi, 0, sizeof(mi));
	mi.nCount = 1;
	mi.nScreen = display;

	if (pfnEnumDisplayMonitors)
		pfnEnumDisplayMonitors(NULL, NULL, enum_display_proc, (LPARAM)&mi);

	if (mi.rcMonitor.left != 0 ||
		mi.rcMonitor.top != 0 ||
		mi.rcMonitor.right != 0 ||
		mi.rcMonitor.bottom != 0)
	{
        *prc = mi.rcMonitor;
		return TRUE;
	}
	return FALSE;
}

static bool set_fullscreen(unsigned long fullscreen, unsigned long use_tvm)
{
    if (fullscreen)
    {
        if (!ui_get_full_screen())
        {
			RECT rc;
			int cw, ch;
            use_tv_modes tvm;
            video_canvas_t *c = video_canvas_for_hwnd(retroplatform_active_window);
            switch (use_tvm)
            {
                case RP_SCREENMODE_USETVM_ALWAYS: tvm = utvm_always; break;
                case RP_SCREENMODE_USETVM_AUTO:   tvm = utvm_auto;   break;
                default:                          tvm = utvm_never;  break;
            }
			if (c)
				get_1x_size(c, &cw, &ch);
			else
			{
				cw = 640;
				ch = 480;
			}
			if (!get_display_rect(fullscreen, &rc))
				return FALSE;
            if (!select_best_fullscreen_mode(&rc, cw, ch, tvm))
				return FALSE;
            ui_toggle_full_screen_win(retroplatform_active_window);
            if (!ui_get_full_screen())
                return FALSE;
            if (retroplatform_mouse)
				retroplatform_capture_mouse();
        }
    }
    else
    {
        if (ui_get_full_screen())
        {
            ui_toggle_full_screen_win(retroplatform_active_window);
            if (ui_get_full_screen())
                return FALSE;
        }
    }
    return TRUE;
}

static bool set_screen_nx(unsigned long nx)
{
    switch (nx)
    {
        case RP_SCREENMODE_SCALE_1X:
			set_double_size(0);
			set_double_scan(needs_1x_double_scan() ? 1 : 0);
            break;

        case RP_SCREENMODE_SCALE_2X:
        case RP_SCREENMODE_SCALE_3X:
        case RP_SCREENMODE_SCALE_4X:
        case RP_SCREENMODE_SCALE_MAX:
            if (set_double_size(1) < 0)
            {
                if (nx != RP_SCREENMODE_SCALE_MAX)
                    return FALSE; // double size mode not available
            }
            set_double_scan(1);
            break;
        default:
            return FALSE;
    }
    return TRUE;
}

static bool get_correct_window_size(HWND window, DWORD dwScreenMode, int *pw, int *ph)
{
    video_canvas_t *c;
	int cw, ch;

	c = video_canvas_for_hwnd(window);
	if (!c)
		return FALSE;

	get_1x_size(c, &cw, &ch);

	switch (RP_SCREENMODE_SCALE(dwScreenMode))
	{
		case RP_SCREENMODE_SCALE_2X:
			cw *= 2;
			ch *= 2;
			break;
		case RP_SCREENMODE_SCALE_3X:
			cw *= 3;
			ch *= 3;
			break;
		case RP_SCREENMODE_SCALE_4X:
			cw *= 4;
			ch *= 4;
			break;
	}
	*pw = cw;
	*ph = ch;
	return TRUE;
}

static bool set_fullwindow(HWND window, unsigned long fullwindow, DWORD dwScreenMode)
{
    RECT rcDisplay, rcPrevPadding, rc;
    struct MonitorInfo mi;
    int i, mx, my, p1, p2;
    int cw = 0;
    int ch = 0;
    video_canvas_t *c = video_canvas_for_hwnd(window);
    if (!c)
        return FALSE;

    rcPrevPadding = fullwindow_padding;
    memset(&fullwindow_padding, 0, sizeof(fullwindow_padding));

    if (fullwindow)
    {
        memset(&mi, 0, sizeof(mi));
		mi.nCount = 1;
		mi.nScreen = fullwindow;

        if (pfnEnumDisplayMonitors)
            pfnEnumDisplayMonitors(NULL, NULL, enum_display_proc, (LPARAM)&mi);

		if (mi.rcMonitor.left != 0 ||
			mi.rcMonitor.top != 0 ||
			mi.rcMonitor.right != 0 ||
			mi.rcMonitor.bottom != 0)
		{
            rcDisplay = mi.rcMonitor;
        }
        else
        {
            GetWindowRect(GetDesktopWindow(), &rcDisplay);
        }
        rc = rcDisplay;
        if (dwScreenMode & RP_SCREENMODE_SCALING_STRETCH)
        {
            resources_set_int("KeepAspectRatio", 0);
            resources_set_int("TrueAspectRatio", 0);
        }
        else if (dwScreenMode & RP_SCREENMODE_SCALING_SUBPIXEL)
        {
            resources_set_int("KeepAspectRatio", 1);
            resources_set_int("TrueAspectRatio", 1);
            for (i = 0; i < 2; i++)
            {
                rc = rcDisplay;
                SendMessage(window, WM_SIZING, (WPARAM)(i != 0 ? WMSZ_RIGHT : WMSZ_BOTTOM), (LPARAM)&rc);
                if (i != 0 && (rc.right - rc.left) == (rcDisplay.right - rcDisplay.left))
                {
                    p1 = ((rcDisplay.bottom - rcDisplay.top) - (rc.bottom - rc.top)) / 2;
                    p2 = (rcDisplay.bottom - rcDisplay.top) - (rc.bottom - rc.top) - fullwindow_padding.top;
					if (p1 >= 0 && p2 >= 0)
					{
						fullwindow_padding.top = p1;
						fullwindow_padding.bottom = p2;
	                    break;
					}
                }
                else if (i == 0 && (rc.bottom - rc.top) == (rcDisplay.bottom - rcDisplay.top))
                {
                    p1 = ((rcDisplay.right - rcDisplay.left) - (rc.right - rc.left)) / 2;
                    p2 = (rcDisplay.right - rcDisplay.left) - (rc.right - rc.left) - fullwindow_padding.left;
					if (p1 >= 0 && p2 >= 0)
					{
						fullwindow_padding.left = p1;
						fullwindow_padding.right = p2;
	                    break;
					}
                }
            }
        }
        else // integer scaling
        {
            resources_set_int("KeepAspectRatio", 0);
            resources_set_int("TrueAspectRatio", 0);

			get_1x_size(c, &cw, &ch);

			mx = (rcDisplay.right - rcDisplay.left) / cw;
            my = (rcDisplay.bottom - rcDisplay.top) / ch;

            for (i = (mx > my) ? mx : my; i > 0; i--)
            {
                mx = (rcDisplay.right - rcDisplay.left) - (cw * i);
                my = (rcDisplay.bottom - rcDisplay.top) - (ch * i);
                if (mx >= 0 && my >= 0) // this multiplier fit the screen
                {
                    fullwindow_padding.left = mx / 2;
                    fullwindow_padding.right = (rcDisplay.right - rcDisplay.left) - (cw * i) - fullwindow_padding.left;
                    fullwindow_padding.top = my / 2;
                    fullwindow_padding.bottom = (rcDisplay.bottom - rcDisplay.top) - (ch * i) - fullwindow_padding.top;
                    break;
                }
            }
        }
        if (!fullwindow_screen_mode)
            cursor_active -= 1;
        fullwindow_screen_mode = TRUE;

        if (memcmp(&rcPrevPadding, &fullwindow_padding, sizeof(fullwindow_padding)))
            SetWindowPos(window, 0, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOSIZE | SWP_NOMOVE); // first issue a framechange, than resize (fixes switch to 1024x768 fullwindow)
        SetWindowPos(window, 0, rcDisplay.left, rcDisplay.top, rcDisplay.right - rcDisplay.left, rcDisplay.bottom - rcDisplay.top, SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER);

        if (retroplatform_mouse)
			retroplatform_capture_mouse();
	}
    else
    {
        if (fullwindow_screen_mode)
            cursor_active += 1;
        fullwindow_screen_mode = FALSE;

        if (RP_SCREENMODE_DISPLAY(dwScreenMode) == 0) // not fullscreen
        {
            if (memcmp(&rcPrevPadding, &fullwindow_padding, sizeof(fullwindow_padding)))
                SetWindowPos(window, 0, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOSIZE | SWP_NOMOVE);

			if (get_correct_window_size((HWND)window, dwScreenMode, &cw, &ch))
			{
	            GetWindowRect(window, &rc);
	            if ((rc.right - rc.left) != cw || (rc.bottom - rc.top) != ch)
			        SetWindowPos(window, 0, 0, 0, cw, ch, SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOMOVE);
			}
        }
    }
    return TRUE;
}

static void set_video_filter(bool scan_lines)
{
    switch (machine_class)
    {
        case VICE_MACHINE_PET:
        case VICE_MACHINE_CBM6x0:
			resources_set_int("CrtcFilter", scale_2x ? 2 : (scan_lines ? 1 : 0));
            break;
        case VICE_MACHINE_VIC20:
            resources_set_int("VICFilter", scale_2x ? 2 : (scan_lines ? 1 : 0));
            break;
        case VICE_MACHINE_PLUS4:
            resources_set_int("TEDFilter", scale_2x ? 2 : (scan_lines ? 1 : 0));
            break;
        case VICE_MACHINE_C128:
            resources_set_int("VICIIFilter", scale_2x ? 2 : (scan_lines ? 1 : 0));
            resources_set_int("VDCFilter", scale_2x ? 2 : (scan_lines ? 1 : 0));
            break;
        default:
            resources_set_int("VICIIFilter", scale_2x ? 2 : (scan_lines ? 1 : 0));
            break;
    }
}

static HWND change_screenmode(DWORD dwScreenMode)
{
    unsigned long fullscreen = RP_SCREENMODE_DISPLAY(dwScreenMode);
    unsigned long use_tvm = RP_SCREENMODE_USETVM(dwScreenMode);
    unsigned long nx = RP_SCREENMODE_SCALE(dwScreenMode);
    unsigned long fullwindow = 0;

    if (fullscreen &&
        RP_SCREENMODE_FULLSCREEN(dwScreenMode) == RP_SCREENMODE_FULLSCREEN_SHARED &&
        video_dx9_enabled())
    {
        fullwindow = fullscreen;
        fullscreen = 0;
    }
    changing_screen_mode = TRUE;

    if (ui_get_full_screen() && !fullscreen) {  // from fullscreen to windowed mode
        if (!set_fullscreen(fullscreen, use_tvm))
            return NULL;
        if (!set_screen_nx(nx))
            return NULL;
    }
    else {
        if (!set_screen_nx(nx))
            return NULL;
        if (!set_fullscreen(fullscreen, use_tvm))
            return NULL;
    }
	set_video_filter((dwScreenMode & RP_SCREENMODE_SCANLINES) != 0);

	if (!set_fullwindow(retroplatform_active_window, fullwindow, dwScreenMode))
        return NULL;

    retroplatform_screen_mode = dwScreenMode;
    changing_screen_mode = FALSE;

    return retroplatform_active_window;
}

bool retroplatform_wm_nccalcsize(void *window, WPARAM wParam, LPARAM lParam, LRESULT *plResult)
{
    LRESULT lr = 0;
    if ((HWND)window != retroplatform_active_window ||
        (fullwindow_padding.left == 0 &&
         fullwindow_padding.top == 0 &&
         fullwindow_padding.right == 0 &&
         fullwindow_padding.bottom == 0))
    {
        return FALSE;
    }
    if (wParam)
    {
        NCCALCSIZE_PARAMS *p = (NCCALCSIZE_PARAMS *)lParam;
        if (fullwindow_padding.left || 
            fullwindow_padding.right)
        {
            p->rgrc[0].left += fullwindow_padding.left;
            p->rgrc[0].right -= fullwindow_padding.right;
            lr |= WVR_HREDRAW;
        }
        if (fullwindow_padding.top || 
            fullwindow_padding.bottom)
        {
            p->rgrc[0].top += fullwindow_padding.top;
            p->rgrc[0].bottom -= fullwindow_padding.bottom;
            lr |= WVR_VREDRAW;
        }
        if (plResult)
            *plResult = lr;
        return TRUE;
    }
    else
    {
        RECT *p = (RECT *)lParam;
        if (fullwindow_padding.left || 
            fullwindow_padding.right)
        {
            p->left += fullwindow_padding.left;
            p->right -= fullwindow_padding.right;
        }
        if (fullwindow_padding.top || 
            fullwindow_padding.bottom)
        {
            p->top += fullwindow_padding.top;
            p->bottom -= fullwindow_padding.bottom;
        }
        if (plResult)
            *plResult = 0;
        return FALSE;
    }
}

bool retroplatform_wm_ncpaint(void *window, WPARAM wParam, LPARAM lParam)
{
    HDC hdc;
    RECT rcw, rc;
    if ((HWND)window != retroplatform_active_window ||
        (fullwindow_padding.left == 0 &&
         fullwindow_padding.top == 0 &&
         fullwindow_padding.right == 0 &&
         fullwindow_padding.bottom == 0))
    {
        return FALSE;
    }
    GetWindowRect((HWND)window, &rcw);
    rcw.right = rcw.right - rcw.left;
    rcw.left = 0;
    rcw.bottom = rcw.bottom - rcw.top;
    rcw.top = 0;
    hdc = GetWindowDC((HWND)window);

    if (fullwindow_padding.left ||
        fullwindow_padding.right)
    {
        rc.left = 0;
        rc.top = 0;
        rc.right = fullwindow_padding.left;
        rc.bottom = rcw.bottom;
        FillRect(hdc, &rc, GetStockObject(BLACK_BRUSH));
        rc.left = rcw.right - fullwindow_padding.right;
        rc.top = 0;
        rc.right = rcw.right;
        rc.bottom = rcw.bottom;
        FillRect(hdc, &rc, GetStockObject(BLACK_BRUSH));
    }
    if (fullwindow_padding.top || 
        fullwindow_padding.bottom)
    {
        rc.left = 0;
        rc.top = 0;
        rc.right = rcw.right;
        rc.bottom = fullwindow_padding.top;
        FillRect(hdc, &rc, GetStockObject(BLACK_BRUSH));
        rc.left = 0;
        rc.top = rcw.bottom - fullwindow_padding.bottom;
        rc.right = rcw.right;
        rc.bottom = rcw.bottom;
        FillRect(hdc, &rc, GetStockObject(BLACK_BRUSH));
    }
    ReleaseDC((HWND)window, hdc);
    return TRUE;
}

static bool save_bmp(const char *filename, HBITMAP hBitmap)
{
    HDC hdc;
    FILE *fp;
    LPVOID pBuf;
    BITMAPINFO bmpInfo;
    BITMAPFILEHEADER  bmpFileHeader; 
    bool ok = FALSE;

    hdc = GetDC(NULL);
    if (hdc)
    {
        ZeroMemory(&bmpInfo,sizeof(BITMAPINFO));
        bmpInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        GetDIBits(hdc, hBitmap, 0, 0, NULL, &bmpInfo, DIB_RGB_COLORS);
        if (bmpInfo.bmiHeader.biSizeImage <= 0)
            bmpInfo.bmiHeader.biSizeImage = bmpInfo.bmiHeader.biWidth * abs(bmpInfo.bmiHeader.biHeight) * (bmpInfo.bmiHeader.biBitCount+7) / 8;
        pBuf = lib_malloc(bmpInfo.bmiHeader.biSizeImage);
        bmpInfo.bmiHeader.biCompression = BI_RGB;
        GetDIBits(hdc, hBitmap, 0, bmpInfo.bmiHeader.biHeight, pBuf, &bmpInfo, DIB_RGB_COLORS);
        fp = fopen(filename, "wb");
        if (fp)
        {
            bmpFileHeader.bfReserved1 = 0;
            bmpFileHeader.bfReserved2 = 0;
            bmpFileHeader.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + bmpInfo.bmiHeader.biSizeImage;
            bmpFileHeader.bfType = *(WORD*)"BM";
            bmpFileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER); 
            if (fwrite(&bmpFileHeader, sizeof(BITMAPFILEHEADER), 1, fp) &&
                fwrite(&bmpInfo.bmiHeader, sizeof(BITMAPINFOHEADER), 1, fp) &&
                fwrite(pBuf, bmpInfo.bmiHeader.biSizeImage, 1, fp))
                ok = TRUE;
            fclose(fp);
        }
        lib_free(pBuf);
        ReleaseDC(NULL, hdc);
    }
    return ok;
}

static long canvas_save_bmp(video_canvas_t *canvas, const char *pszRawFile, const char *pszFilteredFile)
{
    BITMAPINFO bmi, bmi2;
    PVOID pvb, pvb2;
    HBITMAP hBitmap, hBitmap2, hBitmap0;
    RECT rc;
	HDC hdc;
    int w, h, xs, ys, cw, ch, pitch, val;
	long vmode;
	char *pszBmpName;
	size_t ln;
    bool ok = FALSE;

	if (pszFilteredFile)
	{
		GetWindowRect(canvas->client_hwnd, &rc);
		cw = rc.right - rc.left;
		ch = rc.bottom - rc.top;
		w = MIN(canvas->draw_buffer->canvas_width, canvas->geometry->screen_size.width - canvas->viewport->first_x) * canvas->videoconfig->scalex;
        h = MIN(canvas->draw_buffer->canvas_height, canvas->viewport->last_line - canvas->viewport->first_line + 1) * canvas->videoconfig->scaley;
		bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
		bmi.bmiHeader.biWidth = w;
		bmi.bmiHeader.biHeight = -h; // top-down bitmap
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = canvas->depth;
		bmi.bmiHeader.biCompression = BI_RGB;
		bmi.bmiHeader.biSizeImage = 0;
		bmi.bmiHeader.biXPelsPerMeter = 0;
		bmi.bmiHeader.biYPelsPerMeter = 0;
		bmi.bmiHeader.biClrUsed = 0;
		bmi.bmiHeader.biClrImportant = 0;

		hBitmap = (HBITMAP)CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, &pvb, NULL, 0);
		if (hBitmap)
		{
			xs = canvas->viewport->first_x - canvas->viewport->x_offset + canvas->geometry->extra_offscreen_border_left;
			ys = canvas->viewport->first_line - canvas->viewport->y_offset;
			pitch = w * (canvas->depth / 8);
			video_canvas_render(canvas, pvb, w, h, xs, ys, 0, 0, pitch, canvas->depth);

			bmi2 = bmi;
			bmi2.bmiHeader.biWidth = cw; // this is bigger than w,h, in 3X and 4X screen modes
			bmi2.bmiHeader.biHeight = -ch;
			hBitmap2 = (HBITMAP)CreateDIBSection(NULL, &bmi2, DIB_RGB_COLORS, &pvb2, NULL, 0);
			if (hBitmap2)
			{
				hdc = CreateCompatibleDC(NULL);
				if (hdc)
				{
					hBitmap0 = (HBITMAP)SelectObject(hdc, hBitmap2);
					SetStretchBltMode(hdc, COLORONCOLOR);
					StretchDIBits(hdc, 0, 0, cw, ch, 0, 0, w, h, pvb, &bmi, DIB_RGB_COLORS, SRCCOPY);
					ok = save_bmp(pszFilteredFile, hBitmap2);
					SelectObject(hdc, hBitmap0);
					DeleteDC(hdc);
				}
				DeleteObject(hBitmap2);
			}
			DeleteObject(hBitmap);
		}
		if (!ok)
			return RP_SCREENCAPTURE_ERROR;
	}
	if (pszRawFile)
	{
		DeleteFile(pszRawFile);
		if (screenshot_save("BMP", pszRawFile, canvas) < 0)
			return RP_SCREENCAPTURE_ERROR;
		ln = strlen(pszRawFile);
        pszBmpName = lib_malloc(ln + 5);
		if (pszBmpName)
		{
			strcpy(pszBmpName, pszRawFile);
			strcat(pszBmpName, ".bmp");
			if (!(ln > 4 && stricmp(pszRawFile + ln - 4, ".bmp") == 0) && // we usually save .tmp files, but screenshot_save() appends a .bmp extension to them
				GetFileAttributes(pszRawFile) == (DWORD)-1 &&
				GetFileAttributes(pszBmpName) != (DWORD)-1)
			{
				MoveFile(pszBmpName, pszRawFile);
			}
            lib_free(pszBmpName);
		}
	}

	vmode = RP_GUESTSCREENFLAGS_VERTICAL_NONINTERLACED;
    switch (machine_class)
    {
        case VICE_MACHINE_PET:
            if (resources_get_int("VideoSize", &val) < 0) // VideoSize values: 40 (40 column mode) or 0 (80 column mode)
                val = 40;
			vmode |= (val == 40) ? RP_GUESTSCREENFLAGS_HORIZONTAL_LORES : RP_GUESTSCREENFLAGS_HORIZONTAL_HIRES;
			break;
		case VICE_MACHINE_CBM6x0:
			vmode |= RP_GUESTSCREENFLAGS_HORIZONTAL_HIRES;
			break;
		default:
			vmode |= RP_GUESTSCREENFLAGS_HORIZONTAL_LORES;
			break;
	}
	vmode |= (resources_get_int("MachineVideoStandard", &val) >= 0 && val == MACHINE_SYNC_NTSC) ? RP_GUESTSCREENFLAGS_MODE_NTSC : RP_GUESTSCREENFLAGS_MODE_PAL;
    return vmode;
}

static int press_key(BYTE row, BYTE column, key_alarm_func_t kaf)
{
    if (row == RW_EXTRA_FUNC)
    {
        switch (column)
        {
            case CL_FUNC_RESTORE:
                if (machine_has_restore_key())
                    return keyboard_restore_pressed(kaf);
                break;
            case CL_FUNC_COLUMN4080:
                if (key_ctrl_column4080_func != NULL)
                    key_ctrl_column4080_func();
                break;
            case CL_FUNC_CAPS:
                if (key_ctrl_caps_func != NULL)
                    key_ctrl_caps_func();
                break;
        }
        return FALSE;
    }
    else
    {
        return keyboard_do_key_pressed_matrix(row, column, kaf);
    }
}

static int release_key(BYTE row, BYTE column, key_alarm_func_t kaf)
{
    if (row == RW_EXTRA_FUNC)
    {
        switch (column)
        {
            case CL_FUNC_RESTORE:
                if (machine_has_restore_key())
                    return keyboard_restore_released(kaf);
                break;
        }
        return FALSE;
    }
    else
    {
        return keyboard_do_key_released_matrix(row, column, kaf);
    }
}

static void add_events(const char *events)
{
	char *pending_events;

	if (events)
	{
		EnterCriticalSection(&event_string_critical_section);

		if (event_string && event_string_ptr)
		{
			pending_events = lib_malloc(strlen(event_string_ptr) + 1);
			if (pending_events)
			{
				strcpy(pending_events, event_string_ptr);
                lib_free(event_string);
				event_string = lib_malloc(strlen(pending_events) + strlen(events) + 2);
				if (event_string)
				{
					strcpy(event_string, pending_events);
					strcat(event_string, " ");
					strcat(event_string, events);
					event_string_ptr = event_string;
				}
                lib_free(pending_events);
			}
		}
		else
		{
			event_string = lib_malloc(strlen(events) + 1);
			if (event_string)
			{
				strcpy(event_string, events);
				event_string_ptr = event_string;
			}
		}
		LeaveCriticalSection(&event_string_critical_section);
	}
}

static void set_event_pause_alarm(void)
{
    // an additional pause (after keyboard_alarm) is required,
    // or some key events may not produce the desired results
    // (e.g. Plus/4 Help key just after startup)
    alarm_set(event_pause_alarm, maincpu_clk + machine_get_cycles_per_frame());
}

static void event_pause_alarm_func(CLOCK offset, void *data)
{
    alarm_unset(event_pause_alarm);
    alarm_context_update_next_pending(event_pause_alarm->context);
    parse_events();
}

static void parse_events(void)
{
    static const rp_cmdname_t *cn;
    size_t ln;
    int alarm, i;
	char *es2;
	long key;
    bool fully_parsed = FALSE;

    if (!event_string_ptr)
	{
        return;
	}
	if (!event_pause_alarm)
		event_pause_alarm = alarm_new(maincpu_alarm_context, "RetroPlatformEventsPause", event_pause_alarm_func, NULL);

	EnterCriticalSection(&event_string_critical_section);

	for (;;)
    {
        for (; *event_string_ptr == ' '; event_string_ptr++);
        if (*event_string_ptr == 0)
        {
            fully_parsed = TRUE;
            break;
        }
        alarm = 0;
        for (ln = 0; event_string_ptr[ln] != 0 && event_string_ptr[ln] != ' '; ln++);

        for (cn = cmdnames, i = _countof(cmdnames); i > 0; i--, cn++)
        {
            if (_strnicmp(event_string_ptr, cn->name, ln) == 0)
            {
                event_string_ptr += ln;
                switch (cn->command)
                {
					case CN_KEY_RAW_DOWN:
						for (; *event_string_ptr == ' '; event_string_ptr++);
						key = strtol(event_string_ptr, &es2, 0);
						if (es2 != event_string_ptr)
						{
		                    alarm = press_key((BYTE)((key >> 8) & 0xFF), (BYTE)(key & 0xFF), set_event_pause_alarm);
							event_string_ptr = es2;
						}
						else i = 0;
						break;
					case CN_KEY_RAW_UP:
						for (; *event_string_ptr == ' '; event_string_ptr++);
						key = strtol(event_string_ptr, &es2, 0);
						if (es2 != event_string_ptr)
						{
		                    alarm = release_key((BYTE)((key >> 8) & 0xFF), (BYTE)(key & 0xFF), set_event_pause_alarm);
							event_string_ptr = es2;
						}
						else i = 0;
						break;
                    case CN_DISPLAY_SWITCH:
						ui_switch_display();
						break;
					case CN_CART_FREEZE:
						ui_cart_freeze();
						break;
                    case CN_TAPE_STOP:
						uidatasette_command(NULL, IDM_DATASETTE_CONTROL_STOP);
						break;
                    case CN_TAPE_PLAY:
						uidatasette_command(NULL, IDM_DATASETTE_CONTROL_START);
						break;
                    case CN_TAPE_FFORWARD:
						uidatasette_command(NULL, IDM_DATASETTE_CONTROL_FORWARD);
						break;
                    case CN_TAPE_REWIND:
						uidatasette_command(NULL, IDM_DATASETTE_CONTROL_REWIND);
						break;
                    case CN_TAPE_RECORD:
						uidatasette_command(NULL, IDM_DATASETTE_CONTROL_RECORD);
						break;
                    case CN_TAPE_RESET:
						uidatasette_command(NULL, IDM_DATASETTE_CONTROL_RESET);
						break;
                    case CN_TAPE_RESET_COUNTER:
						uidatasette_command(NULL, IDM_DATASETTE_RESET_COUNTER);
						break;
                }
                break;
            }
        }
        if (i <= 0) // unknown keyword: stop parsing
        {
            fully_parsed = TRUE;
            break;
        }
        if (alarm) // parse_events() gets called as soon as the keyboard is ready for another event
            break;
    }
    if (fully_parsed)
    {
        lib_free(event_string);
        event_string = NULL;
        event_string_ptr = NULL;
    }
	LeaveCriticalSection(&event_string_critical_section);
}

static void begin_parse_events(void)
{
	if (event_string_ptr)
	{
		if (event_pause_alarm)
		{
			if (event_pause_alarm->pending_idx >= 0) // alarm set
				return; // wait for the alarm to resume parsing
			else
			{
				// set the alarm anyway: it improves character output in some circumstances
				// ('é' character from an Italian PC keyboard, in a German C-128)
				set_event_pause_alarm();
				return; // wait for the alarm to start parsing
			}
		}
		parse_events();
	}
}

static void CALLBACK scheduled_refresh_proc(HWND hwnd, UINT message, UINT idTimer, DWORD dwSystemTime)
{
	KillTimer(hwnd, idTimer);
	scheduled_refresh = FALSE;
	video_canvas_refresh_all(video_canvas_for_hwnd(retroplatform_active_window));
}

static void schedule_refresh(void)
{
	if (!scheduled_refresh)
	{
		scheduled_refresh = TRUE;
		SetTimer(retroplatform_active_window, SCHEDULED_REFRESH_TIMER_ID, 16, scheduled_refresh_proc);
	}
}

static LRESULT CALLBACK retroplatform_message_function(UINT uMessage, WPARAM wParam, LPARAM lParam, LPCVOID pData, DWORD dwDataSize, LPARAM lMsgFunctionParam)
{
    switch (uMessage)
    {
        case RP_IPC_TO_GUEST_PING:
            return TRUE;

        case RP_IPC_TO_GUEST_GUESTAPIVERSION:
            return MAKELONG(RETROPLATFORM_API_VER_MAJOR, RETROPLATFORM_API_VER_MINOR);

        case RP_IPC_TO_GUEST_CLOSE:
            ui_silent_exit();
            return TRUE;

        case RP_IPC_TO_GUEST_SCREENMODE:
            return (dwDataSize >= sizeof(RPSCREENMODE)) ? (LRESULT)change_screenmode(((const RPSCREENMODE *)pData)->dwScreenMode) : 0;

        case RP_IPC_TO_GUEST_PAUSE:
		{
			BOOL fullwindow = RP_SCREENMODE_DISPLAY(retroplatform_screen_mode) && RP_SCREENMODE_FULLSCREEN(retroplatform_screen_mode) == RP_SCREENMODE_FULLSCREEN_SHARED;
            if (wParam) // enter pause mode
            {
                if (!ui_emulation_is_paused())
                {
                    if (retroplatform_mouse)
                        retroplatform_release_mouse();
                    ui_pause_emulation(TRUE);
					if (fullwindow)
						cursor_active += 1;
                }
            }
            else // exit pause mode
            {
                if (ui_emulation_is_paused())
                {
                    ui_pause_emulation(FALSE);
					if (fullwindow)
						cursor_active -= 1;
                    RedrawWindow((HWND)retroplatform_active_window, NULL, NULL, RDW_INVALIDATE); // needed when retroplatform_active_window is not focussed
                    if (retroplatform_mouse && retroplatform_active_window && GetForegroundWindow() == retroplatform_active_window)
                        retroplatform_capture_mouse();
                }
            }
			if (fullwindow)
				PostMessage(retroplatform_active_window, WM_SETCURSOR, (WPARAM)retroplatform_active_window, (LPARAM)MAKELONG(HTCLIENT,WM_MOUSEMOVE)); // hide/show cursor, based on cursor_active
            return TRUE;
		}
        case RP_IPC_TO_GUEST_MOUSECAPTURE:
            if (retroplatform_mouse)
            {
                if (wParam & RP_MOUSECAPTURE_CAPTURED)
                {
                    int mouse;
                    if (resources_get_int("Mouse", &mouse) >= 0 && mouse == 0)
                        resources_set_int("Mouse", 1);
                }
                else
                {
                    int mouse;
                    if (resources_get_int("Mouse", &mouse) >= 0 && mouse != 0)
                        resources_set_int("Mouse", 0);
                }
            }
            return TRUE;

        case RP_IPC_TO_GUEST_FLUSH:
        {
            int d;
            for (d = 8; d < (8 + DRIVE_NUM); d++) // flush floppy changes
            {
                const char *disk_name = file_system_get_disk_name(d);
                if (disk_name)
                {
                    char *name = lib_malloc(strlen(disk_name) + 1);
                    strcpy(name, disk_name);
                    file_system_detach_disk(d);
                    file_system_attach_disk(d, name);
                    lib_free(name);
                }
            }
            return TRUE;
        }

        case RP_IPC_TO_GUEST_SCREENCAPTURE:
        {
            long rc = RP_SCREENCAPTURE_ERROR;
			const RPSCREENCAPTURE *pScreenCapture = (const RPSCREENCAPTURE *)pData;
			if (pScreenCapture)
			{
				int n;
				char *pszRawFile = NULL;
				char *pszFilteredFile = NULL;
				if (pScreenCapture->szScreenRaw[0])
				{
					n = WideCharToMultiByte(CP_ACP, 0, pScreenCapture->szScreenRaw, -1, NULL, 0, NULL, NULL);
					if (n)
					{
						pszRawFile = lib_malloc(n + 1);
						WideCharToMultiByte(CP_ACP, 0, pScreenCapture->szScreenRaw, -1, pszRawFile, n, NULL, NULL);
					}
				}
				if (pScreenCapture->szScreenFiltered[0])
				{
					n = WideCharToMultiByte(CP_ACP, 0, pScreenCapture->szScreenFiltered, -1, NULL, 0, NULL, NULL);
					if (n)
					{
						pszFilteredFile = lib_malloc(n + 1);
						WideCharToMultiByte(CP_ACP, 0, pScreenCapture->szScreenFiltered, -1, pszFilteredFile, n, NULL, NULL);
					}
				}
				if (pszRawFile || pszFilteredFile)
					rc = canvas_save_bmp(video_canvas_for_hwnd(retroplatform_active_window), pszRawFile, pszFilteredFile);
				if (pszRawFile)
					lib_free(pszRawFile);
				if (pszFilteredFile)
					lib_free(pszFilteredFile);
			}
            return (LRESULT)rc;
        }

        case RP_IPC_TO_GUEST_EVENT:
        {
			char *events;
            bool ok = FALSE;
            int n = WideCharToMultiByte(CP_ACP, 0, (LPCWSTR)pData, -1, NULL, 0, NULL, NULL);
            if (n)
            {
                events = lib_malloc(n + 1);
                if (WideCharToMultiByte(CP_ACP, 0, (LPCWSTR)pData, -1, events, n, NULL, NULL))
                {
					add_events(events);
                    begin_parse_events(); // this may complete asynchronously
                    ok = TRUE;
                }
                lib_free(events);
            }
            return (LRESULT)ok;
        }
        case RP_IPC_TO_GUEST_DEVICEREADWRITE:
            switch (LOBYTE(wParam))
            {
                case RP_DEVICECATEGORY_FLOPPY:
                case RP_DEVICECATEGORY_TAPE:
					if (resources_set_int_sprintf("AttachDevice%dReadonly", lParam == RP_DEVICE_READONLY, (int)HIBYTE(wParam)) >= 0)
						return TRUE;
					break;
			}
			return FALSE;

		case RP_IPC_TO_GUEST_DEVICECONTENT:
        {
            RPDEVICECONTENT *pDevImage = (RPDEVICECONTENT *)pData;
            bool ok = FALSE;
            int n = WideCharToMultiByte(CP_ACP, 0, pDevImage->szContent, -1, NULL, 0, NULL, NULL);
            if (n)
            {
                char *pszContent = lib_malloc(n + 1);
                if (WideCharToMultiByte(CP_ACP, 0, pDevImage->szContent, -1, pszContent, n, NULL, NULL))
                {
                    switch (pDevImage->btDeviceCategory)
                    {
                        case RP_DEVICECATEGORY_FLOPPY:
                            if (pszContent[0])
                            {
                                if (file_system_attach_disk(pDevImage->btDeviceNumber, pszContent) >= 0)
								{
									ok = TRUE;
									resources_set_int_sprintf("AttachDevice%dReadonly", RP_DEVICEFLAGS_RW(pDevImage->dwFlags) == RP_DEVICEFLAGS_RW_READONLY, (int)pDevImage->btDeviceNumber);
								}
                            }
                            else
                            {
                                file_system_detach_disk(pDevImage->btDeviceNumber);
                                ok = TRUE;
                            }
                            break;
                        case RP_DEVICECATEGORY_TAPE:
						{
                            if (pszContent[0])
                            {
                                if ((n = tape_image_attach(pDevImage->btDeviceNumber, pszContent)) >= 0)
								{
                                    ok = TRUE;
									resources_set_int_sprintf("AttachDevice%dReadonly", RP_DEVICEFLAGS_RW(pDevImage->dwFlags) == RP_DEVICEFLAGS_RW_READONLY, (int)pDevImage->btDeviceNumber);
								}
                            }
                            else
                            {
                                if (tape_image_detach(pDevImage->btDeviceNumber) >= 0)
                                    ok = TRUE;
                            }
                            break;
						}
                        case RP_DEVICECATEGORY_INPUTPORT:
                        {
                            int val;
							LPCSTR pszProperty = (pDevImage->btDeviceNumber == 1) ? "JoyPort1Device" : "JoyPort2Device";

							if (resources_get_int(pszProperty, &val) >= 0)
                            {
                                if (retroplatform_mouse && val == JOYPORT_ID_MOUSE_1351)
                                {
                                    retroplatform_mouse = FALSE;
                                    resources_set_int("Mouse", 0);
                                }
                            }
							switch (pDevImage->dwInputDevice)
							{
								case RP_INPUTDEVICE_EMPTY:
								case RP_INPUTDEVICE_JOYSTICK:
									if (pDevImage->btDeviceNumber >= 1 && pDevImage->btDeviceNumber <= 2)
									{
										if (resources_set_int(pszProperty, JOYPORT_ID_JOYSTICK) >= 0)
										{
											input_port_device[pDevImage->btDeviceNumber-1] = pDevImage->dwInputDevice;
											ok = TRUE;
										}
									}
									break;
								case RP_INPUTDEVICE_MOUSE:
									if (pDevImage->btDeviceNumber >= 1 && pDevImage->btDeviceNumber <= 2 &&
										_stricmp(pszContent, mouse_id) == 0)
									{
										if (resources_set_int(pszProperty, JOYPORT_ID_MOUSE_1351) >= 0)
										{
											input_port_device[pDevImage->btDeviceNumber-1] = pDevImage->dwInputDevice;
											retroplatform_mouse = TRUE;
											ok = TRUE;
										}
									}
									break;
                            }
                            break;
                        }
                        case RP_DEVICECATEGORY_MULTITAPPORT:
							switch (pDevImage->dwInputDevice)
							{
								case RP_INPUTDEVICE_EMPTY:
								case RP_INPUTDEVICE_JOYSTICK:
									if (pDevImage->btDeviceNumber >= 1 && pDevImage->btDeviceNumber <= 2)
									{
										multitap_port_device[pDevImage->btDeviceNumber-1] = pDevImage->dwInputDevice;
										ok = TRUE;
									}
									break;
                            }
                            break;
                    }
                }
                lib_free(pszContent);
            }
            return (LRESULT)ok;
        }

        case RP_IPC_TO_GUEST_RESET:
            machine_trigger_reset((wParam == RP_RESET_SOFT) ? MACHINE_RESET_MODE_SOFT : MACHINE_RESET_MODE_HARD);
            return TRUE;

        case RP_IPC_TO_GUEST_TURBO:
            if (wParam & RP_TURBO_CPU)
			{
				if (resources_set_int("WarpMode", 0) < 0)
					return FALSE;
				if (resources_set_int("Speed", (lParam & RP_TURBO_CPU) ? 0 : 100) < 0)
					return FALSE;
			}
            if (wParam & RP_TURBO_FLOPPY)
			{
				if (resources_set_int("DriveTrueEmulation", (lParam & RP_TURBO_FLOPPY) ? 0 : 1) < 0)
					return FALSE;
				if (resources_set_int("VirtualDevices", (lParam & RP_TURBO_FLOPPY) ? 1 : 0) < 0)
					return FALSE;
			}
            return TRUE;

        case RP_IPC_TO_GUEST_VOLUME:
            return (resources_set_int("SoundVolume", (int)wParam) >= 0) ? TRUE : FALSE;

        case RP_IPC_TO_GUEST_SAVESTATE:
        {
            char *pszFile;
            bool ok = FALSE;
            int n = WideCharToMultiByte(CP_ACP, 0, (LPCWSTR)pData, -1, NULL, 0, NULL, NULL);
            if (n)
            {
                pszFile = lib_malloc(n + 1);
                if (WideCharToMultiByte(CP_ACP, 0, (LPCWSTR)pData, -1, pszFile, n, NULL, NULL))
                {
                    int paused = ui_emulation_is_paused();
                    if (!paused)
                        ui_pause_emulation(TRUE);
                    if (machine_write_snapshot(pszFile, TRUE, TRUE, 0) >= 0)
                        ok = TRUE;
                    if (!paused)
                    {
                        ui_pause_emulation(FALSE);
                        RedrawWindow((HWND)retroplatform_active_window, NULL, NULL, RDW_INVALIDATE);
                    }
                }
                lib_free(pszFile);
            }
            return (LRESULT)ok;
        }

        case RP_IPC_TO_GUEST_LOADSTATE:
        {
            char *pszFile;
            bool ok = FALSE;
            int n = WideCharToMultiByte(CP_ACP, 0, (LPCWSTR)pData, -1, NULL, 0, NULL, NULL);
            if (n)
            {
                pszFile = lib_malloc(n + 1);
                if (WideCharToMultiByte(CP_ACP, 0, (LPCWSTR)pData, -1, pszFile, n, NULL, NULL))
                {
                    int paused = ui_emulation_is_paused();
                    if (!paused)
                        ui_pause_emulation(TRUE);
                    if (machine_read_snapshot(pszFile, 0) >= 0)
                        ok = TRUE;
                    if (!paused)
                    {
                        ui_pause_emulation(FALSE);
                        RedrawWindow((HWND)retroplatform_active_window, NULL, NULL, RDW_INVALIDATE);
                    }
                }
                lib_free(pszFile);
            }
            return (LRESULT)ok;
        }

		case RP_IPC_TO_GUEST_DEVICEACTIVITY:
			switch (LOBYTE(wParam))
			{
				case RP_DEVICECATEGORY_INPUTPORT:
				case RP_DEVICECATEGORY_MULTITAPPORT:
				{
					unsigned int nPort = (unsigned int)HIBYTE(wParam);
					BYTE value = 0;
					if (lParam & RP_JOYSTICK_UP)
						value |= (1<<0);
					if (lParam & RP_JOYSTICK_DOWN)
						value |= (1<<1);
					if (lParam & RP_JOYSTICK_LEFT)
						value |= (1<<2);
					if (lParam & RP_JOYSTICK_RIGHT)
						value |= (1<<3);
					if (lParam & RP_JOYSTICK_BUTTON1)
						value |= (1<<4);

					if (nPort >= 1 &&
						nPort <= 2)
					{
						if (LOBYTE(wParam) == RP_DEVICECATEGORY_INPUTPORT &&
							input_port_device[nPort-1] == RP_INPUTDEVICE_JOYSTICK)
						{
							joystick_set_value_absolute(nPort, value);
							return 1;
						}
						else if (LOBYTE(wParam) == RP_DEVICECATEGORY_MULTITAPPORT &&
								 multitap_port_device[nPort-1] == RP_INPUTDEVICE_JOYSTICK)
						{
							joystick_set_value_absolute(nPort + 2, value);
							return 1;
						}
					}
					break;
				}
			}
			break;

		case RP_IPC_TO_GUEST_SCREENOVERLAY:
		{
			LRESULT lr = 0;
			video_canvas_t *c;
			RPSCREENOVERLAY *pOverlay;
			c = video_canvas_for_hwnd(retroplatform_active_window);
			pOverlay = (RPSCREENOVERLAY *)pData;
			if (pOverlay)
			{
				if (pOverlay->lWidth > 0 &&
					pOverlay->lHeight > 0 &&
					pOverlay->dwFormat == RPSOPF_32BIT_BGRA)
				{
					if (video_canvas_overlay_create_dx9(c, pOverlay->dwIndex, pOverlay->lWidth, pOverlay->lHeight, pOverlay->lLeft, pOverlay->lTop, pOverlay->btData) == 0)
						lr = 1;
				}
			}
			if (lr != 0 && ui_emulation_is_paused())
				schedule_refresh();
			return lr;
		}
		case RP_IPC_TO_GUEST_MOVESCREENOVERLAY:
		{
			video_canvas_t *c = video_canvas_for_hwnd(retroplatform_active_window);
			if (video_canvas_overlay_move_dx9(c, (unsigned long)wParam, LOWORD(lParam), HIWORD(lParam)) == 0)
			{
				if (ui_emulation_is_paused())
					schedule_refresh();
				return 1;
			}
			break;
		}
		case RP_IPC_TO_GUEST_DELETESCREENOVERLAY:
		{
			video_canvas_t *c = video_canvas_for_hwnd(retroplatform_active_window);
			if (video_canvas_overlay_delete_dx9(c, (unsigned long)wParam) == 0)
			{
				if (ui_emulation_is_paused())
					schedule_refresh();
				return 1;
			}
			break;
		}
		case RP_IPC_TO_GUEST_SENDMOUSEEVENTS:
		{
			RECT rc;
			POINT pt;
			BOOL fullwindow = RP_SCREENMODE_DISPLAY(retroplatform_screen_mode) && RP_SCREENMODE_FULLSCREEN(retroplatform_screen_mode) == RP_SCREENMODE_FULLSCREEN_SHARED;
			retroplatform_send_mouse_events = (wParam != 0);
			if (retroplatform_send_mouse_events)
			{
				if (retroplatform_mouse)
					resources_set_int("Mouse", 0);
				if (fullwindow)
					cursor_active += 1;
				GetWindowRect(retroplatform_active_window, &rc);
				GetCursorPos(&pt);
				if (pt.x < rc.left)
					pt.x = rc.left;
				if (pt.x >= rc.right)
					pt.x = rc.right - 1;
				if (pt.y < rc.top)
					pt.y = rc.top;
				if (pt.y >= rc.bottom)
					pt.y = rc.bottom - 1;
				SetCursorPos(pt.x, pt.y); // correct cursor position and causes an initial RP_IPC_TO_HOST_MOUSEMOVE to be posted
			}
			else
			{
				if (retroplatform_mouse)
					resources_set_int("Mouse", 1);
				if (fullwindow)
					cursor_active -= 1;
			}
			if (fullwindow)
				PostMessage(retroplatform_active_window, WM_SETCURSOR, (WPARAM)retroplatform_active_window, (LPARAM)MAKELONG(HTCLIENT,WM_MOUSEMOVE)); // hide/show cursor, based on cursor_active
			return 1;
		}
		case RP_IPC_TO_GUEST_SHOWDEBUGGER:
            if (!ui_emulation_is_paused()) {
                monitor_startup_trap();
            }
			return 1;
	}
    return 0;
}

void retroplatform_monitor_active(unsigned int active)
{
    RPPostMessage(RP_IPC_TO_HOST_PAUSE, active, 0, &retroplatform_guest_info);
}

static void retroplatform_exit(void)
{
	DeleteCriticalSection(&event_string_critical_section);

    if (retroplatform_guest_mode)
    {
        RPPostMessage(RP_IPC_TO_HOST_CLOSED, 0, 0, &retroplatform_guest_info);
        RPUninitializeGuest(&retroplatform_guest_info);
    }
	if (platform_dll)
	{
        FreeLibrary(platform_dll);
        pfnRPSetCBMCursorPosition = NULL;
	}
    if (user32)
    {
        FreeLibrary(user32);
        pfnEnumDisplayMonitors = NULL;
    }
}

HRESULT retroplatform_initialize(const char *pszInfo)
{
    HRESULT hr;
    char path[MAX_PATH], *p;
    DWORD count;
    PFN_RPSetCrashReporterA pfnSetCrashReporter;

    hr = RPInitializeGuest(&retroplatform_guest_info, winmain_instance, pszInfo, retroplatform_message_function, 0);
    if (FAILED(hr))
        return hr;

    pfnSetCrashReporter = (PFN_RPSetCrashReporterA)GetProcAddress(retroplatform_guest_info.hRPGuestDLL, "RPSetCrashReporterA");
    if (pfnSetCrashReporter)
    {
        hr = pfnSetCrashReporter("VICE", "vice", VERSION, "Cloanto\\RetroPlatform\\VICE\\CrashDumps");
        if (SUCCEEDED(hr))
            debug.do_core_dumps = 1; // do not set signals, when calling signals_init()
    }

    user32 = LoadLibrary("user32.dll");
    pfnEnumDisplayMonitors = user32 ? (PFN_EnumDisplayMonitors)GetProcAddress(user32, "EnumDisplayMonitors") : NULL;

	pfnRPSetCBMCursorPosition = NULL;
	count = GetModuleFileName(NULL, path, MAX_PATH);
	if (count > 0 && count < MAX_PATH)
	{
		for (p = path + strlen(path) - 1; p > path && *p != '\\'; p--);

		if (p > path && ((p - path) + strlen(platform_dll_path) + 1) < MAX_PATH)
		{
			strcpy(p+1, platform_dll_path);
			platform_dll = LoadLibrary(path);
			if (platform_dll)
				pfnRPSetCBMCursorPosition = (PFN_RPSetCBMCursorPosition)GetProcAddress(platform_dll, "RPSetCBMCursorPosition");
		}
	}

	InitializeCriticalSectionAndSpinCount(&event_string_critical_section, 1024); 

	retroplatform_guest_mode = TRUE;
    atexit(retroplatform_exit);

    return NOERROR;
}

void set_retroplatform_enabled_tapes(const char *pszEnabledTapes)
{
    enabled_tapes = strtoul(pszEnabledTapes, NULL, 0);
}

void set_retroplatform_screen_mode(const char *pszScreenMode)
{
    retroplatform_screen_mode = (DWORD)strtoul(pszScreenMode, NULL, 0);

	set_screen_nx(RP_SCREENMODE_SCALE(retroplatform_screen_mode));

    if (RP_SCREENMODE_DISPLAY(retroplatform_screen_mode))
    {
        if (RP_SCREENMODE_FULLSCREEN(retroplatform_screen_mode) != RP_SCREENMODE_FULLSCREEN_SHARED ||
            !video_dx9_enabled())
        {
            if (resources_set_int("FullScreenEnabled", 1) < 0)
                retroplatform_screen_mode &= ~RP_SCREENMODE_DISPLAYMASK;
        }
    }
	set_video_filter((retroplatform_screen_mode & RP_SCREENMODE_SCANLINES) != 0);
}

void set_retroplatform_scale2x(const char *pszScale2X)
{
    scale_2x = strtoul(pszScale2X, NULL, 0) != 0;
	set_video_filter((retroplatform_screen_mode & RP_SCREENMODE_SCANLINES) != 0);
}

void set_retroplatform_dpiawareness(const char *pszAwareness)
{
	typedef enum _RP_Process_DPI_Awareness { 
	  RP_Process_DPI_Unaware            = 0,
	  RP_Process_System_DPI_Aware       = 1,
	  RP_Process_Per_Monitor_DPI_Aware  = 2
	} RP_Process_DPI_Awareness;
	typedef HRESULT (WINAPI *PFN_SetProcessDpiAwareness)(RP_Process_DPI_Awareness a);
	typedef BOOL (WINAPI *PFN_SetProcessDPIAware)(void);
	PFN_SetProcessDpiAwareness pfnSetProcessDpiAwareness;
	PFN_SetProcessDPIAware pfnSetProcessDPIAware;
	RP_Process_DPI_Awareness nAwareness;
	HINSTANCE hCoreDll, uUser32;
	HRESULT hr;

	nAwareness = (int)strtoul(pszAwareness, NULL, 0);
	hr = E_NOTIMPL;
	hCoreDll = LoadLibrary("Shcore.dll");
	if (hCoreDll)
	{
		pfnSetProcessDpiAwareness = (PFN_SetProcessDpiAwareness)GetProcAddress(hCoreDll, "SetProcessDpiAwareness");
		if (pfnSetProcessDpiAwareness)
			hr = pfnSetProcessDpiAwareness(nAwareness);
		FreeLibrary(hCoreDll);
	}
	if (hr == E_NOTIMPL && nAwareness > 0) // Windows Vista / Windows 7
	{
		uUser32 = LoadLibrary("user32.dll");
		if (uUser32)
		{
			pfnSetProcessDPIAware = (PFN_SetProcessDPIAware)GetProcAddress(uUser32, "SetProcessDPIAware");
			if (pfnSetProcessDPIAware)
				pfnSetProcessDPIAware();
			FreeLibrary(uUser32);
		}
	}
}

void enable_retroplatform_virtualmouse(const char *pszEnable)
{
	virtualmouse_enabled = (int)strtoul(pszEnable, NULL, 0);
}

unsigned long retroplatform_parent(void)
{
    LRESULT lr;
    RPSendMessage(RP_IPC_TO_HOST_PARENT, 0, 0, NULL, 0, &retroplatform_guest_info, &lr);
    return (unsigned long)lr;
}

void set_retroplatform_window(void *window, int index)
{
    int width, height, width2, height2;
    RPSCREENMODE sm;
    use_tv_modes tvm;
    RECT rc;

	if (changing_screen_mode)
        return;

    memset(&sm, 0, sizeof(sm));
    GetClientRect((HWND)window, &rc);
    width = rc.right;
    height = rc.bottom;
    retroplatform_active_window = (HWND)window;

	if (machine_class == VICE_MACHINE_C128 && index != retroplatform_screen_index) // switching 40/80 columns
	{
		retroplatform_screen_modes[retroplatform_screen_index] = retroplatform_screen_mode;
		retroplatform_screen_mode = retroplatform_screen_modes[index];
	}

	if (RP_SCREENMODE_DISPLAY(retroplatform_screen_mode))
    {
        if (RP_SCREENMODE_FULLSCREEN(retroplatform_screen_mode) == RP_SCREENMODE_FULLSCREEN_SHARED &&
            video_dx9_enabled())
        {
            if (!set_fullwindow((HWND)window, RP_SCREENMODE_DISPLAY(retroplatform_screen_mode), retroplatform_screen_mode))
                return;
        }
        else
        {
            switch (RP_SCREENMODE_USETVM(retroplatform_screen_mode))
            {
                case RP_SCREENMODE_USETVM_ALWAYS: tvm = utvm_always; break;
                case RP_SCREENMODE_USETVM_AUTO:   tvm = utvm_auto;   break;
                default:                          tvm = utvm_never;  break;
            }
			if (!get_display_rect(RP_SCREENMODE_DISPLAY(retroplatform_screen_mode), &rc))
				return;
            select_best_fullscreen_mode(&rc, width, height, tvm);
        }
    }
	else
	{
		if (get_correct_window_size((HWND)window, retroplatform_screen_mode, &width2, &height2))
		{
			if (width2 != width || height2 != height)
				SetWindowPos((HWND)window, 0, 0, 0, width2, height2, SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER);
		}
	}
	retroplatform_screen_initialized[index] = TRUE;

	retroplatform_screen_index = index;
    sm.dwScreenMode = retroplatform_screen_mode;
    sm.lClipLeft = sm.lClipTop = sm.lClipWidth = sm.lClipHeight = -1;  // unused 
    sm.hGuestWindow = (HWND)window;
    RPSendMessage(RP_IPC_TO_HOST_SCREENMODE, 0, 0, &sm, sizeof(sm), &retroplatform_guest_info, NULL);
}

static void send_device_content(int category, int devnum, int inputdev, int flags, const char *content)
{
    RPDEVICECONTENT dc;
    memset(&dc, 0, sizeof(dc));
    dc.btDeviceCategory = category;
    dc.btDeviceNumber = devnum;
    dc.dwInputDevice = inputdev;
    dc.dwFlags = flags;
    MultiByteToWideChar(CP_ACP, 0, content, -1, dc.szContent, _countof(dc.szContent));
    RPSendMessage(RP_IPC_TO_HOST_DEVICECONTENT, 0, 0, &dc, sizeof(dc), &retroplatform_guest_info, NULL);
}

void retroplatform_enumerate_input_devices(void)
{
    RPINPUTDEVICEDESCRIPTION idd;

    // reports about mouse
    memset(&idd, 0, sizeof(idd));
    idd.dwHostInputType = RP_HOSTINPUT_MOUSE;
    MultiByteToWideChar(CP_ACP, 0, mouse_id, -1, idd.szHostInputID, _countof(idd.szHostInputID));
    MultiByteToWideChar(CP_ACP, 0, "Windows Mouse", -1, idd.szHostInputName, _countof(idd.szHostInputName));
    idd.dwInputDeviceFeatures = RP_FEATURE_INPUTDEVICE_MOUSE;
    idd.dwFlags = RP_HOSTINPUTFLAGS_MOUSE_SMART;
    RPSendMessage(RP_IPC_TO_HOST_INPUTDEVICE, 0, 0, &idd, sizeof(idd), &retroplatform_guest_info, NULL);

    memset(&idd, 0, sizeof(idd));
    idd.dwHostInputType = RP_HOSTINPUT_END;
    RPSendMessage(RP_IPC_TO_HOST_INPUTDEVICE, 0, 0, &idd, sizeof(idd), &retroplatform_guest_info, NULL);
}

static BOOL is_device_readonly(int unit)
{
	int val;
	return resources_get_int_sprintf("AttachDevice%dReadonly", &val, unit) >= 0 ? (val != 0) : FALSE;
}

static BOOL is_t64_file(const char *filename)
{
	size_t len = strlen(filename);
	return filename != NULL && len > 4 && _stricmp(filename + len - 4, ".t64") == 0;
}

static bool get_turbo_cpu(void)
{
	int val;
	if (resources_get_int("WarpMode", &val) >= 0 && val != 0)
		return TRUE;
	if (resources_get_int("Speed", &val) >= 0 && val == 0)
		return TRUE;
	return FALSE;
}

static bool get_turbo_floppy(void)
{
	int val;
	if (resources_get_int("DriveTrueEmulation", &val) >= 0 && val == 0)
		return TRUE;
	return FALSE;
}

void retroplatform_startup(void)
{
    DWORD features, devicemask;
    char szDevice[80];
    const char *content, *editor;
    int val, d;

    // tell the host about supported features
    features = RP_FEATURE_POWERLED |
               RP_FEATURE_SCREEN1X |
               RP_FEATURE_SCREEN2X |
               RP_FEATURE_SCREEN3X |
               RP_FEATURE_SCREEN4X |
               RP_FEATURE_FULLSCREEN |
               RP_FEATURE_SCALING_SUBPIXEL |
               RP_FEATURE_SCALING_STRETCH |
               RP_FEATURE_PAUSE |
               RP_FEATURE_VOLUME |
               RP_FEATURE_TURBO_CPU |
               RP_FEATURE_TURBO_FLOPPY |
               RP_FEATURE_SCREENCAPTURE |
               RP_FEATURE_STATE |
               RP_FEATURE_SCANLINES |
               RP_FEATURE_INPUTDEVICE_JOYSTICK |
               RP_FEATURE_INPUTDEVICE_MOUSE |
			   RP_FEATURE_DEVICEREADWRITE |
			   RP_FEATURE_SCREENOVERLAY;

    RPSendMessage(RP_IPC_TO_HOST_FEATURES, features, 0, NULL, 0, &retroplatform_guest_info, NULL);

    retroplatform_enumerate_input_devices();

    // turns on the power led
    RPSendMessage(RP_IPC_TO_HOST_POWERLED, 100, 0, NULL, 0, &retroplatform_guest_info, NULL);

    // tell the host about initial sound volume
    if (resources_get_int("SoundVolume", &val) >= 0)
        RPSendMessage(RP_IPC_TO_HOST_VOLUME, val, 0, NULL, 0, &retroplatform_guest_info, NULL);

    // tell the host about attached drives
    devicemask = 0;
    for (d = 8; d < (8 + DRIVE_NUM); d++)
    {
        wsprintf(szDevice, "Drive%dType", d);
        if (resources_get_int(szDevice, &val) >= 0 && val != 0)
            devicemask |= 1 << d;
    }
    RPSendMessage(RP_IPC_TO_HOST_DEVICES, RP_DEVICECATEGORY_FLOPPY, devicemask, NULL, 0, &retroplatform_guest_info, NULL);

    for (d = 8; d < (8 + DRIVE_NUM); d++)
    {
        content = file_system_get_disk_name(d);
        if (content)
			send_device_content(RP_DEVICECATEGORY_FLOPPY, d, 0, is_device_readonly(d) ? RP_DEVICEFLAGS_RW_READONLY : RP_DEVICEFLAGS_RW_READWRITE, content);
	}

	if (machine_class == VICE_MACHINE_PET)
	{
		editor = NULL;
		if (resources_get_string("EditorName", &editor) >= 0 && editor != NULL)
		{
			if (stricmp(editor, "edit1g") == 0)
				enabled_tapes |= (1 << 1); // PET-2001 has built-in tape (unit 1)
		}
	}
	// tell the host about attached tapes
    RPSendMessage(RP_IPC_TO_HOST_DEVICES, RP_DEVICECATEGORY_TAPE, enabled_tapes, NULL, 0, &retroplatform_guest_info, NULL);

    content = tape_get_file_name();
    if (content)
	{
        send_device_content(RP_DEVICECATEGORY_TAPE, 1, 0, (is_device_readonly(1) || is_t64_file(content)) ? RP_DEVICEFLAGS_RW_READONLY : RP_DEVICEFLAGS_RW_READWRITE, content);
	}
	if (machine_class != VICE_MACHINE_PET &&
	    machine_class != VICE_MACHINE_CBM6x0)
	{
	    // tell the host about emulated input ports (port 1 and 2)
		RPSendMessage(RP_IPC_TO_HOST_DEVICES, RP_DEVICECATEGORY_INPUTPORT, (1<<1)|(1<<2), NULL, 0, &retroplatform_guest_info, NULL);
	}

    if (machine_class == VICE_MACHINE_PLUS4)
	{
        if (resources_get_int("SIDCartJoy", &val) >= 0 && val != 0)
			RPSendMessage(RP_IPC_TO_HOST_DEVICES, RP_DEVICECATEGORY_MULTITAPPORT, (1<<1), NULL, 0, &retroplatform_guest_info, NULL);
	}
	else
	{
		if (resources_get_int("UserportJoy", &val) >= 0 && val != 0)
		{
			if (resources_get_int("UserportJoyType", &val) >= 0)
			{
				RPSendMessage(RP_IPC_TO_HOST_DEVICES, RP_DEVICECATEGORY_MULTITAPPORT,
				              (val == USERPORT_JOYSTICK_HUMMER || val == USERPORT_JOYSTICK_OEM) ? (1<<1) : ((1<<1)|(1<<2)),
							  NULL, 0, &retroplatform_guest_info, NULL);
			}
		}
	}

    for (d = 1; d <= 2; d++)
    {
        if (retroplatform_mouse &&
            resources_get_int((d == 1) ? "JoyPort1Device" : "JoyPort2Device", &val) >= 0)
        {
            if (val == JOYPORT_ID_MOUSE_1351)
                send_device_content(RP_DEVICECATEGORY_INPUTPORT, d, RP_INPUTDEVICE_MOUSE, 0, mouse_id);
        }
    }

	d = 0;
	if (get_turbo_cpu())
		d |= RP_TURBO_CPU;
	if (get_turbo_floppy())
		d |= RP_TURBO_FLOPPY;
	RPSendMessage(RP_IPC_TO_HOST_TURBO, RP_TURBO_CPU | RP_TURBO_FLOPPY, (LPARAM)d, NULL, 0, &retroplatform_guest_info, NULL);

	retroplatform_screen_initialized[0] = retroplatform_screen_initialized[1] = FALSE;

	if (machine_class == VICE_MACHINE_C128)
	{
		retroplatform_screen_modes[1] = retroplatform_screen_mode;
		retroplatform_screen_modes[0] = RP_SCREENMODE_DISPLAY(retroplatform_screen_mode) ? retroplatform_screen_mode :
										((retroplatform_screen_mode & ~RP_SCREENMODE_SCALEMASK) | ((RP_SCREENMODE_SCALE(retroplatform_screen_mode) == RP_SCREENMODE_SCALE_4X) ? RP_SCREENMODE_SCALE_2X : RP_SCREENMODE_SCALE_1X));
	}
	else retroplatform_screen_modes[0] = retroplatform_screen_modes[1] = retroplatform_screen_mode;

	// final step: tell the host about actual screen mode and window handle
	retroplatform_screen_index = (machine_class == VICE_MACHINE_C128 && resources_get_int("40/80ColumnKey", &val) >= 0) ? (val ? 1 : 0) : number_of_windows-1; // for C128: window[1] = 40-column window; windows[0] = 80-column window; ("40/80ColumnKey" == 0: 80 column mode active)
    set_retroplatform_window(window_handles[retroplatform_screen_index], retroplatform_screen_index);

    retroplatform_started_up = TRUE;
}

void retroplatform_report_turbo_cpu(void) // called when Warpmode is set/unset in autostart.c
{
	bool tc = get_turbo_cpu();
	if (tc != turbo_cpu)
	{
		turbo_cpu = tc;
		RPSendMessage(RP_IPC_TO_HOST_TURBO, RP_TURBO_CPU, (LPARAM)(turbo_cpu ? RP_TURBO_CPU : 0), NULL, 0, &retroplatform_guest_info, NULL);
	}
}

void retroplatform_enable_drive_status(unsigned int enable)
{
    enabled_drives = enable;
}

void retroplatform_display_drive_led(int drivenum, int status, int write_mode)
{
    if (enabled_drives & (1 << drivenum))
    {
        int s = (status >= 500) ? -1 : 0;
        int c = write_mode ? RP_DEVICEACTIVITY_WRITE : RP_DEVICEACTIVITY_READ;
        RPPostMessage(RP_IPC_TO_HOST_DEVICEACTIVITY, MAKEWORD(RP_DEVICECATEGORY_FLOPPY, drivenum+8), MAKELONG(s,c), &retroplatform_guest_info);
    }
}

void retroplatform_blink_tape_led(int time, int write_mode)
{
    int s = time;  // milliseconds
    int c = write_mode ? RP_DEVICEACTIVITY_WRITE : RP_DEVICEACTIVITY_READ;
    RPPostMessage(RP_IPC_TO_HOST_DEVICEACTIVITY, MAKEWORD(RP_DEVICECATEGORY_TAPE, 1), MAKELONG(s,c), &retroplatform_guest_info);
}

static void save_accessibility(void)
{
	got_sticky_keys = SystemParametersInfo(SPI_GETSTICKYKEYS, sizeof(STICKYKEYS), &saved_sticky_keys, 0);
    got_toggle_keys = SystemParametersInfo(SPI_GETTOGGLEKEYS, sizeof(TOGGLEKEYS), &saved_toggle_keys, 0);
    got_filter_keys = SystemParametersInfo(SPI_GETFILTERKEYS, sizeof(FILTERKEYS), &saved_filter_keys, 0);
}

static void enable_accessibility_shortcuts(BOOL enable)
{
	STICKYKEYS sticky_keys;
	TOGGLEKEYS toggle_keys;
	FILTERKEYS filter_keys;    

	if (enable)
	{
		if (got_sticky_keys)
			SystemParametersInfo(SPI_SETSTICKYKEYS, sizeof(STICKYKEYS), &saved_sticky_keys, 0);
		if (got_toggle_keys)
	        SystemParametersInfo(SPI_SETTOGGLEKEYS, sizeof(TOGGLEKEYS), &saved_toggle_keys, 0);
		if (got_filter_keys)
		    SystemParametersInfo(SPI_SETFILTERKEYS, sizeof(FILTERKEYS), &saved_filter_keys, 0);
	}
	else
	{
		if (got_sticky_keys && (saved_sticky_keys.dwFlags & SKF_STICKYKEYSON) == 0)
		{
	        sticky_keys = saved_sticky_keys;
			sticky_keys.dwFlags &= ~SKF_HOTKEYACTIVE;
			sticky_keys.dwFlags &= ~SKF_CONFIRMHOTKEY;
			SystemParametersInfo(SPI_SETSTICKYKEYS, sizeof(STICKYKEYS), &sticky_keys, 0);
		}
		if (got_toggle_keys && (saved_toggle_keys.dwFlags & TKF_TOGGLEKEYSON) == 0)
		{
			toggle_keys = saved_toggle_keys;
			toggle_keys.dwFlags &= ~TKF_HOTKEYACTIVE;
			toggle_keys.dwFlags &= ~TKF_CONFIRMHOTKEY;
			SystemParametersInfo(SPI_SETTOGGLEKEYS, sizeof(TOGGLEKEYS), &toggle_keys, 0);
		}
		if (got_filter_keys && (saved_filter_keys.dwFlags & FKF_FILTERKEYSON) == 0)
		{
			filter_keys = saved_filter_keys;
			filter_keys.dwFlags &= ~FKF_HOTKEYACTIVE;
			filter_keys.dwFlags &= ~FKF_CONFIRMHOTKEY;
			SystemParametersInfo(SPI_SETFILTERKEYS, sizeof(FILTERKEYS), &filter_keys, 0);
		}
	}
}

void retroplatform_wm_activateapp(WPARAM wParam, LPARAM lParam)
{
	video_canvas_t *c;

	if (wParam) // application acivated
	{
		if (IsFullscreenEnabled())
		{
			c = video_canvas_for_hwnd(retroplatform_active_window);
			if (c)
			{
				// restore fullscreen after devicelost (e.g. reactivating after an Alt-Tab)
				video_device_release_dx9(c);
				video_device_create_dx9(c, 1);
				video_canvas_reset_dx9(c);
				video_canvas_refresh_all(c);
			}
		}
		save_accessibility();
		enable_accessibility_shortcuts(FALSE);
	}
	else
	{
		enable_accessibility_shortcuts(TRUE);
	}
    RPSendMessage(wParam ? RP_IPC_TO_HOST_ACTIVATED : RP_IPC_TO_HOST_DEACTIVATED, 0, lParam, NULL, 0, &retroplatform_guest_info, NULL);
}

void retroplatform_wm_activate(WPARAM wParam, LPARAM lParam)
{
	if (wParam == WA_CLICKACTIVE)
		window_clickactive_time[retroplatform_screen_index] = GetTickCount();
}

void retroplatform_wm_enable(WPARAM wParam)
{
    RPSendMessage(wParam ? RP_IPC_TO_HOST_ENABLED : RP_IPC_TO_HOST_DISABLED, 0, 0, NULL, 0, &retroplatform_guest_info, NULL);
}

void retroplatform_wm_close(void)
{
    RPSendMessage(RP_IPC_TO_HOST_CLOSE, 0, 0, NULL, 0, &retroplatform_guest_info, NULL);
}

static BYTE CALLBACK set_cursor_pos_mem_bank_read(int bank, WORD addr, LPARAM lParam)
{
	monitor_interface_t *mi = (monitor_interface_t *)lParam;
	return mi ? mi->mem_bank_read(bank, addr, mi->context) : 0;
}

static void CALLBACK set_cursor_pos_mem_bank_write(int bank, WORD addr, BYTE byte, LPARAM lParam)
{
	monitor_interface_t *mi = (monitor_interface_t *)lParam;
	if (mi)
		mi->mem_bank_write(bank, addr, byte, mi->context);
}

static void set_cursor_pos(WORD unused_addr, void *data) 
{
	monitor_interface_t *mi = maincpu_monitor_interface_get();
	if (mi && pfnRPSetCBMCursorPosition)
		pfnRPSetCBMCursorPosition(cursor_pos_system, cursor_pos.x, cursor_pos.y, set_cursor_pos_mem_bank_read, set_cursor_pos_mem_bank_write, (LPARAM)mi);
}

void retroplatform_lbuttondown(WPARAM wParam, LPARAM lParam)
{
	int scaled_width, scaled_height, unscaled_width, unscaled_height, val, xp, yp, x0, x1, y0, y1;
	RECT rc;
	rectangle_t gfx_size, text_size;
	const video_canvas_t *c;

	if (window_clickactive_time[retroplatform_screen_index] &&
		(GetTickCount() - window_clickactive_time[retroplatform_screen_index]) <= 500) // ignore mouse click, when it gives back focus to our window
	{
		window_clickactive_time[retroplatform_screen_index] = 0;
		return;
	}
	if (!pfnRPSetCBMCursorPosition || !virtualmouse_enabled || ui_get_full_screen())
		return;

	c = video_canvas_for_hwnd(retroplatform_active_window);
	if (!c)
		return;

	GetClientRect(retroplatform_active_window, &rc);
	scaled_width = rc.right - rc.left;
	scaled_height = rc.bottom - rc.top;

	unscaled_width = MIN(c->draw_buffer->canvas_width, c->geometry->screen_size.width - c->viewport->first_x);
    unscaled_height = MIN(c->draw_buffer->canvas_height, c->viewport->last_line - c->viewport->first_line + 1);

	xp = (GET_X_LPARAM(lParam) * unscaled_width) / scaled_width;
	yp = (GET_Y_LPARAM(lParam) * unscaled_height) / scaled_height;

	switch (machine_class)
	{
		case VICE_MACHINE_PET:
			if (resources_get_int("VideoSize", &val) < 0) // VideoSize values: 40 (40 column mode) or 0 (80 column mode)
				val = 40;
			if (val == 40)
			{
				x0 = 32;
				y0 = 33;
				gfx_size.width = 320;
				gfx_size.height = 200;
				text_size.width = 40;
				text_size.height = 25;
			}
			else
			{
				x0 = 32;
				y0 = 20;
				gfx_size.width = 640;
				gfx_size.height = 224;
				text_size.width = 80;
				text_size.height = 25;
			}
			break;
		case VICE_MACHINE_CBM6x0:
			if (resources_get_int("ModelLine", &val) >= 0 && val == 0) // CBM 7x0
			{
				x0 = 32;
				y0 = 8;
				gfx_size.width = 640;
				gfx_size.height = 350;
			}
			else // CBM 6x0
			{
				x0 = 32;
				y0 = 33;
				gfx_size.width = 640;
				gfx_size.height = 200;
			}
			text_size.width = 80;
			text_size.height = 25;
			break;
		default:
			x0 = c->geometry->gfx_position.x - c->viewport->first_x;
			y0 = c->geometry->gfx_position.y - c->geometry->first_displayed_line;
			gfx_size = c->geometry->gfx_size;
			text_size = c->geometry->text_size;
			break;
	}
	x1 = x0 + gfx_size.width;
	y1 = y0 + gfx_size.height;

	if (xp >= x0 && yp >= y0 && xp < x1 && yp < y1)
	{
		cursor_pos.x = ((xp - x0) * text_size.width) / gfx_size.width;
		cursor_pos.y = ((yp - y0) * text_size.height) / gfx_size.height;

		switch (machine_class)
		{
			case VICE_MACHINE_VIC20:
				cursor_pos_system = RP_CURSORPOS_SYSTEM_VIC20;
				break;
			case VICE_MACHINE_C64:
			case VICE_MACHINE_C64DTV:
			case VICE_MACHINE_C64SC:
				cursor_pos_system = RP_CURSORPOS_SYSTEM_C64;
				break;
			case VICE_MACHINE_C128:
				cursor_pos_system = RP_CURSORPOS_SYSTEM_C128;
				break;
			case VICE_MACHINE_PLUS4:
				cursor_pos_system = RP_CURSORPOS_SYSTEM_PLUS4;
				break;
			case VICE_MACHINE_PET:
				cursor_pos_system = (resources_get_int("Basic1Chars", &val) >= 0 && val == 1) ? RP_CURSORPOS_SYSTEM_PET2001 : RP_CURSORPOS_SYSTEM_PET;
				break;
			case VICE_MACHINE_CBM5x0:
				cursor_pos_system = RP_CURSORPOS_SYSTEM_CBM5x0;
				break;
			case VICE_MACHINE_CBM6x0:
				cursor_pos_system = RP_CURSORPOS_SYSTEM_CBM6x0;
				break;
			default:
				cursor_pos_system = RP_CURSORPOS_SYSTEM_NONE;
				break;
		}
		if (cursor_pos_system != RP_CURSORPOS_SYSTEM_NONE)
			interrupt_maincpu_trigger_trap(set_cursor_pos, 0);
	}
}

void retroplatform_mouse_event(void *window, UINT uMessage, WPARAM wParam, LPARAM lParam)
{
	bool button = FALSE;

	switch (uMessage)
	{
        case WM_LBUTTONDOWN: button = TRUE; retroplatform_mouse_buttons_state |= 1;  break;
        case WM_LBUTTONUP:   button = TRUE; retroplatform_mouse_buttons_state &= ~1; break;
        case WM_RBUTTONDOWN: button = TRUE; retroplatform_mouse_buttons_state |= 2;  break;
		case WM_RBUTTONUP:   button = TRUE; retroplatform_mouse_buttons_state &= ~2; break;
        case WM_MBUTTONDOWN: button = TRUE; retroplatform_mouse_buttons_state |= 4;  break;
        case WM_MBUTTONUP:   button = TRUE; retroplatform_mouse_buttons_state &= ~4; break;

		case WM_MOUSEMOVE:
			RPPostMessage(RP_IPC_TO_HOST_MOUSEMOVE, 0, lParam, &retroplatform_guest_info);
			if (!mouseleave_requested)
			{
				TRACKMOUSEEVENT tme;
				tme.cbSize = sizeof(tme);
				tme.hwndTrack = (HWND)window;
				tme.dwFlags = TME_LEAVE;
				tme.dwHoverTime = HOVER_DEFAULT;
				_TrackMouseEvent(&tme);
				mouseleave_requested = TRUE;
			}
			break;

		case WM_MOUSELEAVE:
			mouseleave_requested = FALSE;
			RPPostMessage(RP_IPC_TO_HOST_MOUSEMOVE, 0, MAKELONG(-1,-1), &retroplatform_guest_info);
			break;
	}
	if (button)
		RPPostMessage(RP_IPC_TO_HOST_MOUSEBUTTON, (WPARAM)retroplatform_mouse_buttons_state, lParam, &retroplatform_guest_info);
}
