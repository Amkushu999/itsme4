// shadowhook.h — Public C API for ByteDance ShadowHook 2.x (android-inline-hook)
//
// Source: https://github.com/bytedance/android-inline-hook
// License: MIT
//
// This header is committed for local/CI builds that don't go through the AAR
// prefab.  It covers exactly the subset of the ShadowHook 2.x API that is used
// by hook_proxy.cpp and symbol_resolver.cpp.  No extra symbols are declared so
// there is no risk of conflicting with an upstream header supplied by the AAR.
//
// Functions / macros used in this module:
//   shadowhook_init()
//   shadowhook_get_errno()
//   shadowhook_to_errmsg()
//   shadowhook_hook_sym_addr()
//   shadowhook_unhook()
//   shadowhook_dlopen()
//   shadowhook_dlsym()
//   shadowhook_dlsym_symtab()
//   SHADOWHOOK_STACK_SCOPE()

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Mode
// ---------------------------------------------------------------------------
typedef enum shadowhook_mode {
    SHADOWHOOK_MODE_SHARED = 0,  // multiple hooks can coexist on one address
    SHADOWHOOK_MODE_UNIQUE = 1,  // at most one hook per address (this module's mode)
} shadowhook_mode_t;

// ---------------------------------------------------------------------------
// Initialisation
//
// Must be called once before any other ShadowHook call.
//   mode       — SHADOWHOOK_MODE_UNIQUE or SHADOWHOOK_MODE_SHARED
//   debuggable — enable verbose logging
//
// Returns 0 on success.
// Returns non-zero on error; call shadowhook_get_errno() for the code.
// Error code 1 means "already initialised" — benign on duplicate calls.
// ---------------------------------------------------------------------------
int shadowhook_init(shadowhook_mode_t mode, bool debuggable);

// ---------------------------------------------------------------------------
// Error handling
// ---------------------------------------------------------------------------

// Thread-local last error code.  0 = no error.
int shadowhook_get_errno(void);

// Human-readable string for a ShadowHook error code.
const char *shadowhook_to_errmsg(int error_number);

// ---------------------------------------------------------------------------
// Symbol-address hook / unhook
//
// shadowhook_hook_sym_addr()
//   sym_addr  — address of the function to hook (from dlsym or manual resolve)
//   new_func  — replacement; must match the original's calling convention
//   orig_func — (out) trampoline through which the original can be called;
//               in UNIQUE mode this is a direct function pointer
//
// Returns an opaque stub on success, NULL on failure.
//
// shadowhook_unhook()
//   stub — value returned by a successful shadowhook_hook_*; must not be NULL
//
// Returns 0 on success.
// ---------------------------------------------------------------------------
void *shadowhook_hook_sym_addr(void *sym_addr, void *new_func, void **orig_func);

int shadowhook_unhook(void *stub);

// ---------------------------------------------------------------------------
// Dynamic-linker helpers
//
// These bypass Android's linker-namespace restrictions and can expose symbols
// that are hidden from the standard dlopen/dlsym.
//
// shadowhook_dlopen()   — open a library that is already loaded in the process.
//                         Returns an opaque handle; NOT a real dlopen handle —
//                         do NOT pass it to dlclose().
// shadowhook_dlsym()    — look up a symbol in .dynsym (exported symbols).
// shadowhook_dlsym_symtab() — look up a symbol in the full .symtab table
//                             (includes unexported/hidden symbols; slower).
// shadowhook_dlclose()  — release the handle from shadowhook_dlopen().
// ---------------------------------------------------------------------------
void *shadowhook_dlopen(const char *lib_name);
void *shadowhook_dlsym(void *handle, const char *sym_name);
void *shadowhook_dlsym_symtab(void *handle, const char *sym_name);
void  shadowhook_dlclose(void *handle);   // returns void — matches upstream 2.x

// ---------------------------------------------------------------------------
// SHADOWHOOK_STACK_SCOPE()
//
// Place this macro at the very top of every hook proxy function body.
//
// In UNIQUE mode (which this module always uses) it is a pure no-op — the
// original function is called directly through the orig_func pointer captured
// by shadowhook_hook_sym_addr().
//
// In SHARED mode it manages a thread-local trampoline-stack so that multiple
// hook layers can call through to the previous one via SHADOWHOOK_CALL_PREV.
// The upstream implementation is an internal detail of the shared library;
// this header simply provides the correct no-op expansion for UNIQUE mode.
//
// If you ever switch this module to SHADOWHOOK_MODE_SHARED you will need to
// link against the real libshadowhook and let its prefab header take over.
// ---------------------------------------------------------------------------
#define SHADOWHOOK_STACK_SCOPE()  do { /* no-op in UNIQUE mode */ } while (0)

#ifdef __cplusplus
}
#endif
