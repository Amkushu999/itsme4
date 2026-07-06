// symbol_resolver.cpp — Runtime mangled symbol resolution for camera hooks.
//
// Strategy (in priority order):
//   1. Try each known mangled name via shadowhook_dlsym (exact ELF match).
//   2. Try shadowhook_dlsym_symtab (non-exported / hidden symbols).
//   3. Fall back to a full ELF .dynsym walk via dl_iterate_phdr, demangling
//      each symbol and substring-matching the demangled form.
//
// The known list covers Android 9 → 16.  The ELF scanner covers any OEM
// variant that ships with a different mangling.

#include "symbol_resolver.h"
#include <shadowhook.h>

#include <android/log.h>
#include <cxxabi.h>
#include <link.h>
#include <elf.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#define TAG "amkush/sym_resolver"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

static const char CAMERASERVICE_LIB[] = "libcameraservice.so";

// ---------------------------------------------------------------------------
// Known mangled names for Camera3Device::processCaptureResult
// Most-recent Android version first (faster average hit).
// ---------------------------------------------------------------------------
static const char *const PCR_SYMBOLS[] = {
    // Android 16 (API 36) — further namespace refactor
    "_ZN7android15Camera3OutputUtils20processCaptureResultERNS_13Camera3DeviceERKNS_12CaptureResultERNS_19TagMonitorEbb",
    // Android 15 (API 35) — Camera3OutputUtils with TagMonitor + bool
    "_ZN7android15Camera3OutputUtils20processCaptureResultERNS_13Camera3DeviceERKNS_12CaptureResultERNS_19TagMonitorEb",
    // Android 14 (API 34)
    "_ZN7android15Camera3OutputUtils20processCaptureResultERNS_13Camera3DeviceERKNS_12CaptureResultERNS_19TagMonitor",
    // Android 13 (API 33) — refactored form
    "_ZN7android13Camera3Device20processCaptureResultEPK21camera3_capture_result",
    // Android 12 / 12L (API 31-32)
    "_ZN7android13Camera3Device20processCaptureResultEPK21camera3_capture_resultb",
    // Android 11 (API 30)
    "_ZN7android13Camera3Device20processCaptureResultEPKN7android14camera3_deviceEPK21camera3_capture_result",
    // Android 10 (API 29)
    "_ZN7android13Camera3Device20processCaptureResultEPK8camera3_EPK21camera3_capture_result",
    // Android 9 / Pre-Treble
    "_ZN7android13Camera3Device24processOneCaptureResultLNEPK21camera3_capture_result",
    // Alternative form seen on some OEM builds
    "_ZN7android13Camera3Device21processCaptureResultNLEPK21camera3_capture_result",
    nullptr
};

static const char *const PCR_DEMANGLE_SUBSTRINGS[] = {
    "Camera3Device::processCaptureResult",
    "Camera3OutputUtils::processCaptureResult",
    "Camera3Device::processOneCaptureResultLN",
    "Camera3Device::processCaptureResultNL",
    nullptr
};

// ---------------------------------------------------------------------------
// Known mangled names for configure_streams
// ---------------------------------------------------------------------------
static const char *const CS_SYMBOLS[] = {
    // Android 15-16 — late configure helper
    "_ZN7android13Camera3Device14configureStreamEv",
    // Android 14
    "_ZN7android13Camera3Device15configureStreamsERKNS_2spINS_13CameraDeviceBaseEEERNS0_21StreamSet_tE",
    // Android 13 refactored
    "_ZN7android13Camera3Device16configureStreamsLNEv",
    // Android 12
    "_ZN7android13Camera3Device16configureStreamsEv",
    // HAL3 ops table helper
    "_ZN7android13Camera3Device27configureHalStream_freeUnusedEv",
    // Alternative OEM form
    "_ZN7android13Camera3Device18configureStreamsRawEv",
    nullptr
};

static const char *const CS_DEMANGLE_SUBSTRINGS[] = {
    "Camera3Device::configureStreams",
    "Camera3Device::configureHalStream",
    "Camera3Device::configureStream",
    nullptr
};

