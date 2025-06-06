/*
 * Direct3D wine internal interface main
 *
 * Copyright 2002-2003 The wine-d3d team
 * Copyright 2002-2003 Raphael Junqueira
 * Copyright 2004      Jason Edmeades
 * Copyright 2007-2008 Stefan Dösinger for CodeWeavers
 * Copyright 2009 Henri Verbeet for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "config.h"
#include "wine/port.h"

#include "initguid.h"
#include "wined3d_private.h"

#include "vmsetup.h"

#include <stdio.h>

#ifdef HAVE_CRTEX
#include <lockex.h>
#endif


WINE_DEFAULT_DEBUG_CHANNEL(d3d);
WINE_DECLARE_DEBUG_CHANNEL(winediag);

struct wined3d_wndproc
{
    HWND window;
    BOOL unicode;
    WNDPROC proc;
    struct wined3d_device *device;
};

struct wined3d_wndproc_table
{
    struct wined3d_wndproc *entries;
    unsigned int count;
    unsigned int size;
};

#define DLL_VALID_VALUE 42

static unsigned int DLLValid = 0;


#ifdef USE_HOOKS
HHOOK wine_hook = NULL;
HHOOK wine_hook_pos = NULL;
#endif

static struct wined3d_wndproc_table wndproc_table;

static CRITICAL_SECTION wined3d_cs;
static CRITICAL_SECTION wined3d_wndproc_cs;

#if 0
static CRITICAL_SECTION_DEBUG wined3d_cs_debug =
{
    0, 0, &wined3d_cs,
    {&wined3d_cs_debug.ProcessLocksList,
    &wined3d_cs_debug.ProcessLocksList},
    0, 0, {(DWORD_PTR)(__FILE__ ": wined3d_cs")}
};
static CRITICAL_SECTION wined3d_cs = {&wined3d_cs_debug, -1, 0, 0, 0, 0};


static CRITICAL_SECTION_DEBUG wined3d_wndproc_cs_debug =
{
    0, 0, &wined3d_wndproc_cs,
    {&wined3d_wndproc_cs_debug.ProcessLocksList,
    &wined3d_wndproc_cs_debug.ProcessLocksList},
    0, 0, {(DWORD_PTR)(__FILE__ ": wined3d_wndproc_cs")}
};
static CRITICAL_SECTION wined3d_wndproc_cs = {&wined3d_wndproc_cs_debug, -1, 0, 0, 0, 0};
#endif

/* When updating default value here, make sure to update winecfg as well,
 * where appropriate. */
struct wined3d_settings wined3d_settings =
{
    MAKEDWORD_VERSION(1, 0), /* Default to legacy OpenGL */
    TRUE,           /* Use of GLSL enabled by default */
    ORM_FBO,        /* Use FBOs to do offscreen rendering (alt. ORM_BACKBUFFER) */
    PCI_VENDOR_NONE,/* PCI Vendor ID */
    PCI_DEVICE_NONE,/* PCI Device ID */
    0,              /* The default of memory is set in init_driver_info */
    NULL,           /* No wine logo by default */
    TRUE,           /* Multisampling enabled by default. */
    FALSE,          /* No strict draw ordering. */
    TRUE,           /* Don't try to render onscreen by default. */
    ~0U,            /* No VS shader model limit by default. */
    ~0U,            /* No GS shader model limit by default. */
    ~0U,            /* No PS shader model limit by default. */
    FALSE,          /* 3D support enabled by default. */
    FALSE,          /* allway 32bit rendering disabled by default */
    FALSE,          /* VERTEX_ARRAY_BRGA is OK on most cases */
    FALSE,          /* CheckFloatConstants disabled by default */
    FALSE,          /* system cursor is visible or hidden by application */
};

struct wined3d * CDECL wined3d_create(DWORD flags)
{
    struct wined3d *object;
    HRESULT hr;

    object = calloc(1, FIELD_OFFSET(struct wined3d, adapters[1]));
    if (!object)
    {
        ERR("Failed to allocate wined3d object memory.\n");
        return NULL;
    }

    if (wined3d_settings.no_3d)
        flags |= WINED3D_NO3D;

    hr = wined3d_init(object, flags);
    if (FAILED(hr))
    {
        WARN("Failed to initialize wined3d object, hr %#x.\n", hr);
        free(object);
        return NULL;
    }

    TRACE("Created wined3d object %p.\n", object);

    return object;
}

static DWORD get_config_key(HKEY defkey, HKEY appkey, const char *name, char *buffer, DWORD size)
{
    if (appkey && !RegQueryValueExA(appkey, name, 0, NULL, (BYTE *)buffer, &size)) return 0;
    if (defkey && !RegQueryValueExA(defkey, name, 0, NULL, (BYTE *)buffer, &size)) return 0;
    return ERROR_FILE_NOT_FOUND;
}

static DWORD get_config_key_dword(HKEY defkey, HKEY appkey, const char *name, DWORD *data)
{
    DWORD type;
    DWORD size = sizeof(DWORD);
    if (appkey && !RegQueryValueExA(appkey, name, 0, &type, (BYTE *)data, &size) && (type == REG_DWORD)) return 0;
    if (defkey && !RegQueryValueExA(defkey, name, 0, &type, (BYTE *)data, &size) && (type == REG_DWORD)) return 0;
    return ERROR_FILE_NOT_FOUND;
}

static HINSTANCE act_hInstDLL = NULL;

HINSTANCE wined3d_gethInstDLL()
{
	return act_hInstDLL;
}

static DEVMODEA init_display_state;

static void wined3d_save_display_state()
{
	DEVMODEA current;
	memset(&init_display_state, 0, sizeof(init_display_state));
	memset(&current, 0, sizeof(DEVMODEA));
	init_display_state.dmSize = sizeof(DEVMODEA);
	current.dmSize = sizeof(DEVMODEA);
	BOOL rc;
	
	rc = EnumDisplaySettingsA95(NULL, ENUM_REGISTRY_SETTINGS, &init_display_state);
	if(rc)
	{
		rc = EnumDisplaySettingsA95(NULL, ENUM_CURRENT_SETTINGS, &init_display_state);
	}
	
	if(!rc)
	{
		init_display_state.dmPelsWidth = 0;
		init_display_state.dmPelsHeight = 0;
	}
	
	if(current.dmPelsWidth != init_display_state.dmPelsWidth || 
		current.dmPelsHeight != init_display_state.dmPelsHeight ||
		current.dmBitsPerPel != init_display_state.dmBitsPerPel
		)
	{
		/* when mode is different to registry settings, we don't want to restore it! */
		init_display_state.dmPelsWidth = 0;
		init_display_state.dmPelsHeight = 0;
	}
}

static void wined3d_restore_display_state()
{
	if(init_display_state.dmPelsWidth != 0 && init_display_state.dmPelsHeight != 0)
	{
		DEVMODEA act_display_state;
		
		if(EnumDisplaySettingsA95(NULL, ENUM_CURRENT_SETTINGS, &act_display_state))
		{
			if(act_display_state.dmPelsWidth == init_display_state.dmPelsWidth &&
				act_display_state.dmPelsHeight == init_display_state.dmPelsHeight &&
				act_display_state.dmBitsPerPel == init_display_state.dmBitsPerPel
				)
			{
				/* mode match */
				return;
			}
		}
		
		ChangeDisplaySettingsExA95(NULL, &init_display_state, NULL, CDS_FULLSCREEN, NULL);	
	}
}

static LRESULT CALLBACK wine_hook_proc(int nCode, WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK wine_hook_proc_pos(int nCode, WPARAM wParam, LPARAM lParam);

static BOOL wined3d_dll_init(HINSTANCE hInstDLL)
{
    DWORD wined3d_context_tls_idx;
    char buffer[MAX_PATH+10];
    DWORD size = sizeof(buffer);
    HKEY hkey = 0;
    HKEY appkey = 0;
    DWORD len, tmpvalue;
    WNDCLASSA wc;
    char *ptr;
    
    wined3d_save_display_state();

    wined3d_context_tls_idx = TlsAlloc();
    if (wined3d_context_tls_idx == TLS_OUT_OF_INDEXES)
    {
        DWORD err = GetLastError();
        ERR("Failed to allocate context TLS index, err %#x.\n", err);
        return FALSE;
    }
    context_set_tls_idx(wined3d_context_tls_idx);

    /* We need our own window class for a fake window which we use to retrieve GL capabilities */
    /* We might need CS_OWNDC in the future if we notice strange things on Windows.
     * Various articles/posts about OpenGL problems on Windows recommend this. */
    wc.style                = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc          = DefWindowProcA;
    wc.cbClsExtra           = 0;
    wc.cbWndExtra           = 0;
    wc.hInstance            = hInstDLL;
    wc.hIcon                = LoadIconA(NULL, (const char *)IDI_WINLOGO);
    wc.hCursor              = LoadCursorA(NULL, (const char *)IDC_ARROW);
    wc.hbrBackground        = NULL;
    wc.lpszMenuName         = NULL;
    wc.lpszClassName        = WINED3D_OPENGL_WINDOW_CLASS_NAME;

    if (!RegisterClassA(&wc))
    {
        ERR("Failed to register window class 'WineD3D_OpenGL'!\n");
        if (!TlsFree(wined3d_context_tls_idx))
        {
            DWORD err = GetLastError();
            ERR("Failed to free context TLS index, err %#x.\n", err);
        }
        return FALSE;
    }
    
    act_hInstDLL = hInstDLL;

    DisableThreadLibraryCalls(hInstDLL);

#ifdef USE_HOOKS
    wine_hook     = SetWindowsHookExA(WH_CALLWNDPROC, wine_hook_proc, NULL, GetCurrentThreadId());
#endif
    
	    if ( RegOpenKeyA(HKEY_CURRENT_USER, "Software\\Wine\\Direct3D", &hkey ) != ERROR_SUCCESS ) hkey = 0;
	
	    len = GetModuleFileNameA( 0, buffer, MAX_PATH );
	    if (len && len < MAX_PATH)
	    {
	        HKEY tmpkey;
	        LSTATUS rc;
	        /* @@ Wine registry key: HKCU\Software\Wine\AppDefaults\app.exe\Direct3D */
	        /* OR Wine9x key: HKLM\Software\Wine\app.exe */
	        rc = RegOpenKeyA( HKEY_CURRENT_USER, "Software\\Wine\\AppDefaults", &tmpkey );
	        
	        if (rc == ERROR_SUCCESS)
	        {
	            char *p, *appname = buffer;
	            if ((p = strrchr( appname, '/' ))) appname = p + 1;
	            if ((p = strrchr( appname, '\\' ))) appname = p + 1;
	            	strcat( appname, "\\Direct3D" );
	            
	            TRACE("appname = [%s]\n", appname);
	            if (RegOpenKeyA( tmpkey, appname, &appkey )) appkey = 0;
	            RegCloseKey( tmpkey );
	        }
	    }
	
	    if (hkey || appkey)
	    {
	        if (!get_config_key_dword(hkey, appkey, "MaxVersionGL", &tmpvalue))
	        {
	            if (tmpvalue != wined3d_settings.max_gl_version)
	            {
	                ERR_(winediag)("Setting maximum allowed wined3d GL version to %u.%u.\n",
	                        tmpvalue >> 16, tmpvalue & 0xffff);
	                wined3d_settings.max_gl_version = tmpvalue;
	            }
	        }
	        if ( !get_config_key( hkey, appkey, "UseGLSL", buffer, size) )
	        {
	            if (!strcmp(buffer,"disabled"))
	            {
	                ERR_(winediag)("The GLSL shader backend has been disabled. You get to keep all the pieces if it breaks.\n");
	                TRACE("Use of GL Shading Language disabled\n");
	                wined3d_settings.glslRequested = FALSE;
	            }
	        }
	        if ( !get_config_key( hkey, appkey, "OffscreenRenderingMode", buffer, size) )
	        {
	            if (!strcmp(buffer,"backbuffer"))
	            {
	                TRACE("Using the backbuffer for offscreen rendering\n");
	                wined3d_settings.offscreen_rendering_mode = ORM_BACKBUFFER;
	            }
	            else if (!strcmp(buffer,"fbo"))
	            {
	                TRACE("Using FBOs for offscreen rendering\n");
	                wined3d_settings.offscreen_rendering_mode = ORM_FBO;
	            }
	        }
	        if ( !get_config_key_dword( hkey, appkey, "VideoPciDeviceID", &tmpvalue) )
	        {
	            int pci_device_id = tmpvalue;
	
	            /* A pci device id is 16-bit */
	            if(pci_device_id > 0xffff)
	            {
	                ERR("Invalid value for VideoPciDeviceID. The value should be smaller or equal to 65535 or 0xffff\n");
	            }
	            else
	            {
	                TRACE("Using PCI Device ID %04x\n", pci_device_id);
	                wined3d_settings.pci_device_id = pci_device_id;
	            }
	        }
	        if ( !get_config_key_dword( hkey, appkey, "VideoPciVendorID", &tmpvalue) )
	        {
	            int pci_vendor_id = tmpvalue;
	
	            /* A pci device id is 16-bit */
	            if(pci_vendor_id > 0xffff)
	            {
	                ERR("Invalid value for VideoPciVendorID. The value should be smaller or equal to 65535 or 0xffff\n");
	            }
	            else
	            {
	                TRACE("Using PCI Vendor ID %04x\n", pci_vendor_id);
	                wined3d_settings.pci_vendor_id = pci_vendor_id;
	            }
	        }
	        if ( !get_config_key( hkey, appkey, "VideoMemorySize", buffer, size) )
	        {
	            int TmpVideoMemorySize = atoi(buffer);
	            if(TmpVideoMemorySize > 0)
	            {
	                wined3d_settings.emulated_textureram = (UINT64)TmpVideoMemorySize *1024*1024;
	                TRACE("Use %iMiB = 0x%s bytes for emulated_textureram\n",
	                        TmpVideoMemorySize,
	                        wine_dbgstr_longlong(wined3d_settings.emulated_textureram));
	            }
	            else
	                ERR("VideoMemorySize is %i but must be >0\n", TmpVideoMemorySize);
	        }
	        if ( !get_config_key( hkey, appkey, "WineLogo", buffer, size) )
	        {
	            size_t len = strlen(buffer) + 1;
	
	            wined3d_settings.logo = malloc(len);
	            if (!wined3d_settings.logo) ERR("Failed to allocate logo path memory.\n");
	            else memcpy(wined3d_settings.logo, buffer, len);
	        }
	        if ( !get_config_key( hkey, appkey, "Multisampling", buffer, size) )
	        {
	            if (!strcmp(buffer, "disabled"))
	            {
	                TRACE("Multisampling disabled.\n");
	                wined3d_settings.allow_multisampling = FALSE;
	            }
	        }
	        if (!get_config_key(hkey, appkey, "StrictDrawOrdering", buffer, size)
	                && !strcmp(buffer,"enabled"))
	        {
	            TRACE("Enforcing strict draw ordering.\n");
	            wined3d_settings.strict_draw_ordering = TRUE;
	        }
	        if (!get_config_key(hkey, appkey, "AlwaysOffscreen", buffer, size)
	                && !strcmp(buffer,"disabled"))
	        {
	            TRACE("Not always rendering backbuffers offscreen.\n");
	            wined3d_settings.always_offscreen = FALSE;
	        }
	        
          if (!get_config_key(hkey, appkey, "CheckFloatConstants", buffer, size)
                && !strcmp(buffer, "enabled"))
          {
              TRACE("Checking relative addressing indices in float constants.\n");
              wined3d_settings.check_float_constants = TRUE;
          }
	        
	        if (!get_config_key_dword(hkey, appkey, "MaxShaderModelVS", (DWORD*)&wined3d_settings.max_sm_vs))
	            TRACE("Limiting VS shader model to %u.\n", wined3d_settings.max_sm_vs);
	        if (!get_config_key_dword(hkey, appkey, "MaxShaderModelGS", (DWORD*)&wined3d_settings.max_sm_gs))
	            TRACE("Limiting GS shader model to %u.\n", wined3d_settings.max_sm_gs);
	        if (!get_config_key_dword(hkey, appkey, "MaxShaderModelPS", (DWORD*)&wined3d_settings.max_sm_ps))
	            TRACE("Limiting PS shader model to %u.\n", wined3d_settings.max_sm_ps);
	        if (!get_config_key(hkey, appkey, "DirectDrawRenderer", buffer, size)
	                && !strcmp(buffer, "gdi"))
	        {
	            TRACE("Disabling 3D support.\n");
	            wined3d_settings.no_3d = TRUE;
	        }
	        
          if (!get_config_key(hkey, appkey, "HideCursor", buffer, size))
          {
          		if(strcmp(buffer, "enabled") == 0 || atoi(buffer) >  0)
          		{
              	TRACE("Hidding system cursor.\n");
              	wined3d_settings.hide_sys_cursor = TRUE;
              }
          }
	        
	    }
	
	    if (appkey) RegCloseKey( appkey );
	    if (hkey) RegCloseKey( hkey );
	    	
	  /* vmsetup */
	  tmpvalue = vmhal_setup_dw("wine", "MaxVersionGL");
	  if(tmpvalue > 0)
	  {
	  	if(tmpvalue != wined3d_settings.max_gl_version)
	  	{
	  		wined3d_settings.max_gl_version = tmpvalue;
	  	}
	  }

	  if(strcmp(vmhal_setup_str("wine", "MaxVersionGL", TRUE), "disabled") == 0)
	  {
	  	wined3d_settings.glslRequested = FALSE;
	  }

	  if(strcmp(vmhal_setup_str("wine", "OffscreenRenderingMode", TRUE), "backbuffer") == 0)
	  {
	  	wined3d_settings.offscreen_rendering_mode = ORM_BACKBUFFER;
	  }

	  if(strcmp(vmhal_setup_str("wine", "OffscreenRenderingMode", TRUE), "fbo") == 0)
	  {
	  	wined3d_settings.offscreen_rendering_mode = ORM_FBO;
	  }
	  
	  if(vmhal_setup_str("wine", "VideoPciDeviceID", FALSE) != NULL)
	  {
	  	tmpvalue = vmhal_setup_dw("wine", "VideoPciDeviceID");
	  	if(tmpvalue < 0xffff)
	  	{
	  		wined3d_settings.pci_device_id = tmpvalue;
	  	}
	  }
	  
	  if(vmhal_setup_str("wine", "VideoPciVendorID", FALSE) != NULL)
	  {
	  	tmpvalue = vmhal_setup_dw("wine", "VideoPciVendorID");
	  	if(tmpvalue < 0xffff)
	  	{
	  		wined3d_settings.pci_vendor_id = tmpvalue;
	  	}
	  }

		ptr = vmhal_setup_str("wine", "WineLogo", FALSE);
	  if(ptr != NULL)
	  {
	  	size_t len = strlen(ptr) + 1;
			wined3d_settings.logo = malloc(len);
			if(!wined3d_settings.logo)
			{
				ERR("Failed to allocate logo path memory.\n");
			}
			else
			{
				memcpy(wined3d_settings.logo, ptr, len);
			}
	  }

	  if(strcmp(vmhal_setup_str("wine", "Multisampling", TRUE), "disabled") == 0)
	  {
	  	wined3d_settings.allow_multisampling = FALSE;
	  }
	  
	  if(strcmp(vmhal_setup_str("wine", "StrictDrawOrdering", TRUE), "enabled") == 0)
	  {
	  	wined3d_settings.strict_draw_ordering = TRUE;
	  }
	  
	  if(strcmp(vmhal_setup_str("wine", "AlwaysOffscreen", TRUE), "disabled") == 0)
	  {
	  	wined3d_settings.always_offscreen = TRUE;
	  }
	  
	  if(strcmp(vmhal_setup_str("wine", "CheckFloatConstants", TRUE), "enabled") == 0)
	  {
	  	wined3d_settings.check_float_constants = TRUE;
	  }

	  if(strcmp(vmhal_setup_str("wine", "DirectDrawRenderer", TRUE), "gdi") == 0)
	  {
	  	wined3d_settings.no_3d = TRUE;
	  }
	  
	  if(strcmp(vmhal_setup_str("wine", "HideCursor", TRUE), "enabled") == 0)
	  {
	  	wined3d_settings.hide_sys_cursor = TRUE;
	  }

	  if(vmhal_setup_str("wine", "MaxShaderModelVS", FALSE) != NULL)
	  {
	  	wined3d_settings.max_sm_vs = vmhal_setup_dw("wine", "MaxShaderModelVS");
	  }

	  if(vmhal_setup_str("wine", "MaxShaderModelGS", FALSE) != NULL)
	  {
	  	wined3d_settings.max_sm_gs = vmhal_setup_dw("wine", "MaxShaderModelGS");
	  }

	  if(vmhal_setup_str("wine", "MaxShaderModelPS", FALSE) != NULL)
	  {
	  	wined3d_settings.max_sm_ps = vmhal_setup_dw("wine", "MaxShaderModelPS");
	  }

#ifdef USE_HOOKS
    if(wined3d_settings.hide_sys_cursor)
    {
    	wine_hook_pos = SetWindowsHookExA(WH_CALLWNDPROCRET, wine_hook_proc_pos, NULL, GetCurrentThreadId());
    }
#endif
    
    /* windows 9x require to inicialize critical section */
    InitializeCriticalSection(&wined3d_wndproc_cs);
    InitializeCriticalSection(&wined3d_cs);
    
    DLLValid = DLL_VALID_VALUE;
    
		TRACE("wined3d_dll_init success\n");

    return TRUE;
}

static BOOL wined3d_dll_destroy(HINSTANCE hInstDLL)
{
    DWORD wined3d_context_tls_idx = context_get_tls_idx();
    unsigned int i;
    
#ifdef USE_HOOKS
    if(wine_hook)
    {
    	UnhookWindowsHookEx(wine_hook);
    	wine_hook = NULL;
    }
    if(wine_hook_pos)
    {
    	UnhookWindowsHookEx(wine_hook_pos);
    	wine_hook_pos = NULL;
    }
#endif

    if (!TlsFree(wined3d_context_tls_idx))
    {
        DWORD err = GetLastError();
        ERR("Failed to free context TLS index, err %#x.\n", err);
    }

    for (i = 0; i < wndproc_table.count; ++i)
    {
        /* Trying to unregister these would be futile. These entries can only
         * exist if either we skipped them in wined3d_unregister_window() due
         * to the application replacing the wndproc after the entry was
         * registered, or if the application still has an active wined3d
         * device. In the latter case the application has bigger problems than
         * these entries. */
        WARN("Leftover wndproc table entry %p.\n", &wndproc_table.entries[i]);
    }
    free(wndproc_table.entries);

    free(wined3d_settings.logo);
    UnregisterClassA(WINED3D_OPENGL_WINDOW_CLASS_NAME, hInstDLL);

    DeleteCriticalSection(&wined3d_wndproc_cs);
    DeleteCriticalSection(&wined3d_cs);

    return TRUE;
}

void wined3d_mutex_lock(void)
{
	TRACE("LOCK (from %p)\n", __builtin_return_address(0));
  EnterCriticalSection(&wined3d_cs);
}

void wined3d_mutex_unlock(void)
{
	TRACE("UNLOCK (from %p)\n", __builtin_return_address(0));
    LeaveCriticalSection(&wined3d_cs);
}

static void wined3d_wndproc_mutex_lock(void)
{
	TRACE("LOCK proc\n");
    EnterCriticalSection(&wined3d_wndproc_cs);
}

static void wined3d_wndproc_mutex_unlock(void)
{
	TRACE("UNLOCK proc\n");
    LeaveCriticalSection(&wined3d_wndproc_cs);
}

static struct wined3d_wndproc *wined3d_find_wndproc(HWND window)
{
    unsigned int i;

    for (i = 0; i < wndproc_table.count; ++i)
    {
        if (wndproc_table.entries[i].window == window)
        {
            return &wndproc_table.entries[i];
        }
    }

    return NULL;
}

#ifndef USE_HOOKS
static LRESULT CALLBACK wined3d_wndproc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    struct wined3d_wndproc *entry;
    struct wined3d_device *device;
    BOOL unicode;
    WNDPROC proc;
    
    if(DLLValid != DLL_VALID_VALUE) return DefWindowProcA(window, message, wparam, lparam);

    wined3d_wndproc_mutex_lock();
    entry = wined3d_find_wndproc(window);

    if (!entry)
    {
        wined3d_wndproc_mutex_unlock();
        ERR("Window %p is not registered with wined3d.\n", window);
        return DefWindowProcA(window, message, wparam, lparam);
    }
    
    device = entry->device;
    unicode = entry->unicode;
    proc = entry->proc;
    wined3d_wndproc_mutex_unlock();

    if (device)
        return device_process_message(device, window, unicode, message, wparam, lparam, proc);

    if (proc == NULL || proc == wined3d_wndproc)
    	return DefWindowProc(window, message, wparam, lparam); 

    if (unicode)
        return CallWindowProcW(proc, window, message, wparam, lparam);
    return CallWindowProcA(proc, window, message, wparam, lparam);
}
#endif

