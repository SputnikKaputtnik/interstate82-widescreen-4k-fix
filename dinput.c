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
 * with dwType only as a fallback. Every other DirectInput export is passed
 * straight through to the real DLL.
 *
 * The real DirectInput is found at run time: a dinput_orig.dll next to the
 * game wins if present (older installs, or a deliberate override); otherwise
 * the system's own dinput.dll is loaded by full path. The game is a 32-bit
 * process, so Windows redirects that System32 path to SysWOW64 by itself.
 */
#define DIRECTINPUT_VERSION 0x0700
#include <windows.h>
#include <dinput.h>
#include <string.h>
#define COBJMACROS
#include <dsound.h>

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

/* ---- module identification -------------------------------------------- */
static BYTE* g_i82Base=NULL;

/* The crash this guards against is RtlSizeHeap reading [m-1] on a page the
 * heap has decommitted, so a mapping check on the block header at m-8 is
 * exactly the test that is needed. HeapValidate is deliberately not used: it
 * takes the heap lock on every call, and with CpuAffinity=1 pinning every
 * game thread to one core that contention was enough to hang the process. A
 * block that is freed but still mapped returns a wrong size rather than
 * faulting, which the caller survives. */
static int block_alive(HANDLE h,LPCVOID m){
    (void)h;
    return m && readable((const BYTE*)m-8,8);
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
    LPVOID nw=g_realHeapReAlloc(h,f,old,sz);     /* let the heap move it freely */
    if(nw && nw!=old){                           /* remember it for HeapSize */
        EnterCriticalSection(&g_cs);
        MoveEnt* e=&g_ring[(unsigned)(g_ridx++)%RING];
        e->h=h; e->oldp=old; e->newp=nw;
        LeaveCriticalSection(&g_cs);
    }
    return nw;
}

static BOOL WINAPI Hooked_HeapFree(HANDLE h,DWORD f,LPVOID m){
    return g_realHeapFree(h,f,m);                /* untouched; guard belongs in HeapSize */
}

