// ============================================================================
// Akai S5000 / S6000 Windows 11 64-Bit WinUSB Shim & UI Scaler
// Version: v13.3 Production Release (UI Scaler Active, Zero Logging Overhead)
// File: akS56USB_WinUSB_Shim.cpp -> Compiles to version.dll (x86)
// ============================================================================

#define _CRT_SECURE_NO_WARNINGS
#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <winioctl.h>
#include <setupapi.h>
#include <winusb.h>
#include <usb.h>
#include <cfgmgr32.h>
#include <initguid.h>
#include <intrin.h>
#include <stdio.h>
#include <math.h>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "winusb.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ole32.lib")

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((ULONG_PTR)0x00000000L)
#endif
#ifndef STATUS_UNSUCCESSFUL
#define STATUS_UNSUCCESSFUL ((ULONG_PTR)0xC0000001L)
#endif
#ifndef STATUS_PENDING
#define STATUS_PENDING ((ULONG_PTR)0x00000103L)
#endif

DEFINE_GUID(GUID_DEVINTERFACE_AKAI_S56, 0x9D4B6529L, 0xD4A6, 0x11D3, 0x8E, 0xAE, 0x00, 0x50, 0xDA, 0x2E, 0x2D, 0x53);
DEFINE_GUID(GUID_DEVINTERFACE_USB_DEVICE_DEF, 0xA5DCBF10L, 0x6530, 0x11D2, 0x90, 0x1F, 0x00, 0xC0, 0x4F, 0xB9, 0x51, 0xED);
DEFINE_GUID(GUID_DEVINTERFACE_WINUSB_DEF, 0x88BAE032L, 0x5A81, 0x49F0, 0xBC, 0x3D, 0xA4, 0xFF, 0x13, 0x82, 0x16, 0xD6);

static DWORD g_MainProcessId = 0;
static DWORD g_MainThreadId = 0;
static HMODULE g_hShimModule = NULL;

// ----------------------------------------------------------------------------
// Auto-Repair Collapsed Program Editor Registry Keys in HKCU\Software\AKSys\ak.Sys
// ----------------------------------------------------------------------------
static void InspectAndRepairRegistryKeyRecursive(HKEY hParent, int depth) {
    if (depth > 6) return;
    char valName[256];
    BYTE data[512];
    DWORD idx = 0;

    while (true) {
        DWORD valNameLen = sizeof(valName) - 1;
        DWORD dataLen = sizeof(data) - 1;
        DWORD type = 0;
        LONG res = RegEnumValueA(hParent, idx, valName, &valNameLen, NULL, &type, data, &dataLen);
        if (res != ERROR_SUCCESS) break;
        valName[valNameLen] = '\0';

        if (type == REG_DWORD && dataLen == sizeof(DWORD)) {
            DWORD val = *(DWORD*)data;
            char lowerName[256] = {0};
            strncpy(lowerName, valName, sizeof(lowerName) - 1);
            _strlwr(lowerName);
            if ((strstr(lowerName, "show") || strstr(lowerName, "view") || strstr(lowerName, "panel") ||
                 strstr(lowerName, "pane") || strstr(lowerName, "rack") || strstr(lowerName, "kg") ||
                 strstr(lowerName, "env") || strstr(lowerName, "lfo") || strstr(lowerName, "zone") ||
                 strstr(lowerName, "key") || strstr(lowerName, "filt") || strstr(lowerName, "theme")) && val == 0) {
                DWORD newVal = 1;
                RegSetValueExA(hParent, valName, 0, REG_DWORD, (const BYTE*)&newVal, sizeof(DWORD));
            }
        }
        idx++;
    }

    char subKeyName[256];
    DWORD subIdx = 0;
    while (true) {
        DWORD subLen = sizeof(subKeyName) - 1;
        LONG res = RegEnumKeyExA(hParent, subIdx, subKeyName, &subLen, NULL, NULL, NULL, NULL);
        if (res != ERROR_SUCCESS) break;
        subKeyName[subLen] = '\0';

        HKEY hSub = NULL;
        if (RegOpenKeyExA(hParent, subKeyName, 0, KEY_READ | KEY_WRITE, &hSub) == ERROR_SUCCESS) {
            InspectAndRepairRegistryKeyRecursive(hSub, depth + 1);
            RegCloseKey(hSub);
        }
        subIdx++;
    }
}

static void RepairAkaiProgramEditorRegistry(bool forceResetS56Layout) {
    HKEY hAkSys = NULL;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\AKSys\\ak.Sys", 0, KEY_READ | KEY_WRITE, &hAkSys) == ERROR_SUCCESS) {
        InspectAndRepairRegistryKeyRecursive(hAkSys, 0);

        if (forceResetS56Layout) {
            const char* subKeysToClear[] = {
                "S56\\Program Editor",
                "S56\\Themes",
                "S56\\Windows",
                "S56",
                "AKAI S6000\\Program Editor",
                "AKAI S6000",
                "Demo S56K 1",
                "Demo S56K 2"
            };
            for (int i = 0; i < (int)(sizeof(subKeysToClear) / sizeof(subKeysToClear[0])); i++) {
                RegDeleteKeyA(hAkSys, subKeysToClear[i]);
            }
        }
        RegCloseKey(hAkSys);
    }
}

static void EnableDpiAwareness() {
    HMODULE hUser = GetModuleHandleA("user32.dll");
    if (hUser) {
        typedef BOOL (WINAPI *PFN_SetProcessDPIAware)(VOID);
        PFN_SetProcessDPIAware pfnDPIAware = (PFN_SetProcessDPIAware)GetProcAddress(hUser, "SetProcessDPIAware");
        if (pfnDPIAware) {
            pfnDPIAware();
        }
    }
    HMODULE hShcore = LoadLibraryA("Shcore.dll");
    if (hShcore) {
        typedef HRESULT (WINAPI *PFN_SetProcessDpiAwareness)(int);
        PFN_SetProcessDpiAwareness pfnAwareness = (PFN_SetProcessDpiAwareness)GetProcAddress(hShcore, "SetProcessDpiAwareness");
        if (pfnAwareness) {
            pfnAwareness(2);
        }
    }
}

// ----------------------------------------------------------------------------
// WinUSB Hardware Layer (Rock-Solid v10.1 Engine)
// ----------------------------------------------------------------------------
#define MAX_AKAI_HANDLES 64
#define AKAI_HANDLE_BASE 0x414B5300
#define AKAI_READ_BUFFER_SIZE 65536

enum AkaiHandleType {
    HANDLE_TYPE_UNUSED = 0,
    HANDLE_TYPE_ROOT,
    HANDLE_TYPE_PIPE0,
    HANDLE_TYPE_PIPE1
};

struct AkaiHandleContext {
    AkaiHandleType type;
    char path[256];
    BYTE readBuffer[AKAI_READ_BUFFER_SIZE];
    DWORD readLen;
    OVERLAPPED winusbOv;
    HANDLE hEvent;
    volatile bool readPending;
    LPVOID pUserDest;
    DWORD userReqLen;
    LPOVERLAPPED pUserOv;
};

static CRITICAL_SECTION g_UsbLock;
static HANDLE g_hUsbDevice = INVALID_HANDLE_VALUE;
static WINUSB_INTERFACE_HANDLE g_hWinUsb = NULL;
static char g_WinUsbDevicePath[MAX_PATH] = {0};
static UCHAR g_BulkInPipe = 0x82;
static UCHAR g_BulkOutPipe = 0x02;
static DWORD g_LastBytesTransferred = 0;
static volatile bool g_S56Hooked = false;
static AkaiHandleContext g_Handles[MAX_AKAI_HANDLES];
static LONG g_HandleCounter = 0;

static const char* FAKE_AKAI_PATH = "\\\\?\\usb#vid_09e8&pid_005e#aks56usb#{9d4b6529-d4a6-11d3-8eae-0050da2e2d53}";

static bool IsAkaiHandle(HANDLE h) {
    ULONG_PTR val = (ULONG_PTR)h;
    return (val >= AKAI_HANDLE_BASE && val < (AKAI_HANDLE_BASE + MAX_AKAI_HANDLES));
}

static int GetAkaiHandleIndex(HANDLE h) {
    if (!IsAkaiHandle(h)) return -1;
    return (int)((ULONG_PTR)h - AKAI_HANDLE_BASE);
}

static HANDLE AllocateAkaiHandle(AkaiHandleType type, const char* path) {
    EnterCriticalSection(&g_UsbLock);
    for (int step = 0; step < MAX_AKAI_HANDLES; step++) {
        int i = (InterlockedIncrement(&g_HandleCounter) & 0x7FFFFFFF) % MAX_AKAI_HANDLES;
        if (g_Handles[i].type == HANDLE_TYPE_UNUSED) {
            g_Handles[i].type = type;
            if (path) strncpy(g_Handles[i].path, path, sizeof(g_Handles[i].path) - 1);
            else g_Handles[i].path[0] = 0;
            g_Handles[i].readLen = 0;
            g_Handles[i].readPending = false;
            g_Handles[i].pUserDest = NULL;
            g_Handles[i].userReqLen = 0;
            g_Handles[i].pUserOv = NULL;
            if (!g_Handles[i].hEvent) {
                g_Handles[i].hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
                memset(&g_Handles[i].winusbOv, 0, sizeof(OVERLAPPED));
                g_Handles[i].winusbOv.hEvent = g_Handles[i].hEvent;
            } else {
                ResetEvent(g_Handles[i].hEvent);
            }
            HANDLE hResult = (HANDLE)(ULONG_PTR)(AKAI_HANDLE_BASE + i);
            LeaveCriticalSection(&g_UsbLock);
            return hResult;
        }
    }
    LeaveCriticalSection(&g_UsbLock);
    return INVALID_HANDLE_VALUE;
}

static void FreeAkaiHandle(HANDLE h) {
    int idx = GetAkaiHandleIndex(h);
    if (idx < 0) return;
    EnterCriticalSection(&g_UsbLock);
    if (g_Handles[idx].readPending && g_hWinUsb) {
        g_Handles[idx].readPending = false;
        WinUsb_AbortPipe(g_hWinUsb, g_BulkInPipe);
    }
    g_Handles[idx].type = HANDLE_TYPE_UNUSED;
    g_Handles[idx].readPending = false;
    g_Handles[idx].readLen = 0;
    g_Handles[idx].pUserDest = NULL;
    g_Handles[idx].pUserOv = NULL;
    LeaveCriticalSection(&g_UsbLock);
}

