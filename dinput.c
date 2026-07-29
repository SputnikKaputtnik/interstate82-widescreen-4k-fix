/* dinput.dll proxy for the GOG release of Interstate '82.
 *
 * This release build keeps the verified heap-compatibility workaround and
 * DirectInput keyboard-name bridge. It contains no logging, UI hooks, or
 * diagnostic threads.
 *
 * DirectInputCreateA is intercepted only to wrap the system keyboard's
 * EnumObjects/GetObjectInfo calls. I82sim copies DIDEVICEOBJECTINSTANCEA
 * tszName directly into its input-registration path. Windows 11 returns
 * localized names there (and I82's path may observe them empty); the bridge
 * supplies the canonical English DI names I82 expects. The keyboard scan code
 * comes from dwOfs (the field I82 itself copies from the callback object),
 * with dwType only as a fallback. Every other DirectInput export remains a
 * forwarder.
 */
#define DIRECTINPUT_VERSION 0x0700
#include <windows.h>
#include <dinput.h>
#include <string.h>

/* ---- safe memory probing ---------------------------------------------- */
static int readable(const void* p,SIZE_T n){
    MEMORY_BASIC_INFORMATION mi;
    if(!p) return 0;
    if(!VirtualQuery(p,&mi,sizeof mi)) return 0;
    if(mi.State!=MEM_COMMIT) return 0;
    if(mi.Protect & (PAGE_NOACCESS|PAGE_GUARD)) return 0;
    if((BYTE*)p + n > (BYTE*)mi.BaseAddress + mi.RegionSize) return 0;
    return 1;
}

/* ---- module/arena identification -------------------------------------- */
#define ARENA_RVA      0x38B298u    /* "Project File" arena object          */
#define ARENA_NAME     "Project File"
#define ARENA_HEAP_OFF 0x20
static BYTE* g_i82Base=NULL;

static HANDLE arena_heap(void){
    if(!g_i82Base) return NULL;
    BYTE* obj=g_i82Base+ARENA_RVA;
    if(!readable(obj,0x30)) return NULL;
    if(memcmp(obj,ARENA_NAME,sizeof(ARENA_NAME)-1)!=0) return NULL;
    return *(HANDLE*)(obj+ARENA_HEAP_OFF);
}

/* ---- real function pointers ------------------------------------------- */
typedef SIZE_T (WINAPI *HeapSize_t)(HANDLE,DWORD,LPCVOID);
typedef LPVOID (WINAPI *HeapReAlloc_t)(HANDLE,DWORD,LPVOID,SIZE_T);
typedef BOOL   (WINAPI *HeapFree_t)(HANDLE,DWORD,LPVOID);
static HeapSize_t    g_realHeapSize=NULL;
static HeapReAlloc_t g_realHeapReAlloc=NULL;
static HeapFree_t    g_realHeapFree=NULL;

/* ---- move tracking ----------------------------------------------------- */
#define RING 256
typedef struct { HANDLE h; LPVOID oldp; LPVOID newp; } MoveEnt;
static MoveEnt g_ring[RING];
static LONG g_ridx=0;
static CRITICAL_SECTION g_cs;

/* ---- hooks: heap ------------------------------------------------------- */
static LPVOID WINAPI Hooked_HeapReAlloc(HANDLE h,DWORD f,LPVOID old,SIZE_T sz){
    if(!old) return g_realHeapReAlloc(h,f,old,sz);
    LPVOID r=g_realHeapReAlloc(h,f|HEAP_REALLOC_IN_PLACE_ONLY,old,sz);
    if(r) return r;
    if(f & HEAP_REALLOC_IN_PLACE_ONLY) return NULL;
    SIZE_T oldsz=g_realHeapSize(h,0,old);
    LPVOID nw=HeapAlloc(h,f&HEAP_ZERO_MEMORY,sz);
    if(!nw){
        nw=g_realHeapReAlloc(h,f,old,sz);
        if(nw && nw!=old){
            EnterCriticalSection(&g_cs);
            MoveEnt* e=&g_ring[(unsigned)(g_ridx++)%RING];
            e->h=h; e->oldp=old; e->newp=nw;
            LeaveCriticalSection(&g_cs);
        }
        return nw;
    }
    if(oldsz!=(SIZE_T)-1 && oldsz>0)
        memcpy(nw,old,oldsz<sz?oldsz:sz);
    return nw;
}

