/*
 * video-dx9.c - Video using DirectX9 for Win32
 *
 * Written by
 *  Andreas Matthies <andreas.matthies@gmx.net>
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See README for copyright notice.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307  USA.
 *
 */

#include "vice.h"

#include "fullscrn.h"
#include "lib.h"
#include "log.h"
#include "resources.h"
#include "ui.h"
#include "video.h"
#include "videoarch.h"
#include "viewport.h"
#include "vsync.h"
#include "vsyncapi.h"
#include "RetroPlatform.h"

#ifdef HAVE_D3D9_H

#include <d3d9.h>

typedef IDirect3D9 *(WINAPI* Direct3DCreate9_t)(UINT SDKVersion);

static Direct3DCreate9_t DynDirect3DCreate9;
static HINSTANCE d3d9dll = NULL;
static HINSTANCE d3dx9dll = NULL;

static D3DTEXTUREFILTERTYPE d3dpreffilter;

LPDIRECT3D9 d3d;

#ifndef D3D_SDK_VERSION
#define D3D_SDK_VERSION 31
#endif

static volatile LONG in_video_canvas_refresh_dx9 = 0;

static LPD3DXSPRITE sprite = NULL;
struct dx9_overlay
{
	unsigned long index;
	IDirect3DTexture9 *texture;
	int w;
	int h;
	int x;
	int y;
	struct dx9_overlay *next;
};
static struct dx9_overlay *dx9_overlays = NULL;


int video_setup_dx9(void)
{
    d3d9dll = LoadLibrary(TEXT("d3d9.dll"));
    if (d3d9dll == NULL) {
        return -1;
    }
    DynDirect3DCreate9 = (Direct3DCreate9_t)GetProcAddress(d3d9dll, "Direct3DCreate9");
    if (!DynDirect3DCreate9) {
        return -1;
    }
    d3d = DynDirect3DCreate9(D3D_SDK_VERSION);

    d3dx9dll = LoadLibrary(TEXT("D3DX9_43.dll"));

    return 0;
}

void video_shutdown_dx9(void)
{
    if (d3d != NULL) {
        IDirect3D9_Release(d3d);
        d3d = NULL;
    }
}

int video_device_create_dx9(video_canvas_t *canvas, int fullscreen)
{
    int device = D3DADAPTER_DEFAULT;

    ZeroMemory(&canvas->d3dpp, sizeof(canvas->d3dpp));
    canvas->d3dpp.BackBufferFormat = D3DFMT_X8R8G8B8;
    canvas->d3dpp.BackBufferCount = 1;
    canvas->d3dpp.MultiSampleType = D3DMULTISAMPLE_NONE;
    canvas->d3dpp.SwapEffect = D3DSWAPEFFECT_FLIP;
    canvas->d3dpp.Flags = 0;
    canvas->d3dpp.EnableAutoDepthStencil = FALSE;
    canvas->d3dpp.FullScreen_RefreshRateInHz = D3DPRESENT_RATE_DEFAULT;
    if (dx_primary_surface_rendering) {
        canvas->d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    } else {
        canvas->d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;
    }

    if (fullscreen) {
        int width, height, bitdepth, refreshrate;
        int keep_aspect_ratio, true_aspect_ratio, aspect_ratio;
        int shrinked_width, shrinked_height;
        double canvas_aspect_ratio;
  
        GetCurrentModeParameters(&device, &width, &height, &bitdepth, &refreshrate);

        resources_get_int("KeepAspectRatio", &keep_aspect_ratio);
        if (keep_aspect_ratio == 0) {
            canvas->dest_rect_ptr = NULL;
        } else {
            canvas->dest_rect_ptr = &canvas->dest_rect;
            resources_get_int("TrueAspectRatio", &true_aspect_ratio);
            if (true_aspect_ratio) {
                aspect_ratio = (int)(canvas->geometry->pixel_aspect_ratio * 1000);
            } else {
                resources_get_int("AspectRatio", &aspect_ratio);
            }
            canvas_aspect_ratio = aspect_ratio / 1000.0 * canvas->draw_buffer->canvas_physical_width / canvas->draw_buffer->canvas_physical_height;
            canvas_aspect_ratio = aspect_ratio / 1000.0 
                                    * canvas->draw_buffer->canvas_physical_width / canvas->draw_buffer->canvas_physical_height;
            if (canvas_aspect_ratio < (double) width / height) {
                shrinked_width = (int)(height * canvas_aspect_ratio);
                canvas->dest_rect.top = 0;
                canvas->dest_rect.bottom = height - 1;
                canvas->dest_rect.left = (width - shrinked_width) / 2;
                canvas->dest_rect.right = canvas->dest_rect.left + shrinked_width - 1;
            } else {
                shrinked_height = (int)(width / canvas_aspect_ratio);
                canvas->dest_rect.left = 0;
                canvas->dest_rect.right = width - 1;
                canvas->dest_rect.top = (height - shrinked_height) / 2;
                canvas->dest_rect.bottom = canvas->dest_rect.top + shrinked_height - 1;
            }
        }
        canvas->d3dpp.Windowed = FALSE;
    } else {
        canvas->dest_rect_ptr = NULL;
        canvas->d3dpp.Windowed = TRUE;
    }

    d3dpreffilter = retroplatform_guest_mode ? D3DTEXF_POINT : D3DTEXF_LINEAR;

    return 0;
}