static bool ConfigureWinUsbInterface(HANDLE hDev, const char* devPath) {
    WINUSB_INTERFACE_HANDLE hWinUsb = NULL;
    if (!WinUsb_Initialize(hDev, &hWinUsb)) {
        return false;
    }

    USB_INTERFACE_DESCRIPTOR ifaceDesc;
    if (WinUsb_QueryInterfaceSettings(hWinUsb, 0, &ifaceDesc)) {
        for (UCHAR p = 0; p < ifaceDesc.bNumEndpoints; p++) {
            WINUSB_PIPE_INFORMATION pipeInfo;
            if (WinUsb_QueryPipe(hWinUsb, 0, p, &pipeInfo)) {
                ULONG timeout = 5000;
                UCHAR autoClear = 1;
                UCHAR rawIo = 0;
                UCHAR shortPacket = 1;
                WinUsb_SetPipePolicy(hWinUsb, pipeInfo.PipeId, PIPE_TRANSFER_TIMEOUT, sizeof(ULONG), &timeout);
                WinUsb_SetPipePolicy(hWinUsb, pipeInfo.PipeId, AUTO_CLEAR_STALL, sizeof(UCHAR), &autoClear);
                WinUsb_SetPipePolicy(hWinUsb, pipeInfo.PipeId, RAW_IO, sizeof(UCHAR), &rawIo);
                WinUsb_SetPipePolicy(hWinUsb, pipeInfo.PipeId, SHORT_PACKET_TERMINATE, sizeof(UCHAR), &shortPacket);
            }
        }
    }

    g_BulkInPipe = 0x82;
    g_BulkOutPipe = 0x02;
    WinUsb_FlushPipe(hWinUsb, g_BulkInPipe);

    g_hWinUsb = hWinUsb;
    g_hUsbDevice = hDev;
    strncpy(g_WinUsbDevicePath, devPath, MAX_PATH - 1);
    return true;
}

static bool TryDirectPathOpen(const char* candidatePath) {
    HANDLE hDev = CreateFileA(candidatePath, GENERIC_WRITE | GENERIC_READ,
                              FILE_SHARE_WRITE | FILE_SHARE_READ, NULL,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);
    if (hDev != INVALID_HANDLE_VALUE) {
        if (ConfigureWinUsbInterface(hDev, candidatePath)) {
            return true;
        }
        CloseHandle(hDev);
    }
    return false;
}

static bool ScanAllUsbDevicesUniversal() {
    HDEVINFO hDevInfo = SetupDiGetClassDevsA(NULL, "USB", NULL, DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (hDevInfo == INVALID_HANDLE_VALUE) return false;

    SP_DEVINFO_DATA devData;
    devData.cbSize = sizeof(SP_DEVINFO_DATA);

    for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devData); i++) {
        char hwId[512] = {0};
        if (SetupDiGetDeviceRegistryPropertyA(hDevInfo, &devData, SPDRP_HARDWAREID, NULL, (PBYTE)hwId, sizeof(hwId) - 1, NULL)) {
            _strlwr(hwId);
            if (strstr(hwId, "vid_09e8") != NULL) {
                char instanceId[MAX_PATH] = {0};
                if (SetupDiGetDeviceInstanceIdA(hDevInfo, &devData, instanceId, sizeof(instanceId) - 1, NULL)) {
                    char symInstance[MAX_PATH] = {0};
                    strncpy(symInstance, instanceId, sizeof(symInstance) - 1);
                    for (int c = 0; symInstance[c]; c++) {
                        if (symInstance[c] == '\\') symInstance[c] = '#';
                    }

                    const char* guidStrings[] = {
                        "{a5dcbf10-6530-11d2-901f-00c04fb951ed}",
                        "{88bae032-5a81-49f0-bc3d-a4ff138216d6}",
                        "{9d4b6529-d4a6-11d3-8eae-0050da2e2d53}"
                    };

                    for (int g = 0; g < 3; g++) {
                        char candidatePath[MAX_PATH];
                        snprintf(candidatePath, sizeof(candidatePath), "\\\\?\\%s#%s", symInstance, guidStrings[g]);
                        if (TryDirectPathOpen(candidatePath)) {
                            SetupDiDestroyDeviceInfoList(hDevInfo);
                            return true;
                        }
                    }
                }
            }
        }
    }
    SetupDiDestroyDeviceInfoList(hDevInfo);
    return false;
}

static bool OpenAkaiWinUsbDevice() {
    EnterCriticalSection(&g_UsbLock);
    if (g_hWinUsb != NULL) {
        LeaveCriticalSection(&g_UsbLock);
        return true;
    }
    if (ScanAllUsbDevicesUniversal()) {
        LeaveCriticalSection(&g_UsbLock);
        return true;
    }
    LeaveCriticalSection(&g_UsbLock);
    return false;
}

static BOOL CompleteIoOperation(LPOVERLAPPED lpOverlapped, DWORD bytesTransferred, BOOL ok) {
    g_LastBytesTransferred = bytesTransferred;
    if (lpOverlapped) {
        lpOverlapped->Internal = ok ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
        lpOverlapped->InternalHigh = bytesTransferred;
        if (lpOverlapped->hEvent) {
            SetEvent(lpOverlapped->hEvent);
        }
    }
    SetLastError(ok ? ERROR_SUCCESS : ERROR_GEN_FAILURE);
    return ok;
}

#define FAKE_HDEVINFO ((HDEVINFO)(ULONG_PTR)0x414B0001)

typedef HDEVINFO (WINAPI *PFN_SetupDiGetClassDevsA)(const GUID*, PCSTR, HWND, DWORD);
typedef BOOL (WINAPI *PFN_SetupDiEnumDeviceInterfaces)(HDEVINFO, PSP_DEVINFO_DATA, const GUID*, DWORD, PSP_DEVICE_INTERFACE_DATA);
typedef BOOL (WINAPI *PFN_SetupDiGetDeviceInterfaceDetailA)(HDEVINFO, PSP_DEVICE_INTERFACE_DATA, PSP_DEVICE_INTERFACE_DETAIL_DATA_A, DWORD, PDWORD, PSP_DEVINFO_DATA);

static PFN_SetupDiGetClassDevsA g_realSetupDiGetClassDevsA = NULL;
static PFN_SetupDiEnumDeviceInterfaces g_realSetupDiEnumDeviceInterfaces = NULL;
static PFN_SetupDiGetDeviceInterfaceDetailA g_realSetupDiGetDeviceInterfaceDetailA = NULL;

static bool InstallTrampolineHook(void* targetFunc, void* hookFunc, void** origTrampoline) {
    if (!targetFunc || !hookFunc) return false;
    BYTE* pTarget = (BYTE*)targetFunc;

    if (pTarget[0] != 0x8B || pTarget[1] != 0xFF || pTarget[2] != 0x55 || pTarget[3] != 0x8B || pTarget[4] != 0xEC) {
        return false;
    }

    BYTE* pTrampoline = (BYTE*)VirtualAlloc(NULL, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!pTrampoline) return false;

    memcpy(pTrampoline, pTarget, 5);
    pTrampoline[5] = 0xE9;
    *(DWORD*)(pTrampoline + 6) = (DWORD)((pTarget + 5) - (pTrampoline + 10));

    DWORD oldProt;
    if (!VirtualProtect(pTarget, 5, PAGE_EXECUTE_READWRITE, &oldProt)) {
        VirtualFree(pTrampoline, 0, MEM_RELEASE);
        return false;
    }

    pTarget[0] = 0xE9;
    *(DWORD*)(pTarget + 1) = (DWORD)((BYTE*)hookFunc - (pTarget + 5));
    VirtualProtect(pTarget, 5, oldProt, &oldProt);

    if (origTrampoline) *origTrampoline = pTrampoline;
    return true;
}

static HDEVINFO WINAPI Hook_SetupDiGetClassDevsA(const GUID* g, PCSTR e, HWND h, DWORD f) {
    if (g && (g->Data1 == 0x9D4B6529L || memcmp(g, &GUID_DEVINTERFACE_AKAI_S56, sizeof(GUID)) == 0)) {
        OpenAkaiWinUsbDevice();
        return FAKE_HDEVINFO;
    }
    if (g_realSetupDiGetClassDevsA) return g_realSetupDiGetClassDevsA(g, e, h, f);
    return SetupDiGetClassDevsA(g, e, h, f);
}

static BOOL WINAPI Hook_SetupDiEnumDeviceInterfaces(HDEVINFO s, PSP_DEVINFO_DATA d, const GUID* i, DWORD m, PSP_DEVICE_INTERFACE_DATA id) {
    if (s == FAKE_HDEVINFO) {
        if (m == 0) {
            if (id) {
                id->InterfaceClassGuid = GUID_DEVINTERFACE_AKAI_S56;
                id->Flags = SPINT_ACTIVE;
                id->Reserved = 0x414B;
            }
            return TRUE;
        }
        SetLastError(ERROR_NO_MORE_ITEMS);
        return FALSE;
    }
    if (g_realSetupDiEnumDeviceInterfaces) return g_realSetupDiEnumDeviceInterfaces(s, d, i, m, id);
    return SetupDiEnumDeviceInterfaces(s, d, i, m, id);
}

static BOOL WINAPI Hook_SetupDiGetDeviceInterfaceDetailA(HDEVINFO s, PSP_DEVICE_INTERFACE_DATA id,
                                                        PSP_DEVICE_INTERFACE_DETAIL_DATA_A det,
                                                        DWORD sz, PDWORD req, PSP_DEVINFO_DATA d) {
    if (s == FAKE_HDEVINFO) {
        DWORD needed = (DWORD)(FIELD_OFFSET(SP_DEVICE_INTERFACE_DETAIL_DATA_A, DevicePath) + strlen(FAKE_AKAI_PATH) + 1);
        if (req) *req = needed;
        if (!det || sz < needed) {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return FALSE;
        }
        strcpy(det->DevicePath, FAKE_AKAI_PATH);
        return TRUE;
    }
    if (g_realSetupDiGetDeviceInterfaceDetailA) return g_realSetupDiGetDeviceInterfaceDetailA(s, id, det, sz, req, d);
    return SetupDiGetDeviceInterfaceDetailA(s, id, det, sz, req, d);
}

static BOOL WINAPI Hook_SetupDiDestroyDeviceInfoList(HDEVINFO s) {
    if (s == FAKE_HDEVINFO) return TRUE;
    return SetupDiDestroyDeviceInfoList(s);
}

static HANDLE WINAPI Hook_CreateFileA(LPCSTR name, DWORD acc, DWORD share, LPSECURITY_ATTRIBUTES sec,
                                     DWORD disp, DWORD flags, HANDLE templ) {
    if (name && (strstr(name, "aks56") || strstr(name, "vid_09e8"))) {
        OpenAkaiWinUsbDevice();
        if (strstr(name, "PIPE00") || strstr(name, "pipe00")) {
            return AllocateAkaiHandle(HANDLE_TYPE_PIPE0, name);
        }
        if (strstr(name, "PIPE01") || strstr(name, "pipe01")) {
            return AllocateAkaiHandle(HANDLE_TYPE_PIPE1, name);
        }
        return AllocateAkaiHandle(HANDLE_TYPE_ROOT, name);
    }
    return CreateFileA(name, acc, share, sec, disp, flags, templ);
}

static void CompletePendingReadForContext(AkaiHandleContext* ctx, DWORD gotBytes, BOOL ok) {
    ctx->readPending = false;
    ResetEvent(ctx->hEvent);
    g_LastBytesTransferred = gotBytes;

    if (ok && gotBytes > 0) {
        if (ctx->pUserDest) {
            DWORD copyLen = (ctx->userReqLen < gotBytes) ? ctx->userReqLen : gotBytes;
            memcpy(ctx->pUserDest, ctx->readBuffer, copyLen);
        }
    }

    LPOVERLAPPED uOv = ctx->pUserOv;
    ctx->pUserDest = NULL;
    ctx->pUserOv = NULL;

    if (uOv) {
        uOv->Internal = ok ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
        uOv->InternalHigh = ok ? gotBytes : 0;
        if (uOv->hEvent) {
            SetEvent(uOv->hEvent);
        }
    }
}