static SIZE_T WINAPI Hooked_HeapSize(HANDLE h,DWORD f,LPCVOID m){
    if(m && !block_alive(h,m)){
        LPVOID nw=NULL;
        EnterCriticalSection(&g_cs);
        LONG newest=g_ridx;
        for(LONG i=newest-1; i>=newest-RING && i>=0; i--){
            MoveEnt* e=&g_ring[(unsigned)i%RING];
            if(e->h==h && e->oldp==(LPVOID)m){ nw=e->newp; break; }
        }
        LeaveCriticalSection(&g_cs);
        if(nw && block_alive(h,nw)) return g_realHeapSize(h,0,nw);
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
typedef HRESULT (WINAPI *GetDevInfo_t)(void*,LPDIDEVICEINSTANCEA);
typedef BOOL    (CALLBACK *EnumDevCB_t)(LPCDIDEVICEINSTANCEA,LPVOID);
typedef HRESULT (WINAPI *EnumDevices_t)(void*,DWORD,EnumDevCB_t,LPVOID,DWORD);
static DICreateA_t     g_realDICreateA=NULL;
static CreateDevice_t  g_realCreateDevice=NULL;
static EnumDevices_t   g_realEnumDevices=NULL;
static EnumObjects_t   g_realEnumObjects=NULL;
static GetObjInfo_t    g_realGetObjInfo=NULL;
static GetDevInfo_t    g_realGetDevInfo=NULL;
static const GUID KBD={0x6F1D2B61,0xD5A0,0x11CF,{0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00}};
static const GUID MOUSE={0x6F1D2B60,0xD5A0,0x11CF,{0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00}};

/* Names for the system keyboard and mouse come from DirectInput's string
 * resources, which Windows keeps in a language file beside the DLL
 * (SysWOW64\<lang>\dinput.dll.mui). Loaded from System32 the real DLL finds
 * that file and hands out localized names: "Tastatur", "Maus", "Leertaste".
 * I82 finds a bound key by device name and key name: bindings.def (inside
 * i82.zfs) binds every action to "Keyboard" plus an English key name. With
 * localized names every action "does not exist" and the keyboard is dead in
 * missions. So the proxy reports the system keyboard and mouse as "Keyboard"
 * and "Mouse", canonical English key names, and empty names for anything
 * unmapped on those two devices. */
enum { DEV_OTHER=0, DEV_KEYBOARD, DEV_MOUSE };
static int system_device(const GUID* g){
    if(!memcmp(g,&KBD,sizeof(GUID))) return DEV_KEYBOARD;
    if(!memcmp(g,&MOUSE,sizeof(GUID))) return DEV_MOUSE;
    return DEV_OTHER;
}
static int device_kind(void* self){
    if(!g_realGetDevInfo) return DEV_OTHER;
    DIDEVICEINSTANCEA d; ZeroMemory(&d,sizeof d); d.dwSize=sizeof d;
    if(FAILED(g_realGetDevInfo(self,&d))) return DEV_OTHER;
    return system_device(&d.guidInstance);
}

static DWORD keyboard_scancode(const DIDEVICEOBJECTINSTANCEA* o){
    DWORD ofs=o->dwOfs;
    DWORD instance=DIDFT_GETINSTANCE(o->dwType);
    if(ofs<=0xFF && dik_name(ofs)) return ofs;
    if(instance<=0xFF && dik_name(instance)) return instance;
    return ofs<=0xFF ? ofs : instance;
}
static void override_object_name(LPDIDEVICEOBJECTINSTANCEA o,int kind){
    if(!o) return;
    DWORD dik=keyboard_scancode(o);
    const char* mapped=dik_name(dik);
    if(mapped) lstrcpynA(o->tszName,mapped,MAX_PATH);
    else if(kind!=DEV_OTHER) o->tszName[0]=0;
}

typedef struct { EnumCB_t callback; LPVOID ref; int kind; } EnumContext;
static BOOL CALLBACK Hooked_EnumCallback(LPCDIDEVICEOBJECTINSTANCEA inst,LPVOID ref){
    EnumContext* c=(EnumContext*)ref;
    override_object_name((LPDIDEVICEOBJECTINSTANCEA)inst,c->kind);
    return c->callback(inst,c->ref);
}
static HRESULT WINAPI Hooked_EnumObjects(void* self,EnumCB_t callback,LPVOID ref,DWORD flags){
    EnumContext c;
    c.callback=callback; c.ref=ref; c.kind=device_kind(self);
    return g_realEnumObjects(self,Hooked_EnumCallback,&c,flags);
}
static HRESULT WINAPI Hooked_GetObjectInfo(void* self,LPDIDEVICEOBJECTINSTANCEA obj,DWORD which,DWORD how){
    HRESULT hr=g_realGetObjInfo(self,obj,which,how);
    if(SUCCEEDED(hr)) override_object_name(obj,device_kind(self));
    return hr;
}
static void english_device_names(LPDIDEVICEINSTANCEA d){
    int kind=system_device(&d->guidInstance);
    if(kind!=DEV_OTHER){
        const char* name=kind==DEV_KEYBOARD ? "Keyboard" : "Mouse";
        lstrcpynA(d->tszInstanceName,name,MAX_PATH);
        lstrcpynA(d->tszProductName,name,MAX_PATH);
    }
}
static HRESULT WINAPI Hooked_GetDeviceInfo(void* self,LPDIDEVICEINSTANCEA d){
    HRESULT hr=g_realGetDevInfo(self,d);
    if(SUCCEEDED(hr) && d) english_device_names(d);
    return hr;
}
typedef struct { EnumDevCB_t callback; LPVOID ref; } EnumDevContext;
static BOOL CALLBACK Hooked_EnumDevCallback(LPCDIDEVICEINSTANCEA inst,LPVOID ref){
    EnumDevContext* c=(EnumDevContext*)ref;
    DIDEVICEINSTANCEA copy;
    DWORD n=inst->dwSize<sizeof copy ? inst->dwSize : sizeof copy;
    ZeroMemory(&copy,sizeof copy);
    memcpy(&copy,inst,n);
    english_device_names(&copy);
    return c->callback(&copy,c->ref);
}
static HRESULT WINAPI Hooked_EnumDevices(void* self,DWORD type,EnumDevCB_t callback,LPVOID ref,DWORD flags){
    EnumDevContext c;
    c.callback=callback; c.ref=ref;
    return g_realEnumDevices(self,type,Hooked_EnumDevCallback,&c,flags);
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
    if(SUCCEEDED(hr) && out && *out && guid && system_device(guid)!=DEV_OTHER){
        /* keyboard and mouse share one vtable in the system DLL */
        void** vtable=*(void***)(*out);
        patch_vtable_slot(vtable,4,(void*)Hooked_EnumObjects,(void**)&g_realEnumObjects);
        patch_vtable_slot(vtable,14,(void*)Hooked_GetObjectInfo,(void**)&g_realGetObjInfo);
        patch_vtable_slot(vtable,15,(void*)Hooked_GetDeviceInfo,(void**)&g_realGetDevInfo);
    }
    return hr;
}

/* The real DirectInput. Never called from DllMain, so loading is safe here. */
static HMODULE g_realDinput=NULL;
static HMODULE real_dinput(void){
    if(g_realDinput) return g_realDinput;
    HMODULE m=GetModuleHandleA("dinput_orig.dll");
    if(!m) m=LoadLibraryA("dinput_orig.dll");
    if(!m){
        char path[MAX_PATH];
        UINT n=GetSystemDirectoryA(path,MAX_PATH);
        if(n && n<MAX_PATH-12){
            lstrcatA(path,"\\dinput.dll");
            m=LoadLibraryA(path);
        }
    }
    if(m){
        /* two threads may race here; each LoadLibrary is balanced by the
           module staying loaded for the life of the process, so the loser's
           extra reference is harmless */
        g_realDinput=m;
    }
    return m;
}
static FARPROC real_proc(const char* name){
    HMODULE m=real_dinput();
    return m ? GetProcAddress(m,name) : NULL;
}

/* Up to v1.3.2 the proxy reported the keyboard with an empty name, so a
 * bindings.usr saved from the Controls screen names its device "". With the
 * keyboard called "Keyboard" again, such a file would bind nothing. Rewrite
 * those empty device fields once, before the game reads the file, and keep
 * the old file as bindings.usr.before-v1.3.3. */
static void mlog(const char* t);
static void migrate_bindings(void){
    static const char path[]="bindings.usr";
    static const char backup[]="bindings.usr.before-v1.3.3";
    static const char tmp[]="bindings.usr.tmp";
    HANDLE h=CreateFileA(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(h==INVALID_HANDLE_VALUE) return;
    DWORD size=GetFileSize(h,NULL),got=0;
    if(size==INVALID_FILE_SIZE || size>0x10000){ CloseHandle(h); return; }
    HANDLE heap=GetProcessHeap();
    char* in=(char*)HeapAlloc(heap,0,size+1);
    DWORD lines=1;
    if(in && ReadFile(h,in,size,&got,NULL) && got==size){
        for(DWORD i=0;i<size;i++) if(in[i]=='\n') lines++;
    }else got=0;
    CloseHandle(h);
    char* out=got ? (char*)HeapAlloc(heap,0,size+8*lines) : NULL;
    DWORD n=0,changed=0;
    for(DWORD i=0;out && i<size;){
        DWORD end=i;
        while(end<size && in[end]!='\n') end++;
        DWORD quotes=0,device=0;
        for(DWORD k=i;k<end;k++) if(in[k]=='"' && ++quotes==3) device=k;
        for(DWORD k=i;k<end && k<size;k++){
            out[n++]=in[k];
            if(quotes>=6 && k==device && in[k+1]=='"'){
                memcpy(out+n,"Keyboard",8); n+=8; changed++;
            }
        }
        if(end<size) out[n++]='\n';
        i=end+1;
    }
    if(changed && CopyFileA(path,backup,TRUE)){
        HANDLE w=CreateFileA(tmp,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
        DWORD put=0;
        BOOL ok=w!=INVALID_HANDLE_VALUE && WriteFile(w,out,n,&put,NULL) && put==n;
        if(w!=INVALID_HANDLE_VALUE) CloseHandle(w);
        if(ok && MoveFileExA(tmp,path,MOVEFILE_REPLACE_EXISTING)){
            char msg[160];
            wsprintfA(msg,"[shim] bindings.usr: %lu empty device names set to \"Keyboard\", old file kept as %s\r\n",changed,backup);
            mlog(msg);
        }else DeleteFileA(tmp);
    }
    if(in) HeapFree(heap,0,in);
    if(out) HeapFree(heap,0,out);
}

HRESULT WINAPI DirectInputCreateA(HINSTANCE hinst,DWORD version,LPDIRECTINPUTA* out,LPUNKNOWN outer){
    static LONG migrated=0;
    if(!InterlockedExchange(&migrated,1)) migrate_bindings();
    if(!g_realDICreateA){
        g_realDICreateA=(DICreateA_t)real_proc("DirectInputCreateA");
        if(!g_realDICreateA) return E_FAIL;
    }
    HRESULT hr=g_realDICreateA(hinst,version,out,outer);
    if(SUCCEEDED(hr) && out && *out){
        void** vtable=*(void***)(*out);
        patch_vtable_slot(vtable,3,(void*)Hooked_CreateDevice,(void**)&g_realCreateDevice);
        patch_vtable_slot(vtable,4,(void*)Hooked_EnumDevices,(void**)&g_realEnumDevices);
    }
    return hr;
}

/* ---- remaining exports: plain pass-through to the real DLL --------------- */
typedef HRESULT (WINAPI *DICreateW_t)(HINSTANCE,DWORD,LPDIRECTINPUTW*,LPUNKNOWN);
typedef HRESULT (WINAPI *DICreateEx_t)(HINSTANCE,DWORD,REFIID,LPVOID*,LPUNKNOWN);
typedef HRESULT (WINAPI *GetClassObject_t)(REFCLSID,REFIID,LPVOID*);
typedef HRESULT (WINAPI *NoArgs_t)(void);

HRESULT WINAPI DirectInputCreateW(HINSTANCE hinst,DWORD version,LPDIRECTINPUTW* out,LPUNKNOWN outer){
    DICreateW_t f=(DICreateW_t)real_proc("DirectInputCreateW");
    return f ? f(hinst,version,out,outer) : E_FAIL;
}
HRESULT WINAPI DirectInputCreateEx(HINSTANCE hinst,DWORD version,REFIID iid,LPVOID* out,LPUNKNOWN outer){
    DICreateEx_t f=(DICreateEx_t)real_proc("DirectInputCreateEx");
    return f ? f(hinst,version,iid,out,outer) : E_FAIL;
}
HRESULT WINAPI DllCanUnloadNow(void){
    /* the proxy pins itself, and an unloaded real DLL has nothing to say */
    if(!g_realDinput) return S_FALSE;
    NoArgs_t f=(NoArgs_t)GetProcAddress(g_realDinput,"DllCanUnloadNow");
    return f ? f() : S_FALSE;
}
HRESULT WINAPI DllGetClassObject(REFCLSID clsid,REFIID iid,LPVOID* out){
    GetClassObject_t f=(GetClassObject_t)real_proc("DllGetClassObject");
    if(!f){ if(out) *out=NULL; return CLASS_E_CLASSNOTAVAILABLE; }
    return f(clsid,iid,out);
}
HRESULT WINAPI DllRegisterServer(void){
    NoArgs_t f=(NoArgs_t)real_proc("DllRegisterServer");
    return f ? f() : E_FAIL;
}
HRESULT WINAPI DllUnregisterServer(void){
    NoArgs_t f=(NoArgs_t)real_proc("DllUnregisterServer");
    return f ? f() : E_FAIL;
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

/* ---- input-system popup suppression ------------------------------------
 * I82 reports binding problems through MessageBox, using the C++ method name
 * as the caption. They are developer diagnostics with no player-actionable
 * content; one appears between the menu and the load screen on every mission
 * start and has to be dismissed by hand. Answer them with IDOK and keep the
 * text in a log instead. */
static void logto(const char* file,const char* t){
    HANDLE h=CreateFileA(file,FILE_APPEND_DATA,
        FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if(h!=INVALID_HANDLE_VALUE){DWORD w;WriteFile(h,t,(DWORD)lstrlenA(t),&w,NULL);CloseHandle(h);}
}
static void mlog(const char* t){ logto("dinput_msgbox.log",t); }

/* Build with -DI82_DIAG to trace module loads and where the mode patch lands.
 * Release builds write no such log. */
#ifdef I82_DIAG
static void dnum(char** p,char* end,DWORD v){
    char n[16]; int i=0;
    if(!v) n[i++]='0';
    while(v && i<15){ n[i++]=(char)('0'+(v%10u)); v/=10u; }
    while(i && *p<end) *(*p)++=n[--i];
}
static void dlog(const char* what,const char* name,DWORD a,DWORD b){
    char s[512],*p=s,*end=s+500;
    for(const char* c=what; *c && p<end; ) *p++=*c++;
    *p++=' ';
    if(name && readable(name,1)) for(const char* c=name; *c && p<end; ) *p++=*c++;
    else { const char* q="(none)"; while(*q && p<end) *p++=*q++; }
    *p++=' '; dnum(&p,end,a);
    *p++=' '; dnum(&p,end,b);
    *p++='\n'; *p=0;
    logto("dinput_diag.log",s);
}
#endif

typedef int (WINAPI *MessageBoxA_t)(HWND,LPCSTR,LPCSTR,UINT);
static MessageBoxA_t g_realMessageBoxA=NULL;

static void mlog_box(const char* tag,LPCSTR caption,LPCSTR text){
    char b[512],*p=b;
    for(const char* c=tag; *c && p<b+40; ) *p++=*c++;
    if(caption && readable(caption,1))
        for(const char* c=caption; *c && p<b+220; c++) *p++=(*c=='\n'||*c=='\r')?' ':*c;
    *p++=':'; *p++=' ';
    if(text && readable(text,1))
        for(const char* c=text; *c && p<b+490; c++) *p++=(*c=='\n'||*c=='\r')?' ':*c;
    *p++='\n'; *p=0;
    mlog(b);
}

static int WINAPI Hooked_MessageBoxA(HWND wnd,LPCSTR text,LPCSTR caption,UINT type){
    if(caption && readable(caption,6) && !memcmp(caption,"CInput",6)){
        mlog_box("[swallowed] ",caption,text);
        return IDOK;
    }
    /* Everything else is meant for the player and is passed through. It still
     * gets logged, because DDrawCompat's fullscreen window covers it: the box
     * cannot be read or clicked, and a display error this way has cost a
     * Windows session more than once. The log is then the only record of what
     * it said. */
    mlog_box("[shown] ",caption,text);
    return g_realMessageBoxA(wnd,text,caption,type);
}

static void hook_messagebox(const char* mod){
    HMODULE m=NULL;
    if(!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,mod,&m) || !m) return;
    if(g_realMessageBoxA) IATHookByAddr(m,(void*)g_realMessageBoxA,(void*)Hooked_MessageBoxA);
}

/* ---- widescreen: widen the hardcoded display-mode filter ---------------
 * I82 accepts a display mode only when width and height match one of four
 * hardcoded pairs, checked instruction by instruction in the EnumDisplayModes
 * callback. There is no table to edit, which is why no configuration file can
 * produce a widescreen mode.
 *
 * The same engine source is linked into i82sim.dll and I82ShellDll.dll, and
 * the menu's list comes from the shell -- i82sim is not even loaded while the
 * video options are open. The two builds hold width and height differently:
 *
 *     i82sim       81 7D E8 00 05 00 00   cmp dword ptr [ebp-18h],500h
 *     I82ShellDll  81 FF    00 05 00 00   cmp edi,500h
 *
 * so a fixed signature matches only one. What both share is the shape:
 * compare width against 1024, jump short, compare width against 1280 through
 * the identical operand, then compare height against 1024 shortly after.
 * Matching that shape locates the immediates in either module.
 *
 * The width search tests ">800" first, so the replacement has to stay in that
 * branch: 1280x1024 is taken over, 1024x768 remains as the fallback. */
#define MODE_OLD_W 1280u
#define MODE_OLD_H 1024u
#define MODE_NEW_W 1920u
#define MODE_NEW_H 1080u
/* The filter has four hardcoded slots and no room for a fifth, and the width
 * dispatch tests ">800" first, so a second widescreen mode has to take the
 * other slot in that branch: 1024x768. 640x480 and 800x600 stay untouched. */
#define MODE2_OLD_H 768u
#define MODE2_NEW_W 3840u
#define MODE2_NEW_H 2160u
#define MODE_H_SEARCH 160          /* how far the height compare may sit away */

static int write_imm(DWORD* at,DWORD value){
    DWORD old;
    if(!VirtualProtect(at,4,PAGE_EXECUTE_READWRITE,&old)) return 0;
    *at=value;
    VirtualProtect(at,4,old,&old);
    return 1;
}

/* Trading 1024x768 away is only a gain on a desktop that can actually show the
 * replacement; below that it would cost a mode the monitor has for one it does
 * not. ENUM_REGISTRY_SETTINGS reports the desktop mode rather than whatever
 * the game may already have switched to. */
static int desktop_at_least(DWORD w,DWORD h){
    DEVMODEA dm;
    ZeroMemory(&dm,sizeof dm);
    dm.dmSize=sizeof dm;
    if(!EnumDisplaySettingsA(NULL,ENUM_REGISTRY_SETTINGS,&dm)) return 0;
    return dm.dmPelsWidth>=w && dm.dmPelsHeight>=h;
}

static int widen_modes_in(BYTE* base,DWORD** witness){
    if(!base) return 0;
    IMAGE_DOS_HEADER* dos=(IMAGE_DOS_HEADER*)base;
    if(!readable(base,0x40) || dos->e_magic!=IMAGE_DOS_SIGNATURE) return 0;
    IMAGE_NT_HEADERS* nt=(IMAGE_NT_HEADERS*)(base+dos->e_lfanew);
    if(!readable(nt,sizeof *nt) || nt->Signature!=IMAGE_NT_SIGNATURE) return 0;
    SIZE_T size=nt->OptionalHeader.SizeOfImage;
    if(size<128) return 0;

    int hits=0;
    int fourk=desktop_at_least(MODE2_NEW_W,MODE2_NEW_H);
    for(SIZE_T i=0;i+96<size;i++){
        if(base[i]!=0x81) continue;                       /* cmp r/m32, imm32 */
        for(int ol=1;ol<=3;ol++){                         /* operand bytes    */
            BYTE* w1=base+i;
            if(*(DWORD*)(w1+1+ol)!=1024u) continue;       /* width == 1024    */
            BYTE* j=w1+1+ol+4;
            if(j[0]!=0x74) continue;                      /* je short         */
            BYTE* w2=j+2;
            if(w2[0]!=0x81 || memcmp(w2+1,w1+1,ol)!=0) continue;  /* same operand */
            if(*(DWORD*)(w2+1+ol)!=MODE_OLD_W) continue;  /* width == 1280    */

            /* the height compare uses a different operand of the same length */
            DWORD* himm=NULL;
            for(BYTE* h=w2+1+ol+4; h+8<base+size && h<w2+MODE_H_SEARCH; h++){
                if(h[0]!=0x81) continue;
                if(*(DWORD*)(h+1+ol)==MODE_OLD_H){ himm=(DWORD*)(h+1+ol); break; }
            }
            if(!himm) continue;

            /* The je just after the 1024 compare is that slot's own branch and
             * lands directly on its height compare, in both modules. Following
             * it is exact; searching a window around the site instead is what
             * once rewrote the wrong compare and took 1920x1080 down with it.
             * The target must be a cmp against 768 using the same operand as
             * the height compare above, or the slot is left alone. */
            DWORD* w4=NULL; DWORD* h4=NULL;
            if(fourk){
                BYTE* t=j+2+(signed char)j[1];
                if(t>base && t+8<base+size && t[0]==0x81 &&
                   memcmp(t+1,(BYTE*)himm-ol,ol)==0 &&
                   *(DWORD*)(t+1+ol)==MODE2_OLD_H){
                    w4=(DWORD*)(w1+1+ol);
                    h4=(DWORD*)(t+1+ol);
                }
            }

            if(!write_imm((DWORD*)(w2+1+ol),MODE_NEW_W) || !write_imm(himm,MODE_NEW_H))
                break;
            hits++;
            if(witness) *witness=(DWORD*)(w2+1+ol);
            if(w4 && write_imm(w4,MODE2_NEW_W))
                write_imm(h4,MODE2_NEW_H);
            break;
        }
    }
    return hits;
}

/* Scanning a multi-megabyte image on every poll would be wasteful, and once
 * rewritten the pattern no longer matches anyway, so a module is scanned only
 * until the patch has actually landed.
 *
 * Marking it done regardless of the outcome is what made widescreen flaky:
 * both modules are packed, so their code is still encrypted for a short while
 * after they appear in the loader's module list. A poll that lands inside that
 * window scans ciphertext, finds nothing, and -- with the flag set up front --
 * never looks again, leaving the game with its original four resolutions for
 * the rest of the session. So: only a scan that found something counts as
 * done, and a module that never matches is given a bounded number of tries
 * rather than being rescanned forever. */
/* Non-zero while a hooked LoadLibrary call is in progress on any thread. */
static volatile LONG g_inLoad=0;

#define WS_MAX_TRIES 100                  /* ~5 s at the 50 ms poll interval */
static BYTE* g_wsDone[2]={NULL,NULL};
static BYTE* g_wsSeen[2]={NULL,NULL};
static int   g_wsTries[2]={0,0};
/* Where the patch landed: the slot-2 width immediate, which reads 1920 while
 * the patch is in place. See below for why the module address is not enough. */
static DWORD* g_wsWitness[2]={NULL,NULL};

/* "Restart Mission" unloads i82sim.dll and loads it again, usually at the very
 * same address. The poll re-arms only if it happens to see the module absent,
 * and the gap between unload and reload is often shorter than the 50 ms poll
 * interval. The fresh copy then carried the old "done" mark, stayed at the
 * original four resolutions, and the game failed to find the selected
 * widescreen mode ("Display Error ... unable to set your chosen screen
 * resolution", then "World Init failed" and a loop of message boxes). So a
 * module counts as done only while the patched bytes are actually still
 * there. */
static int still_widened(int slot){
    DWORD* w=g_wsWitness[slot];
    if(!w) return 1;                  /* never matched: nothing to keep up */
    if(!readable(w,sizeof *w)) return 1;       /* mid-(un)map: next poll */
    return *w==MODE_NEW_W;
}
static void widen_module(const char* name,int slot){
    HMODULE m=NULL;
    if(!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,name,&m) || !m){
        g_wsDone[slot]=NULL; g_wsSeen[slot]=NULL; g_wsTries[slot]=0; g_wsWitness[slot]=NULL;
        return;                                           /* unloaded: re-arm */
    }
    if(g_wsDone[slot]==(BYTE*)m){
        if(still_widened(slot)) return;
        /* same address, original code again: reloaded between two polls */
        g_wsDone[slot]=NULL; g_wsSeen[slot]=NULL; g_wsTries[slot]=0; g_wsWitness[slot]=NULL;
    }
    /* Never write into a module that is still being loaded. It appears in the
     * loader's list before its entry point has finished, so the packer is
     * still decrypting the very bytes being patched -- retrying until the
     * scan succeeds turned that from a harmless miss into a corrupted module,
     * and the game then failed to start at any resolution. */
    if(g_inLoad>0) return;
    if(g_wsSeen[slot]!=(BYTE*)m){ g_wsSeen[slot]=(BYTE*)m; g_wsTries[slot]=0; }
    DWORD* witness=NULL;
    int hits=widen_modes_in((BYTE*)m,&witness);
    if(hits>0) g_wsWitness[slot]=witness;
    g_wsTries[slot]++;
    if(hits>0 || g_wsTries[slot]>=WS_MAX_TRIES) g_wsDone[slot]=(BYTE*)m;
#ifdef I82_DIAG
    if(hits>0 || g_wsTries[slot]>=WS_MAX_TRIES || g_wsTries[slot]==1)
        dlog("widen",name,(DWORD)(DWORD_PTR)m,(DWORD)hits);
#endif
}
static void widen_display_modes(void){
    widen_module("i82sim.dll",0);
    widen_module("I82ShellDll.dll",1);
}

/* ---- CD music: route the CD calls to GOG's ogg-winmm ------------------
 * The GOG release plays its soundtrack through its own winmm.dll next to the
 * game ("ogg-winmm virtual CD"): it answers the cdaudio MCI commands and plays
 * MUSIC\TrackNN.ogg. That only works if the game loads that file as its
 * winmm. When Windows applies compatibility shims to i82stubz.exe, AcLayers
 * and AcGenral bring in the system winmm.dll before the game starts, every
 * module then binds to that one, and the music is silently missing.
 *
 * The music DLL implements only six functions itself and forwards the rest.
 * Loaded by full path next to the system winmm, it becomes a separate module
 * whose own imports bind to the system DLL, and later imports of winmm.dll
 * keep getting the system one. So when the loaded winmm is not the one in the
 * game folder, load the game folder's copy (or ogg-winmm.dll, if it has been
 * renamed to keep the game off it) and point those six imports of the two
 * modules that play CD audio at it: mss32.dll (Miles' redbook functions, in
 * missions) and I82ShellDll.dll. Like the heap hooks this is idempotent, and
 * a module the game loads again gets patched again. */
static const char* const g_cdFuncs[]={"mciSendCommandA","mciSendStringA",
    "auxGetDevCapsA","auxGetNumDevs","auxGetVolume","auxSetVolume"};
#define CD_FUNCS (sizeof g_cdFuncs/sizeof g_cdFuncs[0])
static HMODULE g_sysWinmm=NULL;
static HMODULE g_oggWinmm=NULL;         /* loaded by us next to the system one */
static HMODULE g_oggPrimary=NULL;       /* the game's own winmm is the music DLL */
static volatile LONG g_oggState=0;       /* 0 not yet, 1 loading, 2 settled */

static DWORD game_dir(char* dir,DWORD cap){
    DWORD n=GetModuleFileNameA(NULL,dir,cap);
    if(!n || n>=cap) return 0;
    while(n && dir[n-1]!='\\') n--;
    dir[n]=0;                            /* keeps the trailing backslash */
    return n;
}
static HMODULE music_dll(void){
    if(g_oggState==2) return g_oggWinmm;
    HMODULE sys=GetModuleHandleA("winmm.dll");
    if(!sys) return NULL;                /* nothing plays CD audio yet */
    if(InterlockedCompareExchange(&g_oggState,1,0)!=0) return NULL;
    char dir[MAX_PATH],loaded[MAX_PATH],path[MAX_PATH];
    DWORD n=game_dir(dir,MAX_PATH);
    DWORD m=GetModuleFileNameA(sys,loaded,MAX_PATH);
    /* the loaded winmm sits directly in the game folder: the game already
     * plays its music through it */
    int game_own=n && m>n && m<MAX_PATH
        && CompareStringA(LOCALE_INVARIANT,NORM_IGNORECASE,loaded,n,dir,n)==CSTR_EQUAL;
    for(DWORD k=n;game_own && k<m;k++) if(loaded[k]=='\\') game_own=0;
    if(game_own) g_oggPrimary=sys;
    if(n && !game_own){
        static const char* const names[]={"winmm.dll","ogg-winmm.dll"};
        for(unsigned i=0;i<2 && !g_oggWinmm;i++){
            if(n+lstrlenA(names[i])>=MAX_PATH) break;
            lstrcpyA(path,dir); lstrcatA(path,names[i]);
            if(GetFileAttributesA(path)==INVALID_FILE_ATTRIBUTES) continue;
            HMODULE o=LoadLibraryExA(path,NULL,LOAD_WITH_ALTERED_SEARCH_PATH);
            if(!o) continue;
            FARPROC mine=GetProcAddress(o,"mciSendCommandA");
            if(o!=sys && mine && mine!=GetProcAddress(sys,"mciSendCommandA")){ g_sysWinmm=sys; g_oggWinmm=o; }
            else FreeLibrary(o);
        }
    }
    InterlockedExchange(&g_oggState,2);
    return g_oggWinmm;
}
/* ---- CD music output through DirectSound -------------------------------
 * Miles plays the sound effects through DirectSound 3D buffers, and on
 * current Windows they fall silent while the music DLL plays through waveOut
 * in the same process -- with or without this shim. They come back when that
 * waveOut stream is swallowed, although the game still sees the CD playing.
 * The music DLL uses six waveOut functions and nothing else of the wave API,
 * so in its import table those six are replaced by a small waveOut on top of
 * a DirectSound streaming buffer. It still decodes the tracks itself; only
 * the output moves to the API the effects use. If DirectSound cannot be set
 * up, the calls go to the waveOut the DLL was bound to, as before. */
#define DSW_MAGIC 0x57534431u
typedef MMRESULT (WINAPI *wOpen_t)(LPHWAVEOUT,UINT,LPCWAVEFORMATEX,DWORD_PTR,DWORD_PTR,DWORD);
typedef MMRESULT (WINAPI *wHdr_t)(HWAVEOUT,LPWAVEHDR,UINT);
typedef MMRESULT (WINAPI *wH_t)(HWAVEOUT);
typedef HRESULT (WINAPI *DSCreate_t)(LPCGUID,LPDIRECTSOUND*,LPUNKNOWN);
static wOpen_t g_rwOpen=NULL;
static wHdr_t  g_rwPrep=NULL,g_rwUnprep=NULL,g_rwWrite=NULL;
static wH_t    g_rwReset=NULL,g_rwClose=NULL;

typedef struct DSNode { struct DSNode* next; WAVEHDR* h; DWORD copied; int full; ULONGLONG end; } DSNode;
typedef struct {
    DWORD magic;
    LPDIRECTSOUND ds; LPDIRECTSOUNDBUFFER buf;
    DWORD size,block,lead;               /* ring bytes, bytes per frame, bytes kept ahead */
    int silence;                         /* fill byte: 0x80 for 8-bit, 0 for 16-bit */
    CRITICAL_SECTION cs; HANDLE thread,wake; volatile LONG quit;
    DSNode *head,*tail;                  /* queued headers, oldest first */
    ULONGLONG written,played;            /* running byte counts */
    DWORD wpos,lastPlay;                 /* ring offsets: next write, last play cursor */
    DWORD_PTR cb,inst; DWORD cbType;
    WAVEFORMATEX fmt;
} DSWave;

static void dsw_notify(DSWave* w,UINT msg,WAVEHDR* h){
    UINT mm=msg==WOM_DONE?MM_WOM_DONE:msg==WOM_OPEN?MM_WOM_OPEN:MM_WOM_CLOSE;
    switch(w->cbType){
    case CALLBACK_FUNCTION:
        ((void (CALLBACK*)(HWAVEOUT,UINT,DWORD_PTR,DWORD_PTR,DWORD_PTR))w->cb)((HWAVEOUT)w,msg,w->inst,(DWORD_PTR)h,0);
        break;
    case CALLBACK_WINDOW: PostMessageA((HWND)w->cb,mm,(WPARAM)w,(LPARAM)h); break;
    case CALLBACK_THREAD: PostThreadMessageA((DWORD)w->cb,mm,(WPARAM)w,(LPARAM)h); break;
    case CALLBACK_EVENT:  SetEvent((HANDLE)w->cb); break;
    }
}
/* Write n bytes (silence when src is NULL) into the ring at the write count. */
static void dsw_put(DSWave* w,const BYTE* src,DWORD n){
    void *p1,*p2; DWORD n1,n2;
    if(!n || FAILED(IDirectSoundBuffer_Lock(w->buf,w->wpos,n,&p1,&n1,&p2,&n2,0))) return;
    if(src){ memcpy(p1,src,n1); if(p2) memcpy(p2,src+n1,n2); }
    else   { memset(p1,w->silence,n1); if(p2) memset(p2,w->silence,n2); }
    IDirectSoundBuffer_Unlock(w->buf,p1,n1,p2,n2);
    w->written+=n1+n2;
    w->wpos=(w->wpos+n1+n2)%w->size;
}
static void dsw_done(DSWave* w,DSNode* list){
    while(list){
        DSNode* next=list->next;
        dsw_notify(w,WOM_DONE,list->h);
        HeapFree(GetProcessHeap(),0,list);
        list=next;
    }
}
static DWORD WINAPI dsw_thread(LPVOID arg){
    DSWave* w=(DSWave*)arg;
    while(!w->quit){
        WaitForSingleObject(w->wake,10);
        DSNode *done=NULL,**dtail=&done;
        EnterCriticalSection(&w->cs);
        DWORD play=0,wc=0;
        if(SUCCEEDED(IDirectSoundBuffer_GetCurrentPosition(w->buf,&play,&wc))){
            w->played+=(play+w->size-w->lastPlay)%w->size; w->lastPlay=play;
            if(w->played>w->written){ w->written=w->played; w->wpos=play; }   /* underrun: go on at the cursor */
        }
        /* a header is done once its last byte has been played */
        while(w->head && w->head->full && w->head->end<=w->played){
            DSNode* n=w->head; w->head=n->next; if(!w->head) w->tail=NULL;
            n->h->dwFlags=(n->h->dwFlags&~WHDR_INQUEUE)|WHDR_DONE;
            n->next=NULL; *dtail=n; dtail=&n->next;
        }
        /* fill the ring with queued data; silence only so stale audio never repeats */
        for(;;){
            DWORD ahead=(DWORD)(w->written-w->played);
            DSNode* n=w->head;
            while(n && n->full) n=n->next;
            if(!n){
                if(ahead<w->lead) dsw_put(w,NULL,(w->lead-ahead)/w->block*w->block);
                break;
            }
            DWORD room=(w->size-w->block-ahead)/w->block*w->block;
            DWORD left=n->h->dwBufferLength-n->copied;
            DWORD k=left<room?left:room;
            if(!k) break;
            dsw_put(w,(const BYTE*)n->h->lpData+n->copied,k);
            n->copied+=k;
            if(n->copied<n->h->dwBufferLength) break;
            n->full=1; n->end=w->written;
        }
        LeaveCriticalSection(&w->cs);
        dsw_done(w,done);
    }
    return 0;
}
typedef struct { DWORD pid; HWND found; } WinSearch;
static BOOL CALLBACK dsw_enum(HWND h,LPARAM p){
    WinSearch* s=(WinSearch*)p; DWORD pid=0;
    GetWindowThreadProcessId(h,&pid);
    if(pid==s->pid && IsWindowVisible(h)){ s->found=h; return FALSE; }
    return TRUE;
}
static HWND dsw_window(void){
    WinSearch s={GetCurrentProcessId(),NULL};
    EnumWindows(dsw_enum,(LPARAM)&s);
    return s.found?s.found:GetDesktopWindow();
}
#ifdef DSW_DEBUG
static void dswlog(const char* what,const WAVEFORMATEX* f,const void* w){
    char b[160];
    wsprintfA(b,"%lu %s rate=%lu ch=%u bits=%u -> %p\r\n",GetTickCount(),what,
        f?f->nSamplesPerSec:0,f?f->nChannels:0,f?f->wBitsPerSample:0,w);
    logto("dsw_debug.log",b);
}
#else
#define dswlog(what,f,w) ((void)0)
#endif
/* A DirectSound stream playing silence until data is queued. */
static DSWave* dsw_create(LPCWAVEFORMATEX fmt){
    HMODULE m=LoadLibraryA("dsound.dll");
    DSCreate_t create=m?(DSCreate_t)GetProcAddress(m,"DirectSoundCreate"):NULL;
    DSWave* w=(DSWave*)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof *w);
    DSBUFFERDESC d;
    if(!create || !w) goto fail;
    if(FAILED(create(NULL,&w->ds,NULL))) goto fail;
    if(FAILED(IDirectSound_SetCooperativeLevel(w->ds,dsw_window(),DSSCL_NORMAL))) goto fail;
    w->block=fmt->nBlockAlign;
    w->size=fmt->nAvgBytesPerSec/w->block*w->block;                 /* one second */
    if(w->size<64*w->block) w->size=64*w->block;
    w->lead=w->size/4/w->block*w->block;
    ZeroMemory(&d,sizeof d); d.dwSize=sizeof d;
    d.dwFlags=DSBCAPS_GETCURRENTPOSITION2|DSBCAPS_GLOBALFOCUS
#ifdef DSW_ATTEN
        |DSBCAPS_CTRLVOLUME
#endif
        ;
    d.dwBufferBytes=w->size; d.lpwfxFormat=(LPWAVEFORMATEX)fmt;
    if(FAILED(IDirectSound_CreateSoundBuffer(w->ds,&d,&w->buf,NULL))) goto fail;
#ifdef DSW_ATTEN
    IDirectSoundBuffer_SetVolume(w->buf,DSW_ATTEN);
#endif
    w->fmt=*fmt; w->fmt.cbSize=0;
    w->silence=fmt->wBitsPerSample==8?0x80:0;
    w->magic=DSW_MAGIC;
    InitializeCriticalSection(&w->cs);
    w->wake=CreateEventA(NULL,FALSE,FALSE,NULL);
    dsw_put(w,NULL,w->size);                                        /* start from silence */
    w->written=0; w->wpos=0; w->lastPlay=0;
    IDirectSoundBuffer_SetCurrentPosition(w->buf,0);
    IDirectSoundBuffer_Play(w->buf,0,0,DSBPLAY_LOOPING);
    w->thread=CreateThread(NULL,0,dsw_thread,w,0,NULL);
    if(!w->thread){ IDirectSoundBuffer_Stop(w->buf); DeleteCriticalSection(&w->cs); CloseHandle(w->wake); w->magic=0; goto fail; }
    return w;
fail:
    if(w){
        if(w->buf) IDirectSoundBuffer_Release(w->buf);
        if(w->ds) IDirectSound_Release(w->ds);
        HeapFree(GetProcessHeap(),0,w);
    }
    return NULL;
}
/* Starting a new DirectSound stream while Miles is already playing silences
 * its running sounds (the engine loop), although sounds started afterwards
 * are fine. So one stream in the format of the soundtrack (44.1 kHz stereo
 * 16-bit) is opened early, before the first mission, keeps playing silence,
 * and is handed to the music DLL whenever it opens that format. */
static DSWave* volatile g_dswStanding=NULL;
static volatile LONG g_dswStandingBusy=0;
static void dsw_open_standing(void){
    static volatile LONG tried=0;
    if(InterlockedExchange(&tried,1)) return;
    WAVEFORMATEX f; ZeroMemory(&f,sizeof f);
    f.wFormatTag=WAVE_FORMAT_PCM; f.nChannels=2; f.nSamplesPerSec=44100; f.wBitsPerSample=16;
    f.nBlockAlign=4; f.nAvgBytesPerSec=44100*4;
    g_dswStanding=dsw_create(&f);
    dswlog("standing stream",&f,g_dswStanding);
}
static int same_format(const WAVEFORMATEX* a,const WAVEFORMATEX* b){
    return a->nChannels==b->nChannels && a->nSamplesPerSec==b->nSamplesPerSec
        && a->wBitsPerSample==b->wBitsPerSample && a->nBlockAlign==b->nBlockAlign;
}
static MMRESULT WINAPI DSW_waveOutOpen(LPHWAVEOUT ph,UINT dev,LPCWAVEFORMATEX fmt,DWORD_PTR cb,DWORD_PTR inst,DWORD fl){
    if(!fmt || fmt->wFormatTag!=WAVE_FORMAT_PCM || !fmt->nBlockAlign || !fmt->nAvgBytesPerSec || (fl&WAVE_FORMAT_QUERY))
        return g_rwOpen(ph,dev,fmt,cb,inst,fl);
    DSWave* w=g_dswStanding;
    if(w && same_format(&w->fmt,fmt) && !InterlockedExchange(&g_dswStandingBusy,1)){
        EnterCriticalSection(&w->cs);
        w->cb=cb; w->inst=inst; w->cbType=fl&CALLBACK_TYPEMASK;
        LeaveCriticalSection(&w->cs);
        dswlog("open (standing)",fmt,w);
    }else{
        w=dsw_create(fmt);
        dswlog("open (new)",fmt,w);
        if(!w) return g_rwOpen(ph,dev,fmt,cb,inst,fl);
        w->cb=cb; w->inst=inst; w->cbType=fl&CALLBACK_TYPEMASK;
    }
    if(ph) *ph=(HWAVEOUT)w;
    dsw_notify(w,WOM_OPEN,NULL);
    return MMSYSERR_NOERROR;
}
static DSWave* dsw(HWAVEOUT h){
    DSWave* w=(DSWave*)h;
    return (w && readable(w,sizeof(DWORD)) && w->magic==DSW_MAGIC)?w:NULL;
}
static MMRESULT WINAPI DSW_waveOutPrepareHeader(HWAVEOUT h,LPWAVEHDR hdr,UINT n){
    if(!dsw(h)) return g_rwPrep(h,hdr,n);
    if(!hdr) return MMSYSERR_INVALPARAM;
    hdr->dwFlags|=WHDR_PREPARED;
    return MMSYSERR_NOERROR;
}
static MMRESULT WINAPI DSW_waveOutUnprepareHeader(HWAVEOUT h,LPWAVEHDR hdr,UINT n){
    if(!dsw(h)) return g_rwUnprep(h,hdr,n);
    if(!hdr) return MMSYSERR_INVALPARAM;
    if(hdr->dwFlags&WHDR_INQUEUE) return WAVERR_STILLPLAYING;
    hdr->dwFlags&=~WHDR_PREPARED;
    return MMSYSERR_NOERROR;
}
static MMRESULT WINAPI DSW_waveOutWrite(HWAVEOUT h,LPWAVEHDR hdr,UINT n){
    DSWave* w=dsw(h);
    if(!w) return g_rwWrite(h,hdr,n);
    if(!hdr) return MMSYSERR_INVALPARAM;
    if(!(hdr->dwFlags&WHDR_PREPARED)) return WAVERR_UNPREPARED;
    DSNode* node=(DSNode*)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof *node);
    if(!node) return MMSYSERR_NOMEM;
    node->h=hdr;
    EnterCriticalSection(&w->cs);
    hdr->dwFlags=(hdr->dwFlags&~WHDR_DONE)|WHDR_INQUEUE;
    if(w->tail) w->tail->next=node; else w->head=node;
    w->tail=node;
    LeaveCriticalSection(&w->cs);
    SetEvent(w->wake);
    return MMSYSERR_NOERROR;
}
static MMRESULT WINAPI DSW_waveOutReset(HWAVEOUT h){
    DSWave* w=dsw(h);
    if(!w) return g_rwReset(h);
    EnterCriticalSection(&w->cs);
    DSNode* done=w->head; w->head=w->tail=NULL;
    for(DSNode* n=done;n;n=n->next) n->h->dwFlags=(n->h->dwFlags&~WHDR_INQUEUE)|WHDR_DONE;
    IDirectSoundBuffer_Stop(w->buf);
    dsw_put(w,NULL,w->size);                                        /* clear the whole ring */
    { DWORD play=0,wc=0; IDirectSoundBuffer_GetCurrentPosition(w->buf,&play,&wc);
      w->played+=(play+w->size-w->lastPlay)%w->size; w->lastPlay=play; w->wpos=play; }
    w->written=w->played;
    IDirectSoundBuffer_Play(w->buf,0,0,DSBPLAY_LOOPING);
    LeaveCriticalSection(&w->cs);
    dsw_done(w,done);
    return MMSYSERR_NOERROR;
}
static MMRESULT WINAPI DSW_waveOutClose(HWAVEOUT h){
    DSWave* w=dsw(h);
    if(!w) return g_rwClose(h);
    if(w->head) return WAVERR_STILLPLAYING;
    if(w==g_dswStanding){                /* keep it running for the next track */
        dsw_notify(w,WOM_CLOSE,NULL);
        EnterCriticalSection(&w->cs);
        w->cb=0; w->inst=0; w->cbType=CALLBACK_NULL;
        LeaveCriticalSection(&w->cs);
        dswlog("close (standing kept)",&w->fmt,w);
        InterlockedExchange(&g_dswStandingBusy,0);
        return MMSYSERR_NOERROR;
    }
    w->quit=1; SetEvent(w->wake);
    WaitForSingleObject(w->thread,2000);
    CloseHandle(w->thread); CloseHandle(w->wake);
    IDirectSoundBuffer_Stop(w->buf);
    IDirectSoundBuffer_Release(w->buf);
    IDirectSound_Release(w->ds);
    dsw_notify(w,WOM_CLOSE,NULL);
    w->magic=0;
    DeleteCriticalSection(&w->cs);
    HeapFree(GetProcessHeap(),0,w);
    return MMSYSERR_NOERROR;
}
/* Point one import of mod, found by name, at repl; returns the old target. */
static void* IATHookByName(HMODULE mod,const char* dll,const char* fn,void* repl){
    BYTE* base=(BYTE*)mod; IMAGE_DOS_HEADER* dos=(IMAGE_DOS_HEADER*)base;
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE) return NULL;
    IMAGE_NT_HEADERS* nt=(IMAGE_NT_HEADERS*)(base+dos->e_lfanew);
    IMAGE_DATA_DIRECTORY dir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if(!dir.VirtualAddress) return NULL;
    for(IMAGE_IMPORT_DESCRIPTOR* d=(IMAGE_IMPORT_DESCRIPTOR*)(base+dir.VirtualAddress);d->Name;d++){
        if(lstrcmpiA((const char*)base+d->Name,dll) || !d->OriginalFirstThunk) continue;
        IMAGE_THUNK_DATA* ft=(IMAGE_THUNK_DATA*)(base+d->FirstThunk);
        IMAGE_THUNK_DATA* ot=(IMAGE_THUNK_DATA*)(base+d->OriginalFirstThunk);
        for(;ot->u1.AddressOfData;ot++,ft++){
            if(IMAGE_SNAP_BY_ORDINAL(ot->u1.Ordinal)) continue;
            IMAGE_IMPORT_BY_NAME* n=(IMAGE_IMPORT_BY_NAME*)(base+ot->u1.AddressOfData);
            if(lstrcmpA((const char*)n->Name,fn)) continue;
            void* old=(void*)ft->u1.Function; DWORD op;
            if(old==repl) return NULL;
            if(!VirtualProtect(&ft->u1.Function,sizeof(void*),PAGE_READWRITE,&op)) return NULL;
            ft->u1.Function=(DWORD_PTR)repl;
            VirtualProtect(&ft->u1.Function,sizeof(void*),op,&op);
            return old;
        }
    }
    return NULL;
}
/* Move the music DLL's output to DirectSound. The waveOut it was bound to
 * stays the fallback: the system one, or winmmsys.dll in a patched copy. */
static void music_through_dsound(HMODULE ogg){
    static HMODULE volatile done=NULL;
    if(!ogg || InterlockedCompareExchangePointer((PVOID volatile*)&done,ogg,NULL)!=NULL) return;
    static const struct { const char* fn; void* repl; } t[6]={
        {"waveOutOpen",(void*)DSW_waveOutOpen},
        {"waveOutPrepareHeader",(void*)DSW_waveOutPrepareHeader},
        {"waveOutUnprepareHeader",(void*)DSW_waveOutUnprepareHeader},
        {"waveOutWrite",(void*)DSW_waveOutWrite},
        {"waveOutReset",(void*)DSW_waveOutReset},
        {"waveOutClose",(void*)DSW_waveOutClose}};
    void** real[6]={(void**)&g_rwOpen,(void**)&g_rwPrep,(void**)&g_rwUnprep,
                    (void**)&g_rwWrite,(void**)&g_rwReset,(void**)&g_rwClose};
    /* the fallbacks must be in place before the first call can arrive */
    HMODULE sys=g_sysWinmm?g_sysWinmm:GetModuleHandleA("winmm.dll");
    for(int i=0;i<6;i++){
        *real[i]=(void*)GetProcAddress(sys,t[i].fn);
        if(!*real[i]) return;
    }
    for(int i=0;i<6;i++){
        void* old=IATHookByName(ogg,"winmm.dll",t[i].fn,t[i].repl);
        if(old) *real[i]=old;
    }
    dsw_open_standing();
}

static void route_cd_audio(const char* mod){
    HMODULE m=NULL;
    if(!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,mod,&m) || !m) return;
    HMODULE ogg=music_dll();
    if(g_oggPrimary) music_through_dsound(g_oggPrimary);
    if(!ogg) return;
    music_through_dsound(ogg);
    for(unsigned i=0;i<CD_FUNCS;i++){
        FARPROC from=GetProcAddress(g_sysWinmm,g_cdFuncs[i]);
        FARPROC to=GetProcAddress(ogg,g_cdFuncs[i]);
        if(from && to && from!=to) IATHookByAddr(m,(void*)from,(void*)to);
    }
}