video_canvas_t *video_canvas_create_dx9(video_canvas_t *canvas, unsigned int *width, unsigned int *height)
{
    ui_make_resizable(canvas, 1);

    canvas->depth = 32;

    if  (video_device_create_dx9(canvas, 0) != 0) {
        return NULL;
    }

    if (video_set_physical_colors(canvas) < 0) {
        return NULL;
    }

    video_canvas_add(canvas);

    return canvas;
}

void video_device_release_dx9(video_canvas_t *canvas)
{
    if (canvas->d3dsurface) {
        IDirect3DSurface9_Release(canvas->d3dsurface);
        canvas->d3dsurface = NULL;
    }
    if (canvas->d3ddev) { 
        IDirect3DDevice9_Release(canvas->d3ddev);
        canvas->d3ddev = NULL;
    }
}

HRESULT video_canvas_reset_dx9(video_canvas_t *canvas)
{
    LPDIRECT3DSWAPCHAIN9 d3dsc;
    HRESULT ddresult;
   int device = D3DADAPTER_DEFAULT;

    if (((canvas->d3dsurface != NULL) &&
        (S_OK != IDirect3DSurface9_Release(canvas->d3dsurface)))
        || ((canvas->d3ddev != NULL) &&
        ((S_OK != IDirect3DDevice9_GetSwapChain(canvas->d3ddev, 0, &d3dsc))
        || (S_OK != IDirect3DSwapChain9_Release(d3dsc)))
        )) {
        log_debug("video_dx9: Failed to release the DirectX9 device resources!");
    }

    canvas->d3dsurface = NULL;

    if (canvas->d3dpp.Windowed == 0) {
        int width, height, bitdepth, refreshrate;

        GetCurrentModeParameters(&device, &width, &height, &bitdepth, &refreshrate);
        canvas->d3dpp.BackBufferWidth = width;
        canvas->d3dpp.BackBufferHeight = height;
        if (refreshrate) {
            canvas->d3dpp.FullScreen_RefreshRateInHz = refreshrate;
        }
    } else {
        RECT wrect;

        GetClientRect(canvas->render_hwnd, &wrect);
        canvas->d3dpp.BackBufferWidth = wrect.right - wrect.left;
        canvas->d3dpp.BackBufferHeight = wrect.bottom - wrect.top;
    }

    if (dx_primary_surface_rendering) {
        canvas->d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    } else {
        canvas->d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;
    }

    if (canvas->d3ddev == NULL) {
        if (S_OK != (ddresult = IDirect3D9_CreateDevice(d3d, device, D3DDEVTYPE_HAL, canvas->render_hwnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &canvas->d3dpp, &canvas->d3ddev))) {
			if (ddresult != D3DERR_DEVICELOST) // do not log D3DERR_DEVICELOST: it's normal after Alt-Tab
	            log_debug("video_dx9: Failed to create the DirectX9 device! (error 0x%X)", (unsigned int)ddresult);
            return ddresult;
        }
    }
    else {
        if (S_OK != (ddresult = IDirect3DDevice9_Reset(
                                canvas->d3ddev,
                                &canvas->d3dpp))) {
			if (ddresult != D3DERR_DEVICELOST) // do not log D3DERR_DEVICELOST: it's normal after Alt-Tab
	            log_debug("video_dx9: Failed to reset the DirectX9 device! (error 0x%X)", (unsigned int)ddresult);
            return ddresult;
        }
    }
    
    if (S_OK != (ddresult = IDirect3DDevice9_CreateOffscreenPlainSurface(
                                canvas->d3ddev, canvas->draw_buffer->canvas_physical_width, canvas->draw_buffer->canvas_physical_height,
                                D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT,
                                &canvas->d3dsurface, NULL)))
    {
        log_debug("video_dx9: Failed to create new offscreen surface! (error 0x%X)", (unsigned int)ddresult);
        return ddresult;
    }

    return IDirect3DDevice9_TestCooperativeLevel(canvas->d3ddev);
}