static BOOL WINAPI Hooked_HeapFree(HANDLE h,DWORD f,LPVOID m){
    HANDLE ah=arena_heap();
    if(ah && h==ah && m) return TRUE;            /* arena free -> quarantine */
    return g_realHeapFree(h,f,m);
}

static SIZE_T WINAPI Hooked_HeapSize(HANDLE h,DWORD f,LPCVOID m){
    if(m && !HeapValidate(h,0,m)){
        LPVOID nw=NULL;
        EnterCriticalSection(&g_cs);
        LONG newest=g_ridx;
        for(LONG i=newest-1; i>=newest-RING && i>=0; i--){
            MoveEnt* e=&g_ring[(unsigned)i%RING];
            if(e->h==h && e->oldp==(LPVOID)m){ nw=e->newp; break; }
        }
        LeaveCriticalSection(&g_cs);
        if(nw && HeapValidate(h,0,nw)) return g_realHeapSize(h,0,nw);
        return 0;
    }
    return g_realHeapSize(h,f,m);
}

/* ---- targeted DirectInput keyboard-name bridge ------------------------ */
static const char* dik_name(DWORD d){
    switch(d){
    case 0x01:return "Escape"; case 0x02:return "1"; case 0x03:return "2"; case 0x04:return "3";
    case 0x05:return "4"; case 0x06:return "5"; case 0x07:return "6"; case 0x08:return "7";
    case 0x09:return "8"; case 0x0A:return "9"; case 0x0B:return "0"; case 0x0C:return "-";
    case 0x0D:return "="; case 0x0E:return "Backspace"; case 0x0F:return "Tab";
    case 0x10:return "Q"; case 0x11:return "W"; case 0x12:return "E"; case 0x13:return "R";
    case 0x14:return "T"; case 0x15:return "Y"; case 0x16:return "U"; case 0x17:return "I";
    case 0x18:return "O"; case 0x19:return "P"; case 0x1A:return "["; case 0x1B:return "]";
    case 0x1C:return "Enter"; case 0x1D:return "Left Ctrl";
    case 0x1E:return "A"; case 0x1F:return "S"; case 0x20:return "D"; case 0x21:return "F";
    case 0x22:return "G"; case 0x23:return "H"; case 0x24:return "J"; case 0x25:return "K"; case 0x26:return "L";
    case 0x27:return ";"; case 0x28:return "'"; case 0x29:return "`"; case 0x2A:return "Left Shift"; case 0x2B:return "\\";
    case 0x2C:return "Z"; case 0x2D:return "X"; case 0x2E:return "C"; case 0x2F:return "V";
    case 0x30:return "B"; case 0x31:return "N"; case 0x32:return "M";
    case 0x33:return ","; case 0x34:return "."; case 0x35:return "/"; case 0x36:return "Right Shift";
    case 0x37:return "Numpad *"; case 0x38:return "Left Alt"; case 0x39:return "Space"; case 0x3A:return "Capslock";
    case 0x3B:return "F1"; case 0x3C:return "F2"; case 0x3D:return "F3"; case 0x3E:return "F4"; case 0x3F:return "F5";
    case 0x40:return "F6"; case 0x41:return "F7"; case 0x42:return "F8"; case 0x43:return "F9"; case 0x44:return "F10";
    case 0x45:return "Num Lock"; case 0x46:return "Scroll Lock";
    case 0x47:return "Numpad 7"; case 0x48:return "Numpad 8"; case 0x49:return "Numpad 9"; case 0x4A:return "Numpad -";
    case 0x4B:return "Numpad 4"; case 0x4C:return "Numpad 5"; case 0x4D:return "Numpad 6"; case 0x4E:return "Numpad +";
    case 0x4F:return "Numpad 1"; case 0x50:return "Numpad 2"; case 0x51:return "Numpad 3"; case 0x52:return "Numpad 0"; case 0x53:return "Numpad .";
    case 0x57:return "F11"; case 0x58:return "F12";
    case 0x9C:return "Numpad Enter"; case 0x9D:return "Right Ctrl"; case 0xB5:return "Numpad /"; case 0xB8:return "Right Alt";
    case 0xC7:return "Home"; case 0xC8:return "Up Arrow"; case 0xC9:return "PgUp";
    case 0xCB:return "Left Arrow"; case 0xCD:return "Right Arrow"; case 0xCF:return "End";
    case 0xD0:return "Down Arrow"; case 0xD1:return "PgDn"; case 0xD2:return "Insert"; case 0xD3:return "Delete";
    default:return NULL;
    }
}