#ifdef USE_HOOKS
static LRESULT CALLBACK wine_hook_proc(int nCode, WPARAM wParam, LPARAM lParam)
{
	PCWPSTRUCT pParams = (PCWPSTRUCT)lParam;
	struct wined3d_wndproc *entry;
	struct wined3d_device *device;
	BOOL unicode;
	WNDPROC proc;
    
	if (nCode < 0 || DLLValid != DLL_VALID_VALUE)
		return CallNextHookEx(NULL, nCode, wParam, lParam);

	wined3d_wndproc_mutex_lock();
	entry = wined3d_find_wndproc(pParams->hwnd);

	if (entry)
	{
  	device = entry->device;
    unicode = entry->unicode;
    proc = entry->proc;
    
    if(device)
    {
    	device_process_message(device, pParams->hwnd, unicode, pParams->message, pParams->wParam, pParams->lParam, proc);
    }
	}
	
	wined3d_wndproc_mutex_unlock();
	
	return CallNextHookEx(NULL, nCode, wParam, lParam);
}

static LRESULT CALLBACK wine_hook_proc_pos(int nCode, WPARAM wParam, LPARAM lParam)
{
	if (nCode >= HC_ACTION)
	{
		const LPCWPRETSTRUCT lpcwprs = (LPCWPRETSTRUCT)lParam;
		switch (lpcwprs->message)
		{
			case WM_SETCURSOR:
				SetCursor(NULL);
				break;
		}
	}
	return CallNextHookEx(wine_hook_pos, nCode, wParam, lParam); 
}
#endif

BOOL wined3d_register_window(HWND window, struct wined3d_device *device)
{
    struct wined3d_wndproc *entry;

    wined3d_wndproc_mutex_lock();

    if (wined3d_find_wndproc(window))
    {
        wined3d_wndproc_mutex_unlock();
        WARN("Window %p is already registered with wined3d.\n", window);
        return TRUE;
    }

    if (wndproc_table.size == wndproc_table.count)
    {
        unsigned int new_size = max(1, wndproc_table.size * 2);
        struct wined3d_wndproc *new_entries;

        if (!wndproc_table.entries) new_entries = malloc(new_size * sizeof(*new_entries));
        else new_entries = realloc(wndproc_table.entries, new_size * sizeof(*new_entries));

        if (!new_entries)
        {
            wined3d_wndproc_mutex_unlock();
            ERR("Failed to grow table.\n");
            return FALSE;
        }

        wndproc_table.entries = new_entries;
        wndproc_table.size = new_size;
    }

    entry = &wndproc_table.entries[wndproc_table.count++];
    entry->window = window;
    entry->unicode = IsWindowUnicode(window);
    /* Set a window proc that matches the window. Some applications (e.g. NoX)
     * replace the window proc after we've set ours, and expect to be able to
     * call the previous one (ours) directly, without using CallWindowProc(). */
#ifndef USE_HOOKS
    if (entry->unicode)
        entry->proc = (WNDPROC)SetWindowLongPtrW(window, GWLP_WNDPROC, (LONG_PTR)wined3d_wndproc);
    else
        entry->proc = (WNDPROC)SetWindowLongPtrA(window, GWLP_WNDPROC, (LONG_PTR)wined3d_wndproc);
#endif
    entry->device = device;

    wined3d_wndproc_mutex_unlock();

    return TRUE;
}

void wined3d_unregister_window(HWND window)
{
    struct wined3d_wndproc *entry, *last;
    LONG_PTR proc;

    wined3d_wndproc_mutex_lock();

    if (!(entry = wined3d_find_wndproc(window)))
    {
        wined3d_wndproc_mutex_unlock();
        WARN("Window %p is not registered with wined3d.\n", window);
        return;
    }

#ifndef USE_HOOKS
    if (entry->unicode)
    {
        proc = GetWindowLongPtrW(window, GWLP_WNDPROC);
        if (proc != (LONG_PTR)wined3d_wndproc)
        {
            entry->device = NULL;
            wined3d_wndproc_mutex_unlock();
            WARN("Not unregistering window %p, window proc %#lx doesn't match wined3d window proc %p.\n",
                    window, proc, wined3d_wndproc);
            return;
        }

        SetWindowLongPtrW(window, GWLP_WNDPROC, (LONG_PTR)entry->proc);
    }
    else
    {
        proc = GetWindowLongPtrA(window, GWLP_WNDPROC);
        if (proc != (LONG_PTR)wined3d_wndproc)
        {
            entry->device = NULL;
            wined3d_wndproc_mutex_unlock();
            WARN("Not unregistering window %p, window proc %#lx doesn't match wined3d window proc %p.\n",
                    window, proc, wined3d_wndproc);
            return;
        }

        SetWindowLongPtrA(window, GWLP_WNDPROC, (LONG_PTR)entry->proc);
    }
#endif

    last = &wndproc_table.entries[--wndproc_table.count];
    if (entry != last) *entry = *last;

    wined3d_wndproc_mutex_unlock();
}

int crt_sse2_is_safe();
void crt_enable_sse2();

#ifdef WINED3D_STATIC
/* At process attach */
BOOL WINAPI DllMain_wined3d(HINSTANCE inst, DWORD reason, void *reserved)
#else
BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *reserved)
#endif
{
    switch (reason)
    {
        case DLL_PROCESS_ATTACH:
#ifdef HAVE_CRTEX
        		crt_locks_init(0);
#endif
        	  wine_debug_logs_init("wined3d.log");
#ifdef HAVE_CRTEX
        	  if(crt_sse2_is_safe())
        	  {
        	  	//printf("enabling SSE\n");
        	  	crt_enable_sse2();
        	  }
#endif
        	  
            return wined3d_dll_init(inst);

        case DLL_PROCESS_DETACH:
        	  DLLValid = -1;
        	  wine_debug_logs_close();
            if (!reserved)
            {
            	return wined3d_dll_destroy(inst);
            }
            wined3d_restore_display_state();
            break;

        case DLL_THREAD_DETACH:
            if (!context_set_current(NULL))
            {
                ERR("Failed to clear current context.\n");
            }
            return TRUE;
    }
    return TRUE;
}
