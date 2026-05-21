// votv-coop standalone loader -- xinput1_3.dll proxy.
//
// Shipping auto-loader (RULE No.3 -- no UE4SS at runtime). VOTV imports
// XInputGetState/XInputSetState from xinput1_3.dll, and the Windows loader
// searches the exe's own directory before System32. So a xinput1_3.dll dropped
// next to VotV-Win64-Shipping.exe is loaded in place of the system one. The
// two imported exports are satisfied by STATIC forwarders to System32's
// xinput1_4.dll (see xinput1_3.def) -- resolved by the loader, independent of
// the code below.
//
// This proxy's only RUNTIME job is to load the mod payload (votv-coop.dll)
// from its own directory. That is exactly what the dev-only inject.ps1 did via
// CreateRemoteThread; the proxy makes it automatic, with no injection step and
// no UE4SS. LoadLibrary is done off the loader lock (worker thread), never
// inside DllMain.

#include <windows.h>

// Static export forwarders for the two symbols VOTV imports from xinput1_3.dll
// (verified by name in the shipping exe import table). Both are forwarded to
// System32's xinput1_4.dll (guaranteed present on Win8+), which exports the
// same functions with identical signatures. We forward ONLY what VOTV imports
// (RULE No.2 -- no speculative baggage); the ordinals mirror the real
// xinput1_3 so an ordinal importer would also resolve. The loader resolves
// these at bind time, independent of the LoadPayload code below.
#pragma comment(linker, "/export:XInputGetState=xinput1_4.XInputGetState,@2")
#pragma comment(linker, "/export:XInputSetState=xinput1_4.XInputSetState,@3")

namespace {

DWORD WINAPI LoadPayload(LPVOID) {
    // Resolve this proxy's own path so we load the payload sitting beside it,
    // not whatever a relative search order might find first.
    HMODULE self = nullptr;
    ::GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&LoadPayload), &self);

    wchar_t path[MAX_PATH] = {};
    ::GetModuleFileNameW(self, path, MAX_PATH);

    // Trim to the directory (keep the trailing separator) and append the payload.
    wchar_t* lastSep = nullptr;
    for (wchar_t* p = path; *p; ++p) {
        if (*p == L'\\' || *p == L'/') lastSep = p;
    }
    if (lastSep) {
        lastSep[1] = L'\0';
    } else {
        path[0] = L'\0';
    }
    wcscat_s(path, L"votv-coop.dll");

    // The payload bootstraps itself from its own DllMain.
    ::LoadLibraryW(path);
    return 0;
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        ::DisableThreadLibraryCalls(module);
        if (HANDLE t = ::CreateThread(nullptr, 0, LoadPayload, nullptr, 0, nullptr)) {
            ::CloseHandle(t);
        }
    }
    return TRUE;
}