/* Applying the heap hooks is idempotent: IATHookByAddr only matches the
 * untouched API address, so a second pass over an already-patched table
 * changes nothing. That lets both the loader hook and the backstop poll call
 * this freely. */
static void install_heap_hooks(void){
    hook_messagebox(NULL);              /* i82stubz.exe itself */
    hook_messagebox("i82sim.dll");      /* both are no-ops while unloaded */
    hook_messagebox("I82ShellDll.dll");
    widen_display_modes();
    route_cd_audio("mss32.dll");
    route_cd_audio("I82ShellDll.dll");
    HMODULE s=NULL;
    if(!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           "i82sim.dll",&s) || !s){ g_i82Base=NULL; return; }
    g_i82Base=(BYTE*)s;
    IATHookByAddr(s,(void*)g_realHeapSize,   (void*)Hooked_HeapSize);
    IATHookByAddr(s,(void*)g_realHeapReAlloc,(void*)Hooked_HeapReAlloc);
    IATHookByAddr(s,(void*)g_realHeapFree,   (void*)Hooked_HeapFree);
}

typedef HMODULE (WINAPI *LLA_t)(LPCSTR);
typedef HMODULE (WINAPI *LLW_t)(LPCWSTR);
typedef HMODULE (WINAPI *LLExA_t)(LPCSTR,HANDLE,DWORD);
typedef HMODULE (WINAPI *LLExW_t)(LPCWSTR,HANDLE,DWORD);
static LLA_t   g_realLLA=NULL;
static LLW_t   g_realLLW=NULL;
static LLExA_t g_realLLExA=NULL;
static LLExW_t g_realLLExW=NULL;

/* The error the game reports on a failed start is "Where is shell dll?", so
 * the first thing to establish is whether the load itself returns null and
 * with what error -- everything else is guesswork until that is known. The
 * last error is read before the log call, which would clobber it. */
static HMODULE WINAPI Hooked_LLA(LPCSTR f){
    InterlockedIncrement(&g_inLoad);
    HMODULE m=g_realLLA(f);
    DWORD e=m?0:GetLastError();
    InterlockedDecrement(&g_inLoad);
#ifdef I82_DIAG
    dlog("LoadLibraryA",f,(DWORD)(DWORD_PTR)m,e);
#else
    (void)e;
#endif
    if(m) install_heap_hooks(); return m;
}
static HMODULE WINAPI Hooked_LLW(LPCWSTR f){
    InterlockedIncrement(&g_inLoad);
    HMODULE m=g_realLLW(f);
    DWORD e=m?0:GetLastError();
    InterlockedDecrement(&g_inLoad);
#ifdef I82_DIAG
    dlog("LoadLibraryW","(wide)",(DWORD)(DWORD_PTR)m,e);
#else
    (void)e;
#endif
    if(m) install_heap_hooks(); return m;
}
static HMODULE WINAPI Hooked_LLExA(LPCSTR f,HANDLE h,DWORD fl){
    InterlockedIncrement(&g_inLoad);
    HMODULE m=g_realLLExA(f,h,fl);
    DWORD e=m?0:GetLastError();
    InterlockedDecrement(&g_inLoad);
#ifdef I82_DIAG
    dlog("LoadLibraryExA",f,(DWORD)(DWORD_PTR)m,e);
#else
    (void)e;
#endif
    if(m) install_heap_hooks(); return m;
}
static HMODULE WINAPI Hooked_LLExW(LPCWSTR f,HANDLE h,DWORD fl){
    InterlockedIncrement(&g_inLoad);
    HMODULE m=g_realLLExW(f,h,fl);
    DWORD e=m?0:GetLastError();
    InterlockedDecrement(&g_inLoad);
#ifdef I82_DIAG
    dlog("LoadLibraryExW","(wide)",(DWORD)(DWORD_PTR)m,e);
#else
    (void)e;
#endif
    if(m) install_heap_hooks(); return m;
}

/* i82stubz.exe is what pulls in i82sim.dll, so its import table is where the
 * loader calls must be caught. */
static void hook_loader(void){
    HMODULE exe=GetModuleHandleA(NULL);
    HMODULE k=GetModuleHandleA("kernel32.dll");
    if(!exe || !k) return;
    g_realLLA  =(LLA_t)  GetProcAddress(k,"LoadLibraryA");
    g_realLLW  =(LLW_t)  GetProcAddress(k,"LoadLibraryW");
    g_realLLExA=(LLExA_t)GetProcAddress(k,"LoadLibraryExA");
    g_realLLExW=(LLExW_t)GetProcAddress(k,"LoadLibraryExW");
    if(g_realLLA)   IATHookByAddr(exe,(void*)g_realLLA,  (void*)Hooked_LLA);
    if(g_realLLW)   IATHookByAddr(exe,(void*)g_realLLW,  (void*)Hooked_LLW);
    if(g_realLLExA) IATHookByAddr(exe,(void*)g_realLLExA,(void*)Hooked_LLExA);
    if(g_realLLExW) IATHookByAddr(exe,(void*)g_realLLExW,(void*)Hooked_LLExW);
}

static DWORD WINAPI InstallThread(LPVOID p){ (void)p;
    HMODULE k=GetModuleHandleA("kernel32.dll");
    g_realHeapSize   =(HeapSize_t)   GetProcAddress(k,"HeapSize");
    g_realHeapReAlloc=(HeapReAlloc_t)GetProcAddress(k,"HeapReAlloc");
    g_realHeapFree   =(HeapFree_t)   GetProcAddress(k,"HeapFree");
    HMODULE u=GetModuleHandleA("user32.dll");
    if(!u) u=LoadLibraryA("user32.dll");
    if(u) g_realMessageBoxA=(MessageBoxA_t)GetProcAddress(u,"MessageBoxA");
    /* I82 loads i82sim.dll when a mission starts and unloads it when the
     * mission ends, so every mission brings a pristine import table, and the
     * game begins filling its arena immediately after the load returns. Any
     * polling interval is therefore a window in which an unguarded dead
     * pointer can reach the real HeapSize -- which is exactly how a 200 ms
     * poll reintroduced the mission-start crash. Catch the load itself. */
    hook_loader();
    for(;;){                       /* backstop only */
        install_heap_hooks();
        Sleep(50);
    }
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