static DWORD WINAPI AsyncReadMonitorThread(LPVOID lpParam) {
    int idx = (int)(ULONG_PTR)lpParam;
    if (idx < 0 || idx >= MAX_AKAI_HANDLES) return 0;
    AkaiHandleContext* ctx = &g_Handles[idx];

    while (ctx->readPending && g_hWinUsb) {
        DWORD waitRes = WaitForSingleObject(ctx->hEvent, 100);
        if (!ctx->readPending) break;
        if (waitRes == WAIT_OBJECT_0) {
            ULONG got = 0;
            BOOL ok = WinUsb_GetOverlappedResult(g_hWinUsb, &ctx->winusbOv, &got, FALSE);
            CompletePendingReadForContext(ctx, got, ok);
            break;
        }
    }
    return 0;
}

static BOOL WINAPI Hook_WriteFile(HANDLE hFile, LPCVOID buf, DWORD len, LPDWORD written, LPOVERLAPPED ov) {
    if (!IsAkaiHandle(hFile)) return WriteFile(hFile, buf, len, written, ov);

    if (ov) {
        ov->Internal = STATUS_PENDING;
        ov->InternalHigh = 0;
        if (ov->hEvent) ResetEvent(ov->hEvent);
    }

    ULONG transferred = 0;
    BOOL ok = FALSE;

    if (g_hWinUsb) {
        OVERLAPPED localOv;
        memset(&localOv, 0, sizeof(localOv));
        localOv.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

        ok = WinUsb_WritePipe(g_hWinUsb, g_BulkOutPipe, (PUCHAR)buf, len, &transferred, &localOv);
        if (!ok && GetLastError() == ERROR_IO_PENDING) {
            ok = WinUsb_GetOverlappedResult(g_hWinUsb, &localOv, &transferred, TRUE);
        }
        CloseHandle(localOv.hEvent);
    }

    if (written) *written = len;
    return CompleteIoOperation(ov, len, ok);
}