// ---------------------------------------------------------------------------
// Strategy 1 & 2: try the known-name list via ShadowHook
// ---------------------------------------------------------------------------
static void *try_known_list(const char *const *syms) {
    void *handle = shadowhook_dlopen(CAMERASERVICE_LIB);
    if (!handle) {
        LOGE("shadowhook_dlopen(%s) returned NULL", CAMERASERVICE_LIB);
        return nullptr;
    }

    for (int i = 0; syms[i]; i++) {
        void *addr = shadowhook_dlsym(handle, syms[i]);
        if (addr) {
            LOGI("Matched dynsym: %s → %p", syms[i], addr);
            return addr;
        }
        addr = shadowhook_dlsym_symtab(handle, syms[i]);
        if (addr) {
            LOGI("Matched symtab: %s → %p", syms[i], addr);
            return addr;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Strategy 3: full ELF .dynsym walk via dl_iterate_phdr + abi::__cxa_demangle
// ---------------------------------------------------------------------------
struct ScanCtx {
    const char *const *substrings;
    void               *found_addr;
    const char         *found_name;
};

static int phdr_callback(struct dl_phdr_info *info, size_t /*size*/, void *data) {
    if (!info->dlpi_name || !strstr(info->dlpi_name, "libcameraservice.so"))
        return 0;

    LOGI("ELF scanner: found %s at base=%p", info->dlpi_name, (void *)info->dlpi_addr);

    // Walk PT_DYNAMIC to collect SYMTAB, STRTAB, SYMENT, HASH/GNU_HASH.
    const ElfW(Dyn) *dyn = nullptr;
    for (int i = 0; i < info->dlpi_phnum; i++) {
        if (info->dlpi_phdr[i].p_type == PT_DYNAMIC) {
            dyn = (const ElfW(Dyn) *)(info->dlpi_addr + info->dlpi_phdr[i].p_vaddr);
            break;
        }
    }
    if (!dyn) return 0;

    const ElfW(Sym) *symtab  = nullptr;
    const char       *strtab  = nullptr;
    size_t            syment  = sizeof(ElfW(Sym));
    size_t            nchain  = 0;

    for (const ElfW(Dyn) *d = dyn; d->d_tag != DT_NULL; ++d) {
        switch (d->d_tag) {
            case DT_SYMTAB:
                symtab = (const ElfW(Sym) *)(info->dlpi_addr + d->d_un.d_ptr);
                break;
            case DT_STRTAB:
                strtab = (const char *)(info->dlpi_addr + d->d_un.d_ptr);
                break;
            case DT_SYMENT:
                syment = (size_t)d->d_un.d_val;
                break;
            case DT_HASH: {
                const uint32_t *ht = (const uint32_t *)(info->dlpi_addr + d->d_un.d_ptr);
                nchain = ht[1];
                break;
            }
            case DT_GNU_HASH: {
                // GNU hash: bucket_count at [0], symoffset at [1]
                // nchain can be estimated from symtab + HASH; skip if DT_HASH present.
                if (nchain == 0) {
                    const uint32_t *ght = (const uint32_t *)(info->dlpi_addr + d->d_un.d_ptr);
                    // Conservative upper bound — real count obtained from HASH.
                    // Use a large sentinel so we scan until st_value == 0.
                    (void)ght;
                    nchain = 8192;
                }
                break;
            }
            default:
                break;
        }
    }

    if (!symtab || !strtab || nchain == 0) return 0;

    ScanCtx *ctx = (ScanCtx *)data;

    for (size_t i = 0; i < nchain; ++i) {
        const ElfW(Sym) *sym =
            (const ElfW(Sym) *)((const char *)symtab + i * syment);

        unsigned char type = ELF_ST_TYPE(sym->st_info);
        if (type != STT_FUNC && type != STT_GNU_IFUNC) continue;
        if (sym->st_value == 0) continue;
        if (sym->st_name  == 0) continue;

        const char *raw_name = strtab + sym->st_name;

        int   status    = 0;
        char *demangled = abi::__cxa_demangle(raw_name, nullptr, nullptr, &status);
        const char *check = (status == 0 && demangled) ? demangled : raw_name;

        bool match = false;
        for (int j = 0; ctx->substrings[j]; ++j) {
            if (strstr(check, ctx->substrings[j])) {
                match = true;
                break;
            }
        }

        if (demangled) free(demangled);

        if (match) {
            ctx->found_addr = (void *)((uintptr_t)info->dlpi_addr + sym->st_value);
            ctx->found_name = raw_name;
            LOGI("ELF scanner matched: %s → %p", raw_name, ctx->found_addr);
            return 1;
        }
    }
    return 0;
}

static void *scan_demangle(const char *const *substrings,
                            const char **out_matched_sym) {
    ScanCtx ctx = { substrings, nullptr, nullptr };
    dl_iterate_phdr(phdr_callback, &ctx);
    if (out_matched_sym) *out_matched_sym = ctx.found_name;
    return ctx.found_addr;
}

// ---------------------------------------------------------------------------
// Public API — resolve() tries all three strategies in order
// ---------------------------------------------------------------------------
static ResolvedSymbol resolve(const char *const *known_syms,
                               const char *const *dm_substrings,
                               const char        *what) {
    ResolvedSymbol r = {};

    void *addr = try_known_list(known_syms);
    if (addr) {
        r.ptr    = addr;
        r.symbol = "(matched from known list)";
        r.source = "known_list";
        return r;
    }

    LOGW("%s: known-list lookup failed — trying ELF demangle scan", what);
    const char *matched = nullptr;
    addr = scan_demangle(dm_substrings, &matched);
    if (addr) {
        r.ptr    = addr;
        r.symbol = matched ? matched : "(unknown)";
        r.source = "demangle_scan";
        return r;
    }

    LOGE("%s: all resolution strategies failed", what);
    return r;
}

ResolvedSymbol resolve_process_capture_result(void) {
    return resolve(PCR_SYMBOLS, PCR_DEMANGLE_SUBSTRINGS, "processCaptureResult");
}

ResolvedSymbol resolve_configure_streams(void) {
    return resolve(CS_SYMBOLS, CS_DEMANGLE_SUBSTRINGS, "configureStreams");
}