static HRESULT video_canvas_prepare_for_update(video_canvas_t *canvas)
{
    HRESULT coopresult;

    if (canvas->d3ddev == NULL || canvas->d3dsurface == NULL) {
        return -1;
    }

    coopresult = IDirect3DDevice9_TestCooperativeLevel(canvas->d3ddev);
    if (coopresult == D3DERR_DEVICENOTRESET) {
        coopresult = video_canvas_reset_dx9(canvas);
    }
    return coopresult;
}

static int do_video_canvas_refresh_dx9(video_canvas_t *canvas, unsigned int xs, unsigned int ys, unsigned int xi, unsigned int yi, unsigned int w, unsigned int h)
{
    HRESULT stretchresult;
    LPDIRECT3DSURFACE9 d3dbackbuffer = NULL;
    D3DLOCKED_RECT lockedrect;
    HRESULT ddresult;
	D3DXVECTOR3 v;
	struct dx9_overlay *p;
#ifdef VSYNC_DEBUG
    unsigned long t1, t2, t3, t4;

    t1 = vsyncarch_gettime();
#endif

    xi *= canvas->videoconfig->scalex;
    w *= canvas->videoconfig->scalex;

    yi *= canvas->videoconfig->scaley;
    h *= canvas->videoconfig->scaley;

    if (S_OK != video_canvas_prepare_for_update(canvas)) {
        return -1;
    }

	if (S_OK != (ddresult = IDirect3DDevice9_Clear(canvas->d3ddev, 0, NULL, D3DCLEAR_TARGET, 0, 0, 0))) {
        log_debug("video_dx9: Failed to prepare for rendering (Clear error 0x%X)", (unsigned int)ddresult);
        return -1;
	}
	if (S_OK != (ddresult = IDirect3DDevice9_BeginScene(canvas->d3ddev))) {
        log_debug("video_dx9: Failed to prepare for rendering (BeginScene error 0x%X)", (unsigned int)ddresult);
        return -1;
	}
	if (S_OK != (ddresult = IDirect3DDevice9_GetBackBuffer(canvas->d3ddev, 0, 0, D3DBACKBUFFER_TYPE_MONO, &d3dbackbuffer))) {
        log_debug("video_dx9: Failed to prepare for rendering (GetBackBuffer error 0x%X)", (unsigned int)ddresult);
        return -1;
	}
	if (S_OK != (ddresult = IDirect3DSurface9_LockRect(canvas->d3dsurface, &lockedrect, NULL, 0))) {
        log_debug("video_dx9: Failed to prepare for rendering (LockRect error 0x%X)", (unsigned int)ddresult);
        return -1;
    }

    video_canvas_render(canvas, lockedrect.pBits, w, h, xs, ys, xi, yi, lockedrect.Pitch, 32);

    if (S_OK != (ddresult = IDirect3DSurface9_UnlockRect(canvas->d3dsurface))) {
        log_debug("video_dx9: Failed to unlock surface (error 0x%X)", (unsigned int)ddresult);
        return -1;
    }

#ifdef VSYNC_DEBUG
    t2 = vsyncarch_gettime();
#endif

    do {
        stretchresult = IDirect3DDevice9_StretchRect(canvas->d3ddev, canvas->d3dsurface, NULL, d3dbackbuffer, canvas->dest_rect_ptr, d3dpreffilter);

        if (d3dpreffilter == D3DTEXF_NONE) {
            break;
        }

        if (stretchresult != S_OK) {
            /* Some adapters don't support filtering */
            d3dpreffilter = D3DTEXF_NONE;
            log_debug("video_dx9: Disabled StretchRect filtering!");
        }

    } while (stretchresult != S_OK);
    if (stretchresult != S_OK) {
        log_debug("video_dx9: StretchRect failed even without filtering!");
    }

	if (sprite && dx9_overlays)
	{
		ID3DXSprite_Begin(sprite, D3DXSPRITE_ALPHABLEND);
		for (p = dx9_overlays; p != NULL; p = p->next)
		{
			v.x = p->x;
			v.y = p->y;
			v.z = 0;
			ID3DXSprite_Draw(sprite, p->texture, NULL, NULL, &v, 0xFFFFFFFF);
		}
		ID3DXSprite_End(sprite);
	}

    if (S_OK != (ddresult = IDirect3DSurface9_Release(d3dbackbuffer)) 
        || S_OK != (ddresult = IDirect3DDevice9_EndScene(canvas->d3ddev)))
    {
        log_debug("video_dx9: EndScene failed! (error 0x%X)", (unsigned int)ddresult);
        return -1;
    }

#ifdef VSYNC_DEBUG
    t3 = vsyncarch_gettime();
#endif

    if (S_OK != (ddresult = IDirect3DDevice9_Present(canvas->d3ddev, NULL, NULL, NULL, NULL))) {
        log_debug("video_dx9: Refresh failed to present the scene! (error 0x%X)", (unsigned int)ddresult);
        return -1;
    }

#ifdef VSYNC_DEBUG
    t4 = vsyncarch_gettime();
    log_debug("canvas-refresh: render:%lu  stretch:%lu  present:%lu  total:%lu",
                t2-t1, t3-t2, t4-t3, t4-t1);
#endif

    return 0;
}

int video_canvas_refresh_dx9(video_canvas_t *canvas, unsigned int xs, unsigned int ys, unsigned int xi, unsigned int yi, unsigned int w, unsigned int h)
{
	int r;

	if (InterlockedExchange(&in_video_canvas_refresh_dx9, 1) != 0) // shield from concurrent calls (multi-thread safe) or recurrent calls
		return -1;

	r = do_video_canvas_refresh_dx9(canvas, xs, ys, xi, yi, w, h);

	in_video_canvas_refresh_dx9 = 0;

	return r;
}

void video_canvas_update_dx9(HWND hwnd, HDC hdc, int xclient, int yclient, int w, int h)
{
    video_canvas_t *canvas;

    canvas = video_canvas_for_hwnd(hwnd);
    if (canvas == NULL) {
        return;
    }
    
    /* Just refresh the whole canvas */
    video_canvas_refresh_all(canvas);
}

int video_canvas_overlay_create_dx9(video_canvas_t *canvas, unsigned long index, int w, int h, int x, int y, const unsigned char *data)
{
	typedef HRESULT (WINAPI *PFN_D3DXCreateSprite)(LPDIRECT3DDEVICE9, LPD3DXSPRITE *);
	PFN_D3DXCreateSprite pfnD3DXCreateSprite;
	struct dx9_overlay **prev, *next, *p;
	D3DLOCKED_RECT r;
	unsigned char *to;
	HRESULT hr;
	int cy, m;
	bool reuse;

	if (!canvas || w <= 0 || h <= 0 || !data)
		return -1;
	if (!sprite)
	{
		if (!d3dx9dll)
		{
			log_debug("video_dx9: Sprite object cannot be created. (no d3dx9 dll)");
			return -1;
		}
		pfnD3DXCreateSprite = (PFN_D3DXCreateSprite)GetProcAddress(d3dx9dll, "D3DXCreateSprite");
		if (!pfnD3DXCreateSprite)
		{
			log_debug("video_dx9: Sprite object cannot be created. (D3DXCreateSprite not found in d3dx9 dll)");
			return -1;
		}
		hr = pfnD3DXCreateSprite(canvas->d3ddev, &sprite);
		if (FAILED(hr))
		{
			log_debug("video_dx9: Failed to create sprite object. (error 0x%X)", (unsigned int)hr);
			return -1;
		}
	}
	reuse = FALSE;
	prev = &dx9_overlays;
	for (p = dx9_overlays; p != NULL; p = p->next)
	{
		if (p->index == index)
		{
			if ((p->w != w || p->h != h) && p->texture)
			{
				IDirect3DTexture9_Release(p->texture);
				p->texture = NULL;
			}
			reuse = TRUE;
			break;
		}
		if (p->index > index)
			break;
		prev = &p->next;
	}
	if (!reuse)
	{
		p = (struct dx9_overlay *)GlobalAlloc(GMEM_FIXED, sizeof(struct dx9_overlay));
		if (!p)
			return -1;
		memset(p, 0, sizeof(struct dx9_overlay));
		p->index = index;
	}
	p->w = w;
	p->h = h;
	p->x = x;
	p->y = y;
	if (!p->texture)
	{
		hr = IDirect3DDevice9_CreateTexture(canvas->d3ddev, w, h, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &p->texture, NULL);
		if (FAILED(hr))
			hr = IDirect3DDevice9_CreateTexture(canvas->d3ddev, w, h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &p->texture, NULL);
		if (FAILED(hr) || !p->texture)
		{
			log_debug("video_dx9: Failed to create texture. (error 0x%X)", (unsigned int)hr);
			GlobalFree(p);
			return -1;
		}
	}
	hr = IDirect3DTexture9_LockRect(p->texture, 0, &r, NULL, 0);
	if (FAILED(hr))
	{
		log_debug("video_dx9: Failed to lock texture. (error 0x%X)", (unsigned int)hr);
		IDirect3DTexture9_Release(p->texture);
		GlobalFree(p);
		return -1;
	}
	m = w * 4;
	if (r.Pitch < m)
	{
		log_debug("video_dx9: Unexpected texture data. (width %d, pitch %d < %d)", w, r.Pitch, m);
		IDirect3DTexture9_UnlockRect(p->texture, 0);
		IDirect3DTexture9_Release(p->texture);
		GlobalFree(p);
		return -1;
	}
	if (r.Pitch == m)
		memcpy(r.pBits, data, m * h);
	else
	{
		to = (unsigned char *)r.pBits;
		for (cy = 0; cy < h; cy++)
		{
			memcpy(to, data, m);
			to += r.Pitch;
			data += m;
		}
	}
	IDirect3DTexture9_UnlockRect(p->texture, 0);
	if (!reuse)
	{
		next = *prev;
		*prev = p;
		p->next = next;
	}
	return 0;
}

int video_canvas_overlay_delete_dx9(video_canvas_t *canvas, unsigned long index)
{
	struct dx9_overlay **prev, *p;
	int rc;

	rc = -1;
	prev = &dx9_overlays;
	for (p = dx9_overlays; p != NULL; p = p->next)
	{
		if (p->index == index)
		{
			*prev = p->next;
			if (p->texture)
				IDirect3DTexture9_Release(p->texture);
			GlobalFree(p);
			rc = 0;
			break;
		}
		prev = &p->next;
	}
	if (rc < 0)
		return rc;
	if (sprite && !dx9_overlays)
	{
		ID3DXSprite_Release(sprite);
		sprite = NULL;
	}
	return 0;
}

int video_canvas_overlay_move_dx9(video_canvas_t *canvas, unsigned long index, int x, int y)
{
	struct dx9_overlay *p;

	for (p = dx9_overlays; p != NULL; p = p->next)
	{
		if (p->index == index)
		{
			p->x = x;
			p->y = y;
			return 0;
		}
	}
	return -1;
}

void video_canvas_overlays_delete_dx9(video_canvas_t *canvas)
{
	struct dx9_overlay *next, *p;

	for (p = dx9_overlays; p != NULL; p = next)
	{
		next = p->next;
		if (p->texture)
			IDirect3DTexture9_Release(p->texture);
		GlobalFree(p);
	}
	dx9_overlays = NULL;
	if (sprite)
	{
		ID3DXSprite_Release(sprite);
		sprite = NULL;
	}
}

#else

/* some dummies for compilation without DirectX9 header */

void video_shutdown_dx9(void)
{
}

int video_setup_dx9(void)
{
    return -1;
}

video_canvas_t *video_canvas_create_dx9(video_canvas_t *canvas, unsigned int *width, unsigned int *height)
{
    return NULL;
}

void video_device_release_dx9(video_canvas_t *canvas)
{
}

HRESULT video_canvas_reset_dx9(video_canvas_t *canvas)
{
    return -1;
}

int video_canvas_refresh_dx9(video_canvas_t *canvas, unsigned int xs, unsigned int ys, unsigned int xi, unsigned int yi, unsigned int w, unsigned int h)
{
    return -1;
}

void video_canvas_update_dx9(HWND hwnd, HDC hdc, int xclient, int yclient, int w, int h)
{
}

int video_canvas_overlay_create_dx9(video_canvas_t *canvas, unsigned long index, int w, int h, int x, int y, const unsigned char *data)
{
	return -1;
}

int video_canvas_overlay_delete_dx9(video_canvas_t *canvas, unsigned long index)
{
	return -1;
}

int video_canvas_overlay_move_dx9(video_canvas_t *canvas, unsigned long index, int x, int y)
{
	return -1;
}

void video_canvas_overlays_delete_dx9(video_canvas_t *canvas)
{
}

#endif