static BOOL WINAPI Hook_ReadFile(HANDLE hFile, LPVOID buf, DWORD len, LPDWORD readBytes, LPOVERLAPPED ov) {
    if (!IsAkaiHandle(hFile)) return ReadFile(hFile, buf, len, readBytes, ov);

    int idx = GetAkaiHandleIndex(hFile);
    if (idx < 0 || !g_hWinUsb) {
        if (readBytes) *readBytes = 0;
        return CompleteIoOperation(ov, 0, FALSE);
    }

    if (ov) {
        ov->Internal = STATUS_PENDING;
        ov->InternalHigh = 0;
        if (ov->hEvent) ResetEvent(ov->hEvent);
    }

    AkaiHandleContext* ctx = &g_Handles[idx];
    ULONG transferred = 0;
    BOOL ok = FALSE;

    if (ctx->readPending) {
        DWORD waitRes = WaitForSingleObject(ctx->hEvent, 20);
        if (waitRes == WAIT_OBJECT_0) {
            ok = WinUsb_GetOverlappedResult(g_hWinUsb, &ctx->winusbOv, &transferred, FALSE);
            ctx->readPending = false;
            ResetEvent(ctx->hEvent);
        } else {
            ctx->pUserDest = buf;
            ctx->userReqLen = len;
            ctx->pUserOv = ov;
            if (readBytes) *readBytes = 0;
            SetLastError(ERROR_IO_PENDING);
            return FALSE;
        }
    }

    if (!ctx->readPending && !ok) {
        ULONG readReq = (len > AKAI_READ_BUFFER_SIZE) ? AKAI_READ_BUFFER_SIZE : len;

        ResetEvent(ctx->hEvent);
        memset(&ctx->winusbOv, 0, sizeof(OVERLAPPED));
        ctx->winusbOv.hEvent = ctx->hEvent;

        ok = WinUsb_ReadPipe(g_hWinUsb, g_BulkInPipe, ctx->readBuffer, readReq, &transferred, &ctx->winusbOv);
        DWORD err = GetLastError();

        if (!ok && err == ERROR_IO_PENDING) {
            DWORD initialWait = (len >= 512) ? 500 : 25;
            DWORD waitRes = WaitForSingleObject(ctx->hEvent, initialWait);
            if (waitRes == WAIT_OBJECT_0) {
                ok = WinUsb_GetOverlappedResult(g_hWinUsb, &ctx->winusbOv, &transferred, FALSE);
                ctx->readPending = false;
                ResetEvent(ctx->hEvent);
            } else {
                ctx->pUserDest = buf;
                ctx->userReqLen = len;
                ctx->pUserOv = ov;
                ctx->readPending = true;
                HANDLE hTh = CreateThread(NULL, 0, AsyncReadMonitorThread, (LPVOID)(ULONG_PTR)idx, 0, NULL);
                if (hTh) CloseHandle(hTh);
                if (readBytes) *readBytes = 0;
                SetLastError(ERROR_IO_PENDING);
                return FALSE;
            }
        }

        while (ok && transferred < readReq && readReq >= 512 && (transferred % 64) == 0) {
            ULONG chunkGot = 0;
            ResetEvent(ctx->hEvent);
            memset(&ctx->winusbOv, 0, sizeof(OVERLAPPED));
            ctx->winusbOv.hEvent = ctx->hEvent;

            BOOL chunkOk = WinUsb_ReadPipe(g_hWinUsb, g_BulkInPipe,
                                           ctx->readBuffer + transferred,
                                           readReq - transferred,
                                           &chunkGot, &ctx->winusbOv);
            if (!chunkOk && GetLastError() == ERROR_IO_PENDING) {
                if (WaitForSingleObject(ctx->hEvent, 500) == WAIT_OBJECT_0) {
                    chunkOk = WinUsb_GetOverlappedResult(g_hWinUsb, &ctx->winusbOv, &chunkGot, FALSE);
                } else {
                    WinUsb_AbortPipe(g_hWinUsb, g_BulkInPipe);
                    break;
                }
            }
            if (!chunkOk || chunkGot == 0) break;
            transferred += chunkGot;
            if ((chunkGot % 64) != 0) break;
        }
    }

    if (ok && transferred > 0) {
        DWORD copyLen = (len < transferred) ? len : transferred;
        memcpy(buf, ctx->readBuffer, copyLen);

        if (readBytes) *readBytes = copyLen;
        CompleteIoOperation(ov, copyLen, TRUE);

        if (ov) {
            SetLastError(ERROR_IO_PENDING);
            return FALSE;
        }
        return TRUE;
    }

    if (readBytes) *readBytes = 0;
    if (ov) {
        SetLastError(ERROR_IO_PENDING);
        return FALSE;
    }

    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

static BOOL WINAPI Hook_GetOverlappedResult(HANDLE hFile, LPOVERLAPPED ov, LPDWORD trans, BOOL wait) {
    if (IsAkaiHandle(hFile)) {
        int idx = GetAkaiHandleIndex(hFile);

        if (idx >= 0 && g_Handles[idx].readPending && g_Handles[idx].hEvent) {
            DWORD waitTime = wait ? 4000 : 0;
            DWORD waitRes = WaitForSingleObject(g_Handles[idx].hEvent, waitTime);
            if (waitRes == WAIT_OBJECT_0) {
                ULONG got = 0;
                if (WinUsb_GetOverlappedResult(g_hWinUsb, &g_Handles[idx].winusbOv, &got, FALSE)) {
                    CompletePendingReadForContext(&g_Handles[idx], got, TRUE);
                    if (trans) *trans = got;
                    SetLastError(ERROR_SUCCESS);
                    return TRUE;
                }
            } else {
                if (trans) *trans = 0;
                SetLastError(ERROR_IO_INCOMPLETE);
                return FALSE;
            }
        }

        if (ov && ov->Internal == STATUS_PENDING) {
            if (trans) *trans = 0;
            SetLastError(ERROR_IO_INCOMPLETE);
            return FALSE;
        }

        DWORD bytes = ov ? (DWORD)ov->InternalHigh : g_LastBytesTransferred;
        BOOL ok = (ov == NULL) || (ov->Internal == STATUS_SUCCESS);
        if (trans) *trans = bytes;
        SetLastError(ok ? ERROR_SUCCESS : ERROR_GEN_FAILURE);
        return ok;
    }
    return GetOverlappedResult(hFile, ov, trans, wait);
}

static BOOL WINAPI Hook_DeviceIoControl(HANDLE hDev, DWORD code, LPVOID inBuf, DWORD inSz,
                                       LPVOID outBuf, DWORD outSz, LPDWORD ret, LPOVERLAPPED ov) {
    if (!IsAkaiHandle(hDev)) return DeviceIoControl(hDev, code, inBuf, inSz, outBuf, outSz, ret, ov);

    int idx = GetAkaiHandleIndex(hDev);
    if (code == 0x00220008 || code == 0x220000 || code == 0x220004) {
        if (idx >= 0 && g_Handles[idx].type == HANDLE_TYPE_PIPE0 && g_hWinUsb) {
            g_Handles[idx].readPending = false;
            WinUsb_AbortPipe(g_hWinUsb, g_BulkInPipe);
        }
        if (outBuf && outSz > 0) {
            memset(outBuf, 0, outSz);
        }
        if (ret) *ret = 0;
        return CompleteIoOperation(ov, 0, TRUE);
    }

    if (ret) *ret = 0;
    return CompleteIoOperation(ov, 0, TRUE);
}

static BOOL WINAPI Hook_CancelIo(HANDLE hFile) {
    if (IsAkaiHandle(hFile)) {
        int idx = GetAkaiHandleIndex(hFile);
        if (idx >= 0 && g_Handles[idx].readPending && g_hWinUsb) {
            g_Handles[idx].readPending = false;
            WinUsb_AbortPipe(g_hWinUsb, g_BulkInPipe);
        }
        return TRUE;
    }
    return CancelIo(hFile);
}

static BOOL WINAPI Hook_CloseHandle(HANDLE hObject) {
    if (IsAkaiHandle(hObject)) {
        FreeAkaiHandle(hObject);
        return TRUE;
    }
    return CloseHandle(hObject);
}

// ----------------------------------------------------------------------------
// v13.3 Owner-Drawn Dropdown Menu + EB20 + Program + Multi + S6000 Scaler
// ----------------------------------------------------------------------------
#define MAX_SCALED_WINDOWS 256
#define WM_AKAI_APPLY_ZOOM   (WM_APP + 0x414B)

struct ScaledSubWindow {
    HWND hwnd;
    HWND hwndCanvas;
    WNDPROC origFrameProc;
    WNDPROC origCanvasProc;
    bool isRemotePanel;
    bool isMultiWindow;
    bool isProgramWindow;
    bool isEb20Window;
    bool isDropdownPopup;
    int frameNonClientW;
    int frameNonClientH;
    int baseCanvasW;
    int baseCanvasH;
    int cropW;
    int cropH;
    int curCanvasW;
    int curCanvasH;
    double currentZoom;
    bool inZoomResize;
    DWORD discoveredTick;
    HRGN hNativeBaseRgn;
    HDC hBackDC;
    HBITMAP hBackBmp;
    DWORD* pBackPixels;
    int backW;
    int backH;
    bool initialized;
};

static ScaledSubWindow g_ScaledWins[MAX_SCALED_WINDOWS];
static HWND g_hToolbarWidget = NULL;
static double g_CurrentGlobalZoom = 1.5;
static volatile bool g_KeepAspectRatio = true;
static ScaledSubWindow* g_LastClickedEditor = NULL;

static ScaledSubWindow* FindByFrame(HWND hwnd) {
    if (!hwnd) return NULL;
    for (int i = 0; i < MAX_SCALED_WINDOWS; i++) {
        if (g_ScaledWins[i].initialized && g_ScaledWins[i].hwnd == hwnd) {
            return &g_ScaledWins[i];
        }
    }
    return NULL;
}

static ScaledSubWindow* FindByCanvasOrFrame(HWND hwnd) {
    if (!hwnd) return NULL;
    for (int i = 0; i < MAX_SCALED_WINDOWS; i++) {
        if (g_ScaledWins[i].initialized) {
            if (g_ScaledWins[i].hwndCanvas == hwnd || g_ScaledWins[i].hwnd == hwnd) {
                return &g_ScaledWins[i];
            }
        }
    }
    HWND hParent = GetParent(hwnd);
    if (hParent) {
        return FindByFrame(hParent);
    }
    return NULL;
}

static int GetEffectiveSourceW(ScaledSubWindow* sw) {
    if (!sw) return 100;
    return sw->baseCanvasW;
}

static int GetEffectiveSourceH(ScaledSubWindow* sw) {
    if (!sw) return 100;
    if (sw->isDropdownPopup) return sw->baseCanvasH;
    return (sw->cropH >= 96 && sw->cropH <= sw->baseCanvasH) ? sw->cropH : sw->baseCanvasH;
}

static void EnsureBackbuffer(ScaledSubWindow* sw, HDC hdcRef) {
    if (!sw || sw->baseCanvasW <= 0 || sw->baseCanvasH <= 0) return;
    if (sw->hBackDC && sw->hBackBmp && sw->pBackPixels &&
        sw->backW == sw->baseCanvasW && sw->backH == sw->baseCanvasH) {
        return;
    }

    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = sw->baseCanvasW;
    bmi.bmiHeader.biHeight = -sw->baseCanvasH;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = NULL;
    HDC hNewDC = CreateCompatibleDC(hdcRef);
    HBITMAP hNewBmp = CreateDIBSection(hdcRef, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
    if (hNewBmp && pBits) {
        SelectObject(hNewDC, hNewBmp);
        memset(pBits, 0, (size_t)sw->baseCanvasW * (size_t)sw->baseCanvasH * 4);
    }

    if (sw->hBackDC && sw->hBackBmp && sw->backW > 0 && sw->backH > 0 && hNewBmp) {
        int copyW = (sw->backW < sw->baseCanvasW) ? sw->backW : sw->baseCanvasW;
        int copyH = (sw->backH < sw->baseCanvasH) ? sw->backH : sw->baseCanvasH;
        BitBlt(hNewDC, 0, 0, copyW, copyH, sw->hBackDC, 0, 0, SRCCOPY);
    }

    if (sw->hBackBmp) DeleteObject(sw->hBackBmp);
    if (sw->hBackDC) DeleteDC(sw->hBackDC);

    sw->hBackDC = hNewDC;
    sw->hBackBmp = hNewBmp;
    sw->pBackPixels = (DWORD*)pBits;
    sw->backW = sw->baseCanvasW;
    sw->backH = sw->baseCanvasH;
    sw->cropW = sw->baseCanvasW;
    sw->cropH = sw->baseCanvasH;
}

static void ProcessBackbufferPixels(ScaledSubWindow* sw) {
    if (!sw || !sw->pBackPixels || sw->backW <= 10 || sw->backH <= 10) return;
    GdiFlush();

    int W = sw->backW;
    int H = sw->backH;
    DWORD* px = sw->pBackPixels;

    if (sw->isDropdownPopup) {
        sw->cropW = W;
        sw->cropH = H;
        return;
    }

    if (sw->isRemotePanel) {
        const DWORD bezelColor = 0x00315163;
        for (int i = 0; i < W * H; i++) {
            DWORD c = px[i];
            BYTE r = (BYTE)((c >> 16) & 0xFF);
            BYTE g = (BYTE)((c >> 8) & 0xFF);
            BYTE b = (BYTE)(c & 0xFF);
            if (r >= 180 && g <= 70 && b >= 180 && (r + b - g * 2) > 260) {
                px[i] = bezelColor;
            }
        }
        sw->cropW = W;
        sw->cropH = H;
        return;
    }

    sw->cropW = W;

    if (sw->isEb20Window) {
        sw->cropH = H;
        return;
    }

    int validH = H;
    for (int y = H - 1; y >= 96; y--) {
        DWORD* row = px + y * W;
        int nonBlack = 0;
        for (int x = 8; x < W - 8; x += 4) {
            if ((row[x] & 0x00FFFFFF) != 0) {
                nonBlack++;
                if (nonBlack > 6) break;
            }
        }
        if (nonBlack > 6) {
            validH = y + 1;
            break;
        }
    }

    if (sw->isProgramWindow) {
        if (validH >= 500) validH = 546;
        else if (validH >= 300 && validH <= 345) validH = 322;
        else if (validH >= 140 && validH <= 175) validH = 154;
        else if (validH <= 125) validH = 104;
        sw->cropH = validH;
    } else {
        sw->cropH = H;
    }
}

static HRGN CreateCleanRemoteRegion(int targetW, int targetH, int insetX, int insetY) {
    double scaleX = (double)targetW / 540.0;
    double scaleY = (double)targetH / 368.0;
    int radX = (int)(38.0 * scaleX + 0.5);
    int radY = (int)(38.0 * scaleY + 0.5);
    int left = insetX + (int)(4.0 * scaleX + 0.5);
    int top  = insetY + (int)(4.0 * scaleY + 0.5);
    int right  = insetX + targetW - (int)(4.0 * scaleX + 0.5);
    int bottom = insetY + targetH - (int)(4.0 * scaleY + 0.5);
    return CreateRoundRectRgn(left, top, right, bottom, radX, radY);
}

static void UpdateWindowRegionsForScale(ScaledSubWindow* sw) {
    if (!sw) return;
    if (sw->isRemotePanel && sw->curCanvasW > 100 && sw->curCanvasH > 100) {
        if (sw->hwndCanvas && IsWindow(sw->hwndCanvas)) {
            HRGN hCanvasRgn = CreateCleanRemoteRegion(sw->curCanvasW, sw->curCanvasH, 0, 0);
            SetWindowRgn(sw->hwndCanvas, hCanvasRgn, TRUE);
        }
        if (sw->hwnd && IsWindow(sw->hwnd)) {
            RECT rcWin = {0}, rcClient = {0};
            GetWindowRect(sw->hwnd, &rcWin);
            GetClientRect(sw->hwnd, &rcClient);
            POINT ptClientTopLeft = {0, 0};
            ClientToScreen(sw->hwnd, &ptClientTopLeft);
            int offsetX = ptClientTopLeft.x - rcWin.left;
            int offsetY = ptClientTopLeft.y - rcWin.top;
            HRGN hFrameRgn = CreateCleanRemoteRegion(sw->curCanvasW, sw->curCanvasH, offsetX, offsetY);
            SetWindowRgn(sw->hwnd, hFrameRgn, TRUE);
        }
    } else {
        if (sw->hwnd && IsWindow(sw->hwnd)) {
            SetWindowRgn(sw->hwnd, NULL, FALSE);
        }
        if (sw->hwndCanvas && IsWindow(sw->hwndCanvas)) {
            SetWindowRgn(sw->hwndCanvas, NULL, FALSE);
        }
    }
}

static void StripScrollBars(HWND hCanvas) {
    if (!hCanvas || !IsWindow(hCanvas)) return;
    LONG cStyle = GetWindowLongA(hCanvas, GWL_STYLE);
    if (cStyle & (WS_HSCROLL | WS_VSCROLL)) {
        SetWindowLongA(hCanvas, GWL_STYLE, cStyle & ~(WS_HSCROLL | WS_VSCROLL));
    }
    ShowScrollBar(hCanvas, SB_BOTH, FALSE);
}

static int WINAPI Hook_SetWindowRgn(HWND hWnd, HRGN hRgn, BOOL bRedraw) {
    ScaledSubWindow* sw = FindByCanvasOrFrame(hWnd);
    if (sw && hRgn != NULL) {
        DeleteObject(hRgn);
        if (sw->isRemotePanel && sw->curCanvasW > 100 && sw->curCanvasH > 100) {
            UpdateWindowRegionsForScale(sw);
            return 1;
        }
        return SetWindowRgn(hWnd, NULL, bRedraw);
    }
    return SetWindowRgn(hWnd, hRgn, bRedraw);
}

static BOOL WINAPI Hook_InvalidateRect(HWND hWnd, const RECT* lpRect, BOOL bErase) {
    if (hWnd && lpRect != NULL) {
        ScaledSubWindow* sw = FindByCanvasOrFrame(hWnd);
        if (sw) {
            return InvalidateRect(hWnd, NULL, FALSE);
        }
    }
    return InvalidateRect(hWnd, lpRect, bErase);
}

static BOOL WINAPI Hook_GetClientRect(HWND hWnd, LPRECT lpRect) {
    BOOL ok = GetClientRect(hWnd, lpRect);
    if (ok && lpRect) {
        ScaledSubWindow* sw = FindByCanvasOrFrame(hWnd);
        if (sw && sw->hwndCanvas == hWnd && sw->baseCanvasW > 10 && sw->baseCanvasH > 10) {
            lpRect->left = 0;
            lpRect->top = 0;
            lpRect->right = sw->baseCanvasW;
            lpRect->bottom = sw->baseCanvasH;
        }
    }
    return ok;
}

static BOOL WINAPI Hook_BitBlt(HDC hdcDest, int xDest, int yDest, int wDest, int hDest,
                               HDC hdcSrc, int xSrc, int ySrc, DWORD rop) {
    if (hdcDest && hdcSrc && wDest > 0 && hDest > 0) {
        HWND hwnd = WindowFromDC(hdcDest);
        if (hwnd) {
            ScaledSubWindow* sw = FindByCanvasOrFrame(hwnd);
            if (sw && sw->baseCanvasW > 10 && sw->baseCanvasH > 10 &&
                sw->curCanvasW > 10 && sw->curCanvasH > 10) {

                int prevCropH = sw->cropH;
                RECT rcClip = {0};
                int clipType = GetClipBox(hdcDest, &rcClip);

                EnsureBackbuffer(sw, hdcDest);
                if (sw->hBackDC) {
                    if (!sw->isRemotePanel && !sw->isDropdownPopup && xDest == 0 && yDest == 0 &&
                        wDest >= sw->baseCanvasW - 8 && hDest >= 96 &&
                        clipType == SIMPLEREGION &&
                        ((rcClip.right - rcClip.left) < sw->baseCanvasW - 16 ||
                         (rcClip.bottom - rcClip.top) < hDest - 16)) {
                        int cx = (rcClip.left > 0) ? rcClip.left : 0;
                        int cy = (rcClip.top > 0) ? rcClip.top : 0;
                        int cw = rcClip.right - cx;
                        int ch = rcClip.bottom - cy;
                        if (cw > 0 && ch > 0) {
                            BitBlt(sw->hBackDC, cx, cy, cw, ch, hdcSrc, cx, cy, rop);
                        }
                    } else {
                        BitBlt(sw->hBackDC, xDest, yDest, wDest, hDest, hdcSrc, xSrc, ySrc, rop);
                    }
                    ProcessBackbufferPixels(sw);

                    if (sw->isProgramWindow && abs(sw->cropH - prevCropH) > 30 && !sw->inZoomResize) {
                        PostMessageA(sw->hwnd, WM_AKAI_APPLY_ZOOM, (WPARAM)(int)(sw->currentZoom * 100.0 + 0.5), 0);
                    }
                }

                int srcW = GetEffectiveSourceW(sw);
                int srcH = GetEffectiveSourceH(sw);

                if (abs(sw->curCanvasW - srcW) > 1 || abs(sw->curCanvasH - srcH) > 1 || sw->isRemotePanel || sw->isDropdownPopup) {
                    SelectClipRgn(hdcDest, NULL);
                    int oldMode = SetStretchBltMode(hdcDest, HALFTONE);
                    SetBrushOrgEx(hdcDest, 0, 0, NULL);

                    if (sw->hBackDC) {
                        BOOL res = StretchBlt(hdcDest, 0, 0, sw->curCanvasW, sw->curCanvasH,
                                              sw->hBackDC, 0, 0, srcW, srcH, SRCCOPY);
                        SetStretchBltMode(hdcDest, oldMode);
                        return res;
                    }
                    SetStretchBltMode(hdcDest, oldMode);
                }
            }
        }
    }
    return BitBlt(hdcDest, xDest, yDest, wDest, hDest, hdcSrc, xSrc, ySrc, rop);
}

static BOOL WINAPI Hook_ScreenToClient(HWND hWnd, LPPOINT lpPoint) {
    BOOL ok = ScreenToClient(hWnd, lpPoint);
    if (ok && lpPoint) {
        ScaledSubWindow* sw = FindByCanvasOrFrame(hWnd);
        if (sw && sw->curCanvasW > 10 && sw->curCanvasH > 10) {
            int srcW = GetEffectiveSourceW(sw);
            int srcH = GetEffectiveSourceH(sw);
            if (abs(sw->curCanvasW - srcW) > 1 || abs(sw->curCanvasH - srcH) > 1) {
                lpPoint->x = (LONG)((double)lpPoint->x * (double)srcW / (double)sw->curCanvasW + 0.5);
                lpPoint->y = (LONG)((double)lpPoint->y * (double)srcH / (double)sw->curCanvasH + 0.5);
            }
        }
    }
    return ok;
}

static BOOL WINAPI Hook_ClientToScreen(HWND hWnd, LPPOINT lpPoint) {
    if (hWnd && lpPoint) {
        ScaledSubWindow* sw = FindByCanvasOrFrame(hWnd);
        if (sw && !sw->isDropdownPopup && sw->curCanvasW > 10 && sw->curCanvasH > 10) {
            g_LastClickedEditor = sw;
            int srcW = GetEffectiveSourceW(sw);
            int srcH = GetEffectiveSourceH(sw);
            if (abs(sw->curCanvasW - srcW) > 1 || abs(sw->curCanvasH - srcH) > 1) {
                POINT scaledPt;
                scaledPt.x = (LONG)((double)lpPoint->x * (double)sw->curCanvasW / (double)srcW + 0.5);
                scaledPt.y = (LONG)((double)lpPoint->y * (double)sw->curCanvasH / (double)srcH + 0.5);
                BOOL ok = ClientToScreen(hWnd, &scaledPt);
                if (ok) {
                    *lpPoint = scaledPt;
                }
                return ok;
            }
        }
    }
    return ClientToScreen(hWnd, lpPoint);
}

static LPARAM ScaleMouseLParam(ScaledSubWindow* sw, LPARAM lParam) {
    if (!sw || sw->curCanvasW <= 10 || sw->curCanvasH <= 10) {
        return lParam;
    }
    int srcW = GetEffectiveSourceW(sw);
    int srcH = GetEffectiveSourceH(sw);
    if (abs(sw->curCanvasW - srcW) <= 1 && abs(sw->curCanvasH - srcH) <= 1) {
        return lParam;
    }
    short curX = (short)LOWORD(lParam);
    short curY = (short)HIWORD(lParam);
    short mappedX = (short)((double)curX * (double)srcW / (double)sw->curCanvasW + 0.5);
    short mappedY = (short)((double)curY * (double)srcH / (double)sw->curCanvasH + 0.5);
    return MAKELPARAM(mappedX, mappedY);
}

static void RepaintFromBackbuffer(ScaledSubWindow* sw) {
    if (!sw || !sw->hwndCanvas || !IsWindow(sw->hwndCanvas) || !sw->hBackDC) return;
    ProcessBackbufferPixels(sw);
    int srcW = GetEffectiveSourceW(sw);
    int srcH = GetEffectiveSourceH(sw);
    HDC hdc = GetDC(sw->hwndCanvas);
    if (hdc) {
        SelectClipRgn(hdc, NULL);
        int oldMode = SetStretchBltMode(hdc, HALFTONE);
        SetBrushOrgEx(hdc, 0, 0, NULL);
        StretchBlt(hdc, 0, 0, sw->curCanvasW, sw->curCanvasH,
                   sw->hBackDC, 0, 0, srcW, srcH, SRCCOPY);
        SetStretchBltMode(hdc, oldMode);
        ReleaseDC(sw->hwndCanvas, hdc);
    }
}

// ----------------------------------------------------------------------------
// Owner-Drawn Menu Scaling (WM_MEASUREITEM & WM_DRAWITEM for MIDI 1A..16B, etc.)
// ----------------------------------------------------------------------------
static LRESULT HandleScaledMeasureItem(ScaledSubWindow* sw, HWND hwnd, WNDPROC origProc, WPARAM wParam, LPARAM lParam) {
    LRESULT res = CallWindowProcA(origProc, hwnd, WM_MEASUREITEM, wParam, lParam);
    LPMEASUREITEMSTRUCT lpmis = (LPMEASUREITEMSTRUCT)lParam;
    if (lpmis && sw) {
        double z = sw->currentZoom;
        if (z < 1.0) z = g_CurrentGlobalZoom;
        if (z > 1.02 && lpmis->itemWidth > 0 && lpmis->itemHeight > 0 && lpmis->itemHeight < 80) {
            UINT origW = lpmis->itemWidth;
            UINT origH = lpmis->itemHeight;
            UINT newW = (UINT)(origW * z + 0.5);
            UINT newH = (UINT)(origH * z + 0.5);

            int screenH = GetSystemMetrics(SM_CYSCREEN);
            UINT maxItemH = (UINT)((screenH - 96) / 33);
            if (maxItemH < origH) maxItemH = origH;
            if (newH > maxItemH) {
                newH = maxItemH;
            }

            lpmis->itemWidth = newW;
            lpmis->itemHeight = newH;
        }
    }
    return res;
}

static LRESULT HandleScaledDrawItem(ScaledSubWindow* sw, HWND hwnd, WNDPROC origProc, WPARAM wParam, LPARAM lParam) {
    LPDRAWITEMSTRUCT lpdis = (LPDRAWITEMSTRUCT)lParam;
    if (!lpdis || !lpdis->hDC || !sw) {
        return CallWindowProcA(origProc, hwnd, WM_DRAWITEM, wParam, lParam);
    }

    double z = sw->currentZoom;
    if (z < 1.0) z = g_CurrentGlobalZoom;
    int dstW = lpdis->rcItem.right - lpdis->rcItem.left;
    int dstH = lpdis->rcItem.bottom - lpdis->rcItem.top;

    if (z <= 1.02 || dstW <= 4 || dstH <= 4 || dstW > 800 || dstH > 200) {
        return CallWindowProcA(origProc, hwnd, WM_DRAWITEM, wParam, lParam);
    }

    int srcW = (int)((double)dstW / z + 0.5);
    int srcH = (int)((double)dstH / z + 0.5);
    if (srcW < 16) srcW = 16;
    if (srcH < 11) srcH = 11;
    if (srcH > 16 && dstH <= (int)(16.0 * z + 2.0)) {
        srcH = 14;
    }

    HDC hMemDC = CreateCompatibleDC(lpdis->hDC);
    HBITMAP hMemBmp = CreateCompatibleBitmap(lpdis->hDC, srcW, srcH);
    if (!hMemDC || !hMemBmp) {
        if (hMemBmp) DeleteObject(hMemBmp);
        if (hMemDC) DeleteDC(hMemDC);
        return CallWindowProcA(origProc, hwnd, WM_DRAWITEM, wParam, lParam);
    }

    HBITMAP hOldBmp = (HBITMAP)SelectObject(hMemDC, hMemBmp);
    HFONT hCurFont = (HFONT)GetCurrentObject(lpdis->hDC, OBJ_FONT);
    if (hCurFont) SelectObject(hMemDC, hCurFont);

    DRAWITEMSTRUCT disCopy = *lpdis;
    disCopy.hDC = hMemDC;
    disCopy.rcItem.left = 0;
    disCopy.rcItem.top = 0;
    disCopy.rcItem.right = srcW;
    disCopy.rcItem.bottom = srcH;

    LRESULT res = CallWindowProcA(origProc, hwnd, WM_DRAWITEM, wParam, (LPARAM)&disCopy);

    int oldMode = SetStretchBltMode(lpdis->hDC, HALFTONE);
    SetBrushOrgEx(lpdis->hDC, 0, 0, NULL);
    StretchBlt(lpdis->hDC,
               lpdis->rcItem.left, lpdis->rcItem.top, dstW, dstH,
               hMemDC, 0, 0, srcW, srcH, SRCCOPY);
    SetStretchBltMode(lpdis->hDC, oldMode);

    SelectObject(hMemDC, hOldBmp);
    DeleteObject(hMemBmp);
    DeleteDC(hMemDC);
    return res;
}

static BOOL WINAPI Hook_SetWindowPos(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags) {
    return SetWindowPos(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags);
}

static void PerformMainThreadZoom(ScaledSubWindow* sw, double zoom) {
    if (!sw || !sw->initialized || !IsWindow(sw->hwnd) || sw->isDropdownPopup) return;
    sw->currentZoom = zoom;
    sw->inZoomResize = true;

    ProcessBackbufferPixels(sw);
    int srcW = GetEffectiveSourceW(sw);
    int srcH = GetEffectiveSourceH(sw);

    int targetCanvasW = (int)(srcW * zoom + 0.5);
    int targetCanvasH = (int)(srcH * zoom + 0.5);

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    if (sw->isMultiWindow && targetCanvasW > (screenW - 80) && zoom > 1.0) {
        targetCanvasW = screenW - 80;
        if (g_KeepAspectRatio && srcW > 0) {
            targetCanvasH = (int)((double)targetCanvasW * (double)srcH / (double)srcW + 0.5);
        }
    }

    int targetFrameW = targetCanvasW + sw->frameNonClientW;
    int targetFrameH = targetCanvasH + sw->frameNonClientH;

    sw->curCanvasW = targetCanvasW;
    sw->curCanvasH = targetCanvasH;

    StripScrollBars(sw->hwndCanvas);

    SetWindowPos(sw->hwnd, NULL, 0, 0, targetFrameW, targetFrameH,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    RECT rcClient = {0};
    GetClientRect(sw->hwnd, &rcClient);
    int cW = rcClient.right - rcClient.left;
    int cH = rcClient.bottom - rcClient.top;
    if (cW > 50 && cH > 30) {
        sw->curCanvasW = cW;
        sw->curCanvasH = cH;
        if (sw->hwndCanvas && IsWindow(sw->hwndCanvas)) {
            StripScrollBars(sw->hwndCanvas);
            SetWindowPos(sw->hwndCanvas, NULL, 0, 0, cW, cH,
                         SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        }
    }

    UpdateWindowRegionsForScale(sw);

    if (sw->hwndCanvas && IsWindow(sw->hwndCanvas)) {
        InvalidateRect(sw->hwndCanvas, NULL, FALSE);
        RepaintFromBackbuffer(sw);
    }
    InvalidateRect(sw->hwnd, NULL, FALSE);
    sw->inZoomResize = false;
}

static LRESULT CALLBACK CanvasScaleWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ScaledSubWindow* sw = FindByCanvasOrFrame(hwnd);
    if (!sw || !sw->origCanvasProc) return DefWindowProcA(hwnd, msg, wParam, lParam);
    WNDPROC origProc = sw->origCanvasProc;

    switch (msg) {
        case WM_MEASUREITEM: {
            return HandleScaledMeasureItem(sw, hwnd, origProc, wParam, lParam);
        }
        case WM_DRAWITEM: {
            return HandleScaledDrawItem(sw, hwnd, origProc, wParam, lParam);
        }
        case WM_WINDOWPOSCHANGING: {
            WINDOWPOS* wp = (WINDOWPOS*)lParam;
            if (wp && !(wp->flags & SWP_NOSIZE) && !sw->inZoomResize && !sw->isRemotePanel) {
                if (sw->isProgramWindow && wp->cy > 100 && wp->cy <= 600) {
                    if (sw->hBackDC && wp->cy < sw->cropH) {
                        RECT rcClear = { 0, wp->cy, sw->baseCanvasW, sw->baseCanvasH };
                        FillRect(sw->hBackDC, &rcClear, (HBRUSH)GetStockObject(BLACK_BRUSH));
                    }
                    sw->cropH = wp->cy;
                    wp->cx = (int)(sw->baseCanvasW * sw->currentZoom + 0.5);
                    wp->cy = (int)(sw->cropH * sw->currentZoom + 0.5);
                    sw->curCanvasW = wp->cx;
                    sw->curCanvasH = wp->cy;
                    PostMessageA(sw->hwnd, WM_AKAI_APPLY_ZOOM, (WPARAM)(int)(sw->currentZoom * 100.0 + 0.5), 0);
                }
            }
            break;
        }
        case WM_SIZE: {
            StripScrollBars(hwnd);
            if (sw->baseCanvasW > 50 && sw->baseCanvasH > 30) {
                return CallWindowProcA(origProc, hwnd, msg, wParam, MAKELPARAM(sw->baseCanvasW, sw->baseCanvasH));
            }
            break;
        }
        case WM_ERASEBKGND: {
            if (sw->hBackDC) {
                RepaintFromBackbuffer(sw);
                return 1;
            }
            break;
        }
        case WM_PAINT: {
            StripScrollBars(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
            LRESULT res = CallWindowProcA(origProc, hwnd, msg, wParam, lParam);
            if (sw->hBackDC) {
                RepaintFromBackbuffer(sw);
            }
            return res;
        }
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_MOUSEMOVE: {
            g_LastClickedEditor = sw;
            lParam = ScaleMouseLParam(sw, lParam);
            LRESULT res = CallWindowProcA(origProc, hwnd, msg, wParam, lParam);
            if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP || (msg == WM_MOUSEMOVE && (wParam & MK_LBUTTON))) {
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return res;
        }
        case WM_NCDESTROY: {
            SetWindowLongPtrA(hwnd, GWLP_WNDPROC, (LONG_PTR)origProc);
            sw->hwndCanvas = NULL;
            return CallWindowProcA(origProc, hwnd, msg, wParam, lParam);
        }
    }
    return CallWindowProcA(origProc, hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK FrameScaleWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ScaledSubWindow* sw = FindByFrame(hwnd);
    if (!sw || !sw->origFrameProc) return DefWindowProcA(hwnd, msg, wParam, lParam);
    WNDPROC origProc = sw->origFrameProc;

    switch (msg) {
        case WM_MEASUREITEM: {
            return HandleScaledMeasureItem(sw, hwnd, origProc, wParam, lParam);
        }
        case WM_DRAWITEM: {
            return HandleScaledDrawItem(sw, hwnd, origProc, wParam, lParam);
        }
        case WM_AKAI_APPLY_ZOOM: {
            double z = (double)wParam / 100.0;
            if (z >= 0.5 && z <= 4.0) {
                PerformMainThreadZoom(sw, z);
            }
            return 0;
        }
        case WM_WINDOWPOSCHANGING: {
            WINDOWPOS* wp = (WINDOWPOS*)lParam;
            if (wp && !(wp->flags & SWP_NOSIZE) && !sw->inZoomResize && sw->isProgramWindow) {
                int reqClientW = wp->cx - sw->frameNonClientW;
                int reqClientH = wp->cy - sw->frameNonClientH;
                if (abs(reqClientW - 766) <= 24 && reqClientH >= 96 && reqClientH <= 580) {
                    if (sw->hBackDC && reqClientH < sw->cropH) {
                        RECT rcClear = { 0, reqClientH, sw->baseCanvasW, sw->baseCanvasH };
                        FillRect(sw->hBackDC, &rcClear, (HBRUSH)GetStockObject(BLACK_BRUSH));
                    }
                    sw->cropH = reqClientH;
                    wp->cx = (int)(sw->baseCanvasW * sw->currentZoom + 0.5) + sw->frameNonClientW;
                    wp->cy = (int)(sw->cropH * sw->currentZoom + 0.5) + sw->frameNonClientH;
                }
            }
            break;
        }
        case WM_SIZING: {
            if (g_KeepAspectRatio && lParam) {
                RECT* rc = (RECT*)lParam;
                int srcW = GetEffectiveSourceW(sw);
                int srcH = GetEffectiveSourceH(sw);
                if (srcW > 50 && srcH > 30) {
                    double aspect = (double)srcW / (double)srcH;
                    int curClientW = (rc->right - rc->left) - sw->frameNonClientW;
                    int curClientH = (rc->bottom - rc->top) - sw->frameNonClientH;
                    if (curClientW < 120) curClientW = 120;
                    if (curClientH < 60)  curClientH = 60;

                    if (wParam == WMSZ_LEFT || wParam == WMSZ_RIGHT ||
                        wParam == WMSZ_TOPLEFT || wParam == WMSZ_TOPRIGHT ||
                        wParam == WMSZ_BOTTOMLEFT || wParam == WMSZ_BOTTOMRIGHT) {
                        int newClientH = (int)((double)curClientW / aspect + 0.5);
                        int newFrameH = newClientH + sw->frameNonClientH;
                        if (wParam == WMSZ_TOPLEFT || wParam == WMSZ_TOPRIGHT) {
                            rc->top = rc->bottom - newFrameH;
                        } else {
                            rc->bottom = rc->top + newFrameH;
                        }
                    } else if (wParam == WMSZ_TOP || wParam == WMSZ_BOTTOM) {
                        int newClientW = (int)((double)curClientH * aspect + 0.5);
                        int newFrameW = newClientW + sw->frameNonClientW;
                        rc->right = rc->left + newFrameW;
                    }
                    return TRUE;
                }
            }
            break;
        }
        case WM_GETMINMAXINFO: {
            LPMINMAXINFO mmi = (LPMINMAXINFO)lParam;
            if (mmi) {
                mmi->ptMinTrackSize.x = 200;
                mmi->ptMinTrackSize.y = 80;
                mmi->ptMaxTrackSize.x = 7680;
                mmi->ptMaxTrackSize.y = 4320;
                mmi->ptMaxSize.x = 7680;
                mmi->ptMaxSize.y = 4320;
                return 0;
            }
            break;
        }
        case WM_SIZE: {
            if (wParam != SIZE_MINIMIZED) {
                RECT rcClient = {0};
                GetClientRect(hwnd, &rcClient);
                int cW = rcClient.right - rcClient.left;
                int cH = rcClient.bottom - rcClient.top;
                if (cW > 50 && cH > 30) {
                    sw->curCanvasW = cW;
                    sw->curCanvasH = cH;
                    int srcW = GetEffectiveSourceW(sw);
                    if (srcW > 50) {
                        sw->currentZoom = (double)cW / (double)srcW;
                    }
                    if (sw->hwndCanvas && IsWindow(sw->hwndCanvas)) {
                        sw->inZoomResize = true;
                        StripScrollBars(sw->hwndCanvas);
                        SetWindowPos(sw->hwndCanvas, NULL, 0, 0, cW, cH,
                                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
                        sw->inZoomResize = false;
                        UpdateWindowRegionsForScale(sw);
                        InvalidateRect(sw->hwndCanvas, NULL, FALSE);
                        RepaintFromBackbuffer(sw);
                    }
                }
            }
            return 0;
        }
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_MOUSEMOVE: {
            g_LastClickedEditor = sw;
            lParam = ScaleMouseLParam(sw, lParam);
            return CallWindowProcA(origProc, hwnd, msg, wParam, lParam);
        }
        case WM_NCDESTROY: {
            SetWindowLongPtrA(hwnd, GWLP_WNDPROC, (LONG_PTR)origProc);
            if (sw->hNativeBaseRgn) {
                DeleteObject(sw->hNativeBaseRgn);
                sw->hNativeBaseRgn = NULL;
            }
            if (sw->hBackBmp) {
                DeleteObject(sw->hBackBmp);
                sw->hBackBmp = NULL;
            }
            if (sw->hBackDC) {
                DeleteDC(sw->hBackDC);
                sw->hBackDC = NULL;
            }
            sw->pBackPixels = NULL;
            sw->hwnd = NULL;
            sw->initialized = false;
            return CallWindowProcA(origProc, hwnd, msg, wParam, lParam);
        }
    }
    return CallWindowProcA(origProc, hwnd, msg, wParam, lParam);
}

struct EnumScaleCtx {
    double zoom;
    bool forceApplyZoom;
};

static void RegisterAfxFrameWindow(HWND hwnd, double currentZoom, bool forceApplyZoom) {
    if (!IsWindow(hwnd) || !IsWindowVisible(hwnd)) return;

    char clsName[128] = {0};
    char title[128] = {0};
    GetClassNameA(hwnd, clsName, sizeof(clsName) - 1);
    GetWindowTextA(hwnd, title, sizeof(title) - 1);

    if (strstr(clsName, "AfxFrameOrView") == NULL || title[0] == '\0') {
        return;
    }

    ScaledSubWindow* existing = FindByFrame(hwnd);
    if (existing) {
        if (forceApplyZoom && currentZoom >= 0.5 && !existing->isDropdownPopup) {
            DWORD_PTR res = 0;
            SendMessageTimeoutA(hwnd, WM_AKAI_APPLY_ZOOM, (WPARAM)(int)(currentZoom * 100.0 + 0.5), 0,
                                SMTO_ABORTIFHUNG | SMTO_NORMAL, 250, &res);
        }
        return;
    }

    RECT rcWin = {0}, rcClient = {0};
    GetWindowRect(hwnd, &rcWin);
    GetClientRect(hwnd, &rcClient);
    int winW = rcWin.right - rcWin.left;
    int winH = rcWin.bottom - rcWin.top;
    int clientW = rcClient.right - rcClient.left;
    int clientH = rcClient.bottom - rcClient.top;
    if (winW < 100 || winH < 40) return;

    bool isRemote = (strcmp(title, "AKAI S6000") == 0 || strcmp(title, "AKAI S5000") == 0);
    bool isEb20   = (!isRemote && (strstr(title, "EB20") != NULL || strstr(title, "eb20") != NULL));
    bool isMulti  = (!isRemote && !isEb20 && (strstr(title, "Multi") != NULL || strstr(title, "MULTI") != NULL || strstr(title, "multi") != NULL || winW > 1000));
    bool isProg   = (!isRemote && !isEb20 && !isMulti);

    HWND hCanvas = NULL;
    HWND hChild = GetWindow(hwnd, GW_CHILD);
    while (hChild) {
        char cCls[64] = {0};
        GetClassNameA(hChild, cCls, sizeof(cCls) - 1);
        if (strstr(cCls, "Afx:") && !hCanvas) {
            hCanvas = hChild;
        }
        hChild = GetWindow(hChild, GW_HWNDNEXT);
    }

    int nonClientW = (winW > clientW) ? (winW - clientW) : 16;
    int nonClientH = (winH > clientH) ? (winH - clientH) : 39;

    int trueBaseW = clientW;
    int trueBaseH = clientH;
    if (isRemote) {
        trueBaseW = 540;
        trueBaseH = 368;
    } else if (isMulti) {
        trueBaseW = 1600;
        trueBaseH = 522;
    } else if (isEb20) {
        trueBaseW = 650;
        trueBaseH = 352;
    } else {
        trueBaseW = 766;
        trueBaseH = 546;
    }

    int slot = -1;
    for (int i = 0; i < MAX_SCALED_WINDOWS; i++) {
        if (!g_ScaledWins[i].initialized || !IsWindow(g_ScaledWins[i].hwnd)) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return;

    ScaledSubWindow* sw = &g_ScaledWins[slot];
    memset(sw, 0, sizeof(ScaledSubWindow));
    sw->hwnd = hwnd;
    sw->hwndCanvas = hCanvas;
    sw->isRemotePanel = isRemote;
    sw->isMultiWindow = isMulti;
    sw->isProgramWindow = isProg;
    sw->isEb20Window = isEb20;
    sw->isDropdownPopup = false;
    sw->frameNonClientW = nonClientW;
    sw->frameNonClientH = nonClientH;
    sw->baseCanvasW = trueBaseW;
    sw->baseCanvasH = trueBaseH;
    sw->cropW = trueBaseW;
    sw->cropH = (isProg && clientH < 500) ? clientH : trueBaseH;
    sw->curCanvasW = clientW;
    sw->curCanvasH = clientH;
    sw->currentZoom = 1.0;
    sw->inZoomResize = false;
    sw->discoveredTick = GetTickCount();
    sw->hNativeBaseRgn = NULL;
    sw->hBackDC = NULL;
    sw->hBackBmp = NULL;
    sw->pBackPixels = NULL;
    sw->backW = 0;
    sw->backH = 0;
    sw->initialized = true;
    g_LastClickedEditor = sw;

    if (hCanvas) {
        StripScrollBars(hCanvas);

        HDC hdcWin = GetDC(hCanvas);
        if (hdcWin) {
            EnsureBackbuffer(sw, hdcWin);
            if (sw->hBackDC) {
                BitBlt(sw->hBackDC, 0, 0, sw->baseCanvasW, sw->baseCanvasH, hdcWin, 0, 0, SRCCOPY);
                ProcessBackbufferPixels(sw);
            }
            ReleaseDC(hCanvas, hdcWin);
        }
    }

    LONG style = GetWindowLongA(hwnd, GWL_STYLE);
    if (isRemote) {
        SetWindowLongA(hwnd, GWL_STYLE, style | WS_THICKFRAME);
    } else {
        SetWindowLongA(hwnd, GWL_STYLE, style | WS_THICKFRAME | WS_MAXIMIZEBOX | WS_MINIMIZEBOX);
    }

    sw->origFrameProc = (WNDPROC)SetWindowLongPtrA(hwnd, GWLP_WNDPROC, (LONG_PTR)FrameScaleWndProc);
    if (hCanvas) {
        sw->origCanvasProc = (WNDPROC)SetWindowLongPtrA(hCanvas, GWLP_WNDPROC, (LONG_PTR)CanvasScaleWndProc);
    }

    double initialZoom = (currentZoom >= 1.0) ? currentZoom : 1.0;
    DWORD_PTR res = 0;
    SendMessageTimeoutA(hwnd, WM_AKAI_APPLY_ZOOM, (WPARAM)(int)(initialZoom * 100.0 + 0.5), 0,
                        SMTO_ABORTIFHUNG | SMTO_NORMAL, 250, &res);
}

static BOOL CALLBACK EnumChildProc(HWND hwndChild, LPARAM lParam) {
    EnumScaleCtx* ctx = (EnumScaleCtx*)lParam;
    RegisterAfxFrameWindow(hwndChild, ctx->zoom, ctx->forceApplyZoom);
    return TRUE;
}

static BOOL CALLBACK EnumTopWindowsProc(HWND hwnd, LPARAM lParam) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == g_MainProcessId) {
        EnumScaleCtx* ctx = (EnumScaleCtx*)lParam;
        RegisterAfxFrameWindow(hwnd, ctx->zoom, ctx->forceApplyZoom);
        EnumChildWindows(hwnd, EnumChildProc, lParam);
    }
    return TRUE;
}

static void ScanAndScaleAllSubWindows(double requestedZoom, bool forceApplyZoom) {
    EnumScaleCtx ctx;
    ctx.zoom = requestedZoom;
    ctx.forceApplyZoom = forceApplyZoom;
    EnumWindows(EnumTopWindowsProc, (LPARAM)&ctx);
}

// ----------------------------------------------------------------------------
// Dedicated UI Thread for Zoom Toolbar (with Lock Aspect Ratio Checkbox!)
// ----------------------------------------------------------------------------
#define ID_BTN_ZOOM_100 9001
#define ID_BTN_ZOOM_125 9002
#define ID_BTN_ZOOM_150 9003
#define ID_BTN_ZOOM_175 9004
#define ID_BTN_ZOOM_200 9005
#define ID_CHK_ASPECT   9006

static LRESULT CALLBACK ZoomToolbarWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_SETCURSOR: {
            SetCursor(LoadCursor(NULL, IDC_ARROW));
            return TRUE;
        }
        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            if (wmId == ID_CHK_ASPECT) {
                LRESULT chk = SendMessageA((HWND)lParam, BM_GETCHECK, 0, 0);
                g_KeepAspectRatio = (chk == BST_CHECKED);
                if (g_KeepAspectRatio) {
                    ScanAndScaleAllSubWindows(g_CurrentGlobalZoom, true);
                }
                return 0;
            }

            double zoom = 1.0;
            switch (wmId) {
                case ID_BTN_ZOOM_100: zoom = 1.0; break;
                case ID_BTN_ZOOM_125: zoom = 1.25; break;
                case ID_BTN_ZOOM_150: zoom = 1.5; break;
                case ID_BTN_ZOOM_175: zoom = 1.75; break;
                case ID_BTN_ZOOM_200: zoom = 2.0; break;
                default: return 0;
            }
            g_CurrentGlobalZoom = zoom;
            ScanAndScaleAllSubWindows(zoom, true);
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            HBRUSH hBr = CreateSolidBrush(RGB(240, 244, 248));
            FillRect(hdc, &rc, hBr);
            DeleteObject(hBr);

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(15, 23, 42));
            HFONT hOldFont = (HFONT)SelectObject(hdc, GetStockObject(DEFAULT_GUI_FONT));
            TextOutA(hdc, 10, 14, "UI Scale:", 9);
            SelectObject(hdc, hOldFont);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DESTROY: {
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

static DWORD WINAPI ZoomToolbarUIThread(LPVOID) {
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = ZoomToolbarWndProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "AkaiZoomBarClassV133";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassA(&wc);

    g_hToolbarWidget = CreateWindowExA(WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                                       "AkaiZoomBarClassV133", "ak.sys Zoom Controls (v13.3)",
                                       WS_POPUP | WS_CAPTION | WS_VISIBLE | WS_BORDER | WS_SYSMENU,
                                       140, 90, 395, 102,
                                       NULL, NULL, GetModuleHandleA(NULL), NULL);
    if (!g_hToolbarWidget) return 0;

    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    struct { int id; const char* text; int x; } btns[] = {
        { ID_BTN_ZOOM_100, "1.0x", 75 },
        { ID_BTN_ZOOM_125, "1.25x", 135 },
        { ID_BTN_ZOOM_150, "1.5x", 195 },
        { ID_BTN_ZOOM_175, "1.75x", 255 },
        { ID_BTN_ZOOM_200, "2.0x", 315 }
    };

    for (int i = 0; i < 5; i++) {
        HWND hBtn = CreateWindowExA(0, "BUTTON", btns[i].text,
                                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
                                    btns[i].x, 8, 54, 26,
                                    g_hToolbarWidget, (HMENU)(INT_PTR)btns[i].id,
                                    GetModuleHandleA(NULL), NULL);
        if (hBtn && hFont) {
            SendMessageA(hBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
            ShowWindow(hBtn, SW_SHOW);
            UpdateWindow(hBtn);
        }
    }

    HWND hChk = CreateWindowExA(0, "BUTTON", "Lock Original Aspect Ratio When Resizing",
                                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
                                12, 40, 355, 22,
                                g_hToolbarWidget, (HMENU)(INT_PTR)ID_CHK_ASPECT,
                                GetModuleHandleA(NULL), NULL);
    if (hChk) {
        if (hFont) SendMessageA(hChk, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageA(hChk, BM_SETCHECK, g_KeepAspectRatio ? BST_CHECKED : BST_UNCHECKED, 0);
        ShowWindow(hChk, SW_SHOW);
        UpdateWindow(hChk);
    }

    ShowWindow(g_hToolbarWidget, SW_SHOW);
    UpdateWindow(g_hToolbarWidget);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    g_hToolbarWidget = NULL;
    return 0;
}

static void EnsureZoomToolbarRunning() {
    if (g_hToolbarWidget && IsWindow(g_hToolbarWidget)) return;
    HANDLE hThread = CreateThread(NULL, 0, ZoomToolbarUIThread, NULL, 0, NULL);
    if (hThread) CloseHandle(hThread);
}

// ----------------------------------------------------------------------------
// IAT Hook Engine
// ----------------------------------------------------------------------------
static void* ResolveHookFunction(const char* fn) {
    if (strcmp(fn, "CreateFileA") == 0) return (void*)Hook_CreateFileA;
    if (strcmp(fn, "ReadFile") == 0) return (void*)Hook_ReadFile;
    if (strcmp(fn, "WriteFile") == 0) return (void*)Hook_WriteFile;
    if (strcmp(fn, "DeviceIoControl") == 0) return (void*)Hook_DeviceIoControl;
    if (strcmp(fn, "GetOverlappedResult") == 0) return (void*)Hook_GetOverlappedResult;
    if (strcmp(fn, "CancelIo") == 0) return (void*)Hook_CancelIo;
    if (strcmp(fn, "CloseHandle") == 0) return (void*)Hook_CloseHandle;
    if (strcmp(fn, "SetupDiGetClassDevsA") == 0) return (void*)Hook_SetupDiGetClassDevsA;
    if (strcmp(fn, "SetupDiEnumDeviceInterfaces") == 0) return (void*)Hook_SetupDiEnumDeviceInterfaces;
    if (strcmp(fn, "SetupDiGetDeviceInterfaceDetailA") == 0) return (void*)Hook_SetupDiGetDeviceInterfaceDetailA;
    if (strcmp(fn, "SetupDiDestroyDeviceInfoList") == 0) return (void*)Hook_SetupDiDestroyDeviceInfoList;
    if (strcmp(fn, "BitBlt") == 0) return (void*)Hook_BitBlt;
    if (strcmp(fn, "SetWindowRgn") == 0) return (void*)Hook_SetWindowRgn;
    if (strcmp(fn, "ScreenToClient") == 0) return (void*)Hook_ScreenToClient;
    if (strcmp(fn, "ClientToScreen") == 0) return (void*)Hook_ClientToScreen;
    if (strcmp(fn, "SetWindowPos") == 0) return (void*)Hook_SetWindowPos;
    if (strcmp(fn, "GetClientRect") == 0) return (void*)Hook_GetClientRect;
    if (strcmp(fn, "InvalidateRect") == 0) return (void*)Hook_InvalidateRect;
    return NULL;
}

static void PatchModuleIAT(HMODULE hMod, const char* modName) {
    if (!hMod || hMod == g_hShimModule) return;

    __try {
        PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)hMod;
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
        PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)((BYTE*)hMod + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return;

        DWORD impRva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
        if (!impRva) return;
        PIMAGE_IMPORT_DESCRIPTOR imp = (PIMAGE_IMPORT_DESCRIPTOR)((BYTE*)hMod + impRva);

        int count = 0;
        for (; imp->Name; imp++) {
            PIMAGE_THUNK_DATA thunk = (PIMAGE_THUNK_DATA)((BYTE*)hMod + imp->FirstThunk);
            PIMAGE_THUNK_DATA origThunk = imp->OriginalFirstThunk ?
                                          (PIMAGE_THUNK_DATA)((BYTE*)hMod + imp->OriginalFirstThunk) : thunk;

            for (; thunk->u1.AddressOfData; thunk++, origThunk++) {
                if (IMAGE_SNAP_BY_ORDINAL(origThunk->u1.Ordinal)) continue;
                PIMAGE_IMPORT_BY_NAME name = (PIMAGE_IMPORT_BY_NAME)((BYTE*)hMod + origThunk->u1.AddressOfData);
                void* rep = ResolveHookFunction((const char*)name->Name);
                if (rep && thunk->u1.Function != (ULONG_PTR)rep) {
                    DWORD old;
                    if (VirtualProtect(&thunk->u1.Function, sizeof(ULONG_PTR), PAGE_EXECUTE_READWRITE, &old)) {
                        thunk->u1.Function = (ULONG_PTR)rep;
                        VirtualProtect(&thunk->u1.Function, sizeof(ULONG_PTR), old, &old);
                        count++;
                    }
                }
            }
        }
        if (count > 0 && modName) {
            if (strstr(modName, "S56") || strstr(modName, "s56")) {
                g_S56Hooked = true;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static void ScanAndPatchAllLoadedModules() {
    const char* knownModules[] = {
        "S56Device.dll",
        "devices\\S56Device.dll",
        "C:\\Program Files (x86)\\AKAIPRO\\aksys\\devices\\S56Device.dll",
        "Z48Device.dll",
        "DPS24Device.dll",
        "AKComCtrls.dll",
        "AKSys.exe",
        "aksys.exe",
        "MFC42.DLL",
        "mfc42.dll"
    };
    for (int i = 0; i < (int)(sizeof(knownModules) / sizeof(knownModules[0])); i++) {
        HMODULE h = GetModuleHandleA(knownModules[i]);
        if (h) {
            PatchModuleIAT(h, knownModules[i]);
        }
    }
    HMODULE hMain = GetModuleHandleA(NULL);
    if (hMain) {
        PatchModuleIAT(hMain, "AKSys.exe(Main)");
    }
}

static void InstallSetupApiTrampolines() {
    HMODULE hSetup = GetModuleHandleA("setupapi.dll");
    if (!hSetup) hSetup = LoadLibraryA("setupapi.dll");
    if (hSetup) {
        void* pG = (void*)GetProcAddress(hSetup, "SetupDiGetClassDevsA");
        void* pE = (void*)GetProcAddress(hSetup, "SetupDiEnumDeviceInterfaces");
        void* pD = (void*)GetProcAddress(hSetup, "SetupDiGetDeviceInterfaceDetailA");

        if (pG && !g_realSetupDiGetClassDevsA) {
            InstallTrampolineHook(pG, (void*)Hook_SetupDiGetClassDevsA, (void**)&g_realSetupDiGetClassDevsA);
        }
        if (pE && !g_realSetupDiEnumDeviceInterfaces) {
            InstallTrampolineHook(pE, (void*)Hook_SetupDiEnumDeviceInterfaces, (void**)&g_realSetupDiEnumDeviceInterfaces);
        }
        if (pD && !g_realSetupDiGetDeviceInterfaceDetailA) {
            InstallTrampolineHook(pD, (void*)Hook_SetupDiGetDeviceInterfaceDetailA, (void**)&g_realSetupDiGetDeviceInterfaceDetailA);
        }
    }
}

static DWORD WINAPI WatcherThread(LPVOID) {
    EnableDpiAwareness();
    RepairAkaiProgramEditorRegistry(false);
    OpenAkaiWinUsbDevice();
    InstallSetupApiTrampolines();

    for (int i = 0; i < 40; i++) {
        ScanAndPatchAllLoadedModules();
        if (g_S56Hooked && i > 3) break;
        Sleep(50);
    }

    while (true) {
        Sleep(250);
        EnsureZoomToolbarRunning();
        ScanAndScaleAllSubWindows(g_CurrentGlobalZoom, false);
    }

    return 0;
}

static HMODULE g_hRealVersion = NULL;

static void LoadRealVersionDll() {
    if (g_hRealVersion) return;
    char sysDir[MAX_PATH];
    GetSystemDirectoryA(sysDir, MAX_PATH);
    strcat(sysDir, "\\version.dll");
    g_hRealVersion = LoadLibraryA(sysDir);
}

static void CheckAndHookCaller(void* returnAddr) {
    HMODULE hCaller = NULL;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)returnAddr, &hCaller)) {
        if (hCaller && hCaller != g_hShimModule) {
            PatchModuleIAT(hCaller, "CallerModule");
        }
    }
    ScanAndPatchAllLoadedModules();
}

extern "C" {
    BOOL WINAPI Proxy_GetFileVersionInfoA(LPCSTR f, DWORD h, DWORD l, LPVOID d) {
        CheckAndHookCaller(_ReturnAddress());
        LoadRealVersionDll();
        FARPROC p = g_hRealVersion ? GetProcAddress(g_hRealVersion, "GetFileVersionInfoA") : NULL;
        typedef BOOL (WINAPI *PFN)(LPCSTR, DWORD, DWORD, LPVOID);
        return p ? ((PFN)p)(f, h, l, d) : FALSE;
    }
    BOOL WINAPI Proxy_GetFileVersionInfoW(LPCWSTR f, DWORD h, DWORD l, LPVOID d) {
        CheckAndHookCaller(_ReturnAddress());
        LoadRealVersionDll();
        FARPROC p = g_hRealVersion ? GetProcAddress(g_hRealVersion, "GetFileVersionInfoW") : NULL;
        typedef BOOL (WINAPI *PFN)(LPCWSTR, DWORD, DWORD, LPVOID);
        return p ? ((PFN)p)(f, h, l, d) : FALSE;
    }
    DWORD WINAPI Proxy_GetFileVersionInfoSizeA(LPCSTR f, LPDWORD h) {
        CheckAndHookCaller(_ReturnAddress());
        LoadRealVersionDll();
        FARPROC p = g_hRealVersion ? GetProcAddress(g_hRealVersion, "GetFileVersionInfoSizeA") : NULL;
        typedef DWORD (WINAPI *PFN)(LPCSTR, LPDWORD);
        return p ? ((PFN)p)(f, h) : 0;
    }
    DWORD WINAPI Proxy_GetFileVersionInfoSizeW(LPCWSTR f, LPDWORD h) {
        CheckAndHookCaller(_ReturnAddress());
        LoadRealVersionDll();
        FARPROC p = g_hRealVersion ? GetProcAddress(g_hRealVersion, "GetFileVersionInfoSizeW") : NULL;
        typedef DWORD (WINAPI *PFN)(LPCWSTR, LPDWORD);
        return p ? ((PFN)p)(f, h) : 0;
    }
    BOOL WINAPI Proxy_VerQueryValueA(LPCVOID b, LPCSTR s, LPVOID *pBlock, PUINT l) {
        CheckAndHookCaller(_ReturnAddress());
        LoadRealVersionDll();
        FARPROC p = g_hRealVersion ? GetProcAddress(g_hRealVersion, "VerQueryValueA") : NULL;
        typedef BOOL (WINAPI *PFN)(LPCVOID, LPCSTR, LPVOID*, PUINT);
        return p ? ((PFN)p)(b, s, pBlock, l) : FALSE;
    }
    BOOL WINAPI Proxy_VerQueryValueW(LPCVOID b, LPCWSTR s, LPVOID *pBlock, PUINT l) {
        CheckAndHookCaller(_ReturnAddress());
        LoadRealVersionDll();
        FARPROC p = g_hRealVersion ? GetProcAddress(g_hRealVersion, "VerQueryValueW") : NULL;
        typedef BOOL (WINAPI *PFN)(LPCVOID, LPCWSTR, LPVOID*, PUINT);
        return p ? ((PFN)p)(b, s, pBlock, l) : FALSE;
    }
}

#pragma comment(linker, "/EXPORT:GetFileVersionInfoA=_Proxy_GetFileVersionInfoA@16")
#pragma comment(linker, "/EXPORT:GetFileVersionInfoW=_Proxy_GetFileVersionInfoW@16")
#pragma comment(linker, "/EXPORT:GetFileVersionInfoSizeA=_Proxy_GetFileVersionInfoSizeA@8")
#pragma comment(linker, "/EXPORT:GetFileVersionInfoSizeW=_Proxy_GetFileVersionInfoSizeW@8")
#pragma comment(linker, "/EXPORT:VerQueryValueA=_Proxy_VerQueryValueA@16")
#pragma comment(linker, "/EXPORT:VerQueryValueW=_Proxy_VerQueryValueW@16")

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hShimModule = hModule;
        g_MainProcessId = GetCurrentProcessId();
        g_MainThreadId = GetCurrentThreadId();
        DisableThreadLibraryCalls(hModule);
        memset(g_Handles, 0, sizeof(g_Handles));
        memset(g_ScaledWins, 0, sizeof(g_ScaledWins));
        g_HandleCounter = 0;
        InitializeCriticalSection(&g_UsbLock);

        CreateThread(NULL, 0, WatcherThread, NULL, 0, NULL);
    } else if (reason == DLL_PROCESS_DETACH) {
        if (g_hToolbarWidget && IsWindow(g_hToolbarWidget)) DestroyWindow(g_hToolbarWidget);
        for (int i = 0; i < MAX_AKAI_HANDLES; i++) {
            if (g_Handles[i].hEvent) CloseHandle(g_Handles[i].hEvent);
        }
        if (g_hWinUsb) WinUsb_Free(g_hWinUsb);
        if (g_hUsbDevice != INVALID_HANDLE_VALUE) CloseHandle(g_hUsbDevice);
    }
    return TRUE;
}