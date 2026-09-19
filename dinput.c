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

/* ---- input-system popup suppression ------------------------------------
 * I82 reports binding problems through MessageBox, using the C++ method name
 * as the caption. They are developer diagnostics with no player-actionable
 * content; one appears between the menu and the load screen on every mission
 * start and has to be dismissed by hand. Answer them with IDOK and keep the
 * text in a log instead. */
static void mlog(const char* t){
    HANDLE h=CreateFileA("dinput_msgbox.log",FILE_APPEND_DATA,
        FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if(h!=INVALID_HANDLE_VALUE){DWORD w;WriteFile(h,t,(DWORD)lstrlenA(t),&w,NULL);CloseHandle(h);}
}

typedef int (WINAPI *MessageBoxA_t)(HWND,LPCSTR,LPCSTR,UINT);
static MessageBoxA_t g_realMessageBoxA=NULL;

static int WINAPI Hooked_MessageBoxA(HWND wnd,LPCSTR text,LPCSTR caption,UINT type){
    if(caption && readable(caption,6) && !memcmp(caption,"CInput",6)){
        char b[512],*p=b;
        const char* t="[swallowed] ";
        while(*t) *p++=*t++;
        for(const char* c=caption; *c && p<b+200; ) *p++=*c++;
        *p++=':'; *p++=' ';
        if(text && readable(text,1))
            for(const char* c=text; *c && p<b+480; c++) *p++=(*c=='\n'||*c=='\r')?' ':*c;
        *p++='\n'; *p=0;
        mlog(b);
        return IDOK;
    }
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
#define MODE_H_SEARCH 160          /* how far the height compare may sit away */

static int write_imm(DWORD* at,DWORD value){
    DWORD old;
    if(!VirtualProtect(at,4,PAGE_EXECUTE_READWRITE,&old)) return 0;
    *at=value;
    VirtualProtect(at,4,old,&old);
    return 1;
}

static int widen_modes_in(BYTE* base){
    if(!base) return 0;
    IMAGE_DOS_HEADER* dos=(IMAGE_DOS_HEADER*)base;
    if(!readable(base,0x40) || dos->e_magic!=IMAGE_DOS_SIGNATURE) return 0;
    IMAGE_NT_HEADERS* nt=(IMAGE_NT_HEADERS*)(base+dos->e_lfanew);
    if(!readable(nt,sizeof *nt) || nt->Signature!=IMAGE_NT_SIGNATURE) return 0;
    SIZE_T size=nt->OptionalHeader.SizeOfImage;
    if(size<128) return 0;

    int hits=0;
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

            if(write_imm((DWORD*)(w2+1+ol),MODE_NEW_W) && write_imm(himm,MODE_NEW_H))
                hits++;
            break;
        }
    }
    return hits;
}

/* Scanning a 4 MB image on every poll would be wasteful, and once rewritten
 * the pattern no longer matches anyway, so each module base is done once. */
static BYTE* g_wsDone[2]={NULL,NULL};
static void widen_module(const char* name,int slot){
    HMODULE m=NULL;
    if(!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,name,&m) || !m){
        g_wsDone[slot]=NULL; return;                      /* unloaded: re-arm */
    }
    if(g_wsDone[slot]==(BYTE*)m) return;
    g_wsDone[slot]=(BYTE*)m;
    widen_modes_in((BYTE*)m);
}
static void widen_display_modes(void){
    widen_module("i82sim.dll",0);
    widen_module("I82ShellDll.dll",1);
}

/* Applying the heap hooks is idempotent: IATHookByAddr only matches the
 * untouched API address, so a second pass over an already-patched table
 * changes nothing. That lets both the loader hook and the backstop poll call
 * this freely. */
static void install_heap_hooks(void){
    hook_messagebox("i82sim.dll");      /* both are no-ops while unloaded */
    hook_messagebox("I82ShellDll.dll");
    widen_display_modes();
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

static HMODULE WINAPI Hooked_LLA(LPCSTR f){
    HMODULE m=g_realLLA(f);   if(m) install_heap_hooks(); return m;
}
static HMODULE WINAPI Hooked_LLW(LPCWSTR f){
    HMODULE m=g_realLLW(f);   if(m) install_heap_hooks(); return m;
}
static HMODULE WINAPI Hooked_LLExA(LPCSTR f,HANDLE h,DWORD fl){
    HMODULE m=g_realLLExA(f,h,fl); if(m) install_heap_hooks(); return m;
}
static HMODULE WINAPI Hooked_LLExW(LPCWSTR f,HANDLE h,DWORD fl){
    HMODULE m=g_realLLExW(f,h,fl); if(m) install_heap_hooks(); return m;
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