typedef HRESULT (WINAPI *DICreateA_t)(HINSTANCE,DWORD,LPDIRECTINPUTA*,LPUNKNOWN);
typedef HRESULT (WINAPI *CreateDevice_t)(void*,REFGUID,LPDIRECTINPUTDEVICEA*,LPUNKNOWN);
typedef BOOL    (CALLBACK *EnumCB_t)(LPCDIDEVICEOBJECTINSTANCEA,LPVOID);
typedef HRESULT (WINAPI *EnumObjects_t)(void*,EnumCB_t,LPVOID,DWORD);
typedef HRESULT (WINAPI *GetObjInfo_t)(void*,LPDIDEVICEOBJECTINSTANCEA,DWORD,DWORD);
static DICreateA_t     g_realDICreateA=NULL;
static CreateDevice_t  g_realCreateDevice=NULL;
static EnumObjects_t   g_realEnumObjects=NULL;
static GetObjInfo_t    g_realGetObjInfo=NULL;
static const GUID KBD={0x6F1D2B61,0xD5A0,0x11CF,{0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00}};

static DWORD keyboard_scancode(const DIDEVICEOBJECTINSTANCEA* o){
    DWORD ofs=o->dwOfs;
    DWORD instance=DIDFT_GETINSTANCE(o->dwType);
    if(ofs<=0xFF && dik_name(ofs)) return ofs;
    if(instance<=0xFF && dik_name(instance)) return instance;
    return ofs<=0xFF ? ofs : instance;
}
static void override_keyboard_name(LPDIDEVICEOBJECTINSTANCEA o){
    if(!o) return;
    DWORD dik=keyboard_scancode(o);
    const char* mapped=dik_name(dik);
    if(mapped) lstrcpynA(o->tszName,mapped,MAX_PATH);
}

typedef struct { EnumCB_t callback; LPVOID ref; } EnumContext;
static BOOL CALLBACK Hooked_EnumCallback(LPCDIDEVICEOBJECTINSTANCEA inst,LPVOID ref){
    EnumContext* c=(EnumContext*)ref;
    override_keyboard_name((LPDIDEVICEOBJECTINSTANCEA)inst);
    return c->callback(inst,c->ref);
}
static HRESULT WINAPI Hooked_EnumObjects(void* self,EnumCB_t callback,LPVOID ref,DWORD flags){
    EnumContext c;
    c.callback=callback; c.ref=ref;
    return g_realEnumObjects(self,Hooked_EnumCallback,&c,flags);
}
static HRESULT WINAPI Hooked_GetObjectInfo(void* self,LPDIDEVICEOBJECTINSTANCEA obj,DWORD which,DWORD how){
    HRESULT hr=g_realGetObjInfo(self,obj,which,how);
    if(SUCCEEDED(hr)) override_keyboard_name(obj);
    return hr;
}
static void patch_vtable_slot(void** vtable,int index,void* replacement,void** original){
    if(!vtable || (original && *original)) return;
    DWORD oldProtect;
    if(original) *original=vtable[index];
    if(VirtualProtect(&vtable[index],sizeof(void*),PAGE_READWRITE,&oldProtect)){
        vtable[index]=replacement;
        VirtualProtect(&vtable[index],sizeof(void*),oldProtect,&oldProtect);
    }
}
static HRESULT WINAPI Hooked_CreateDevice(void* self,REFGUID guid,LPDIRECTINPUTDEVICEA* out,LPUNKNOWN outer){
    HRESULT hr=g_realCreateDevice(self,guid,out,outer);
    if(SUCCEEDED(hr) && out && *out && guid && !memcmp(guid,&KBD,sizeof(GUID))){
        void** vtable=*(void***)(*out);
        patch_vtable_slot(vtable,4,(void*)Hooked_EnumObjects,(void**)&g_realEnumObjects);
        patch_vtable_slot(vtable,14,(void*)Hooked_GetObjectInfo,(void**)&g_realGetObjInfo);
    }
    return hr;
}

HRESULT WINAPI DirectInputCreateA(HINSTANCE hinst,DWORD version,LPDIRECTINPUTA* out,LPUNKNOWN outer){
    if(!g_realDICreateA){
        HMODULE original=GetModuleHandleA("dinput_orig.dll");
        if(!original) original=LoadLibraryA("dinput_orig.dll");
        if(!original) return E_FAIL;
        g_realDICreateA=(DICreateA_t)GetProcAddress(original,"DirectInputCreateA");
        if(!g_realDICreateA) return E_FAIL;
    }
    HRESULT hr=g_realDICreateA(hinst,version,out,outer);
    if(SUCCEEDED(hr) && out && *out){
        void** vtable=*(void***)(*out);
        patch_vtable_slot(vtable,3,(void*)Hooked_CreateDevice,(void**)&g_realCreateDevice);
    }
    return hr;
}

/* ---- IAT hooking ------------------------------------------------------- */
static int IATHookByAddr(HMODULE mod,void* target,void* repl){
    BYTE* base=(BYTE*)mod; IMAGE_DOS_HEADER* dos=(IMAGE_DOS_HEADER*)base;
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE) return -1;
    IMAGE_NT_HEADERS* nt=(IMAGE_NT_HEADERS*)(base+dos->e_lfanew);
    IMAGE_DATA_DIRECTORY dir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if(!dir.VirtualAddress) return -3;
    IMAGE_IMPORT_DESCRIPTOR* d=(IMAGE_IMPORT_DESCRIPTOR*)(base+dir.VirtualAddress);
    int c=0;
    for(;d->Name;d++){ IMAGE_THUNK_DATA* ft=(IMAGE_THUNK_DATA*)(base+d->FirstThunk);
        for(;ft->u1.Function;ft++) if((void*)ft->u1.Function==target){ DWORD op;
            if(VirtualProtect(&ft->u1.Function,sizeof(void*),PAGE_READWRITE,&op)){
                ft->u1.Function=(DWORD_PTR)repl; VirtualProtect(&ft->u1.Function,sizeof(void*),op,&op); c++; } } }
    return c;
}

static DWORD WINAPI InstallThread(LPVOID p){ (void)p;
    HMODULE k=GetModuleHandleA("kernel32.dll");
    g_realHeapSize   =(HeapSize_t)   GetProcAddress(k,"HeapSize");
    g_realHeapReAlloc=(HeapReAlloc_t)GetProcAddress(k,"HeapReAlloc");
    g_realHeapFree   =(HeapFree_t)   GetProcAddress(k,"HeapFree");
    for(int i=0;i<40000;i++){
        HMODULE s=GetModuleHandleA("i82sim.dll");
        if(s){
            g_i82Base=(BYTE*)s;
            IATHookByAddr(s,(void*)g_realHeapSize,   (void*)Hooked_HeapSize);
            IATHookByAddr(s,(void*)g_realHeapReAlloc,(void*)Hooked_HeapReAlloc);
            IATHookByAddr(s,(void*)g_realHeapFree,   (void*)Hooked_HeapFree);
            return 0;
        }
        Sleep(15);
    }
    return 0;
}

#ifndef GET_MODULE_HANDLE_EX_FLAG_PIN
#define GET_MODULE_HANDLE_EX_FLAG_PIN 0x1
#define GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS 0x4
#endif
BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,LPVOID r){ (void)r;
    if(reason==DLL_PROCESS_ATTACH){
        HMODULE self=NULL;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN|GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            (LPCWSTR)(void*)&DllMain,&self);
        DisableThreadLibraryCalls(h);
        InitializeCriticalSection(&g_cs);
        CreateThread(NULL,0,InstallThread,NULL,0,NULL);
    }
    return TRUE;
}
