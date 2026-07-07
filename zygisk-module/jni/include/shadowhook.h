// shadowhook.h — Public C API for ByteDance ShadowHook 2.x (android-inline-hook)
//
// Source: https://github.com/bytedance/android-inline-hook
// License: MIT
//
// This header is committed to allow local builds without the AAR/prefab.
// For CI builds, this file is also present in the AAR's prefab module.
// Both paths expose the same API — this header is the authoritative copy.
//
// ShadowHook supports two operating modes:
//   SHADOWHOOK_MODE_SHARED  — multiple hooks on the same symbol (trampolines are shared)
//   SHADOWHOOK_MODE_UNIQUE  — at most one hook per address (faster, less overhead)

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
    SHADOWHOOK_MODE_SHARED = 0,
    SHADOWHOOK_MODE_UNIQUE = 1,
} shadowhook_mode_t;

// ---------------------------------------------------------------------------
// Initialisation
//
// Must be called once before any hook/unhook/dlopen/dlsym call.
//
//   mode       — SHADOWHOOK_MODE_UNIQUE or SHADOWHOOK_MODE_SHARED
//   debuggable — if true, enables extra logging / debug tracing
//
// Returns 0 on success.
// On failure, call shadowhook_get_errno() for the error code.
// ---------------------------------------------------------------------------
int shadowhook_init(shadowhook_mode_t mode, bool debuggable);

// ---------------------------------------------------------------------------
// Error handling
// ---------------------------------------------------------------------------

// Returns the most recent ShadowHook error code (thread-local).
// 0 means no error. 1 means "init already done" (benign duplicate-init).
int shadowhook_get_errno(void);

// Returns a human-readable string for a ShadowHook error code.
const char *shadowhook_to_errmsg(int error_number);

// ---------------------------------------------------------------------------
// Symbol-address hooks
//
// shadowhook_hook_sym_addr()
//   sym_addr  — the exact address of the symbol to hook (from dlsym or manual lookup)
//   new_func  — replacement function; must match the hooked function's calling convention
//   orig_func — (out) pointer to a trampoline that calls the original function
//
// Returns an opaque stub pointer on success, NULL on failure.
// Call shadowhook_get_errno() for error details on NULL return.
//
// shadowhook_unhook()
//   stub — the value returned by shadowhook_hook_*; must not be NULL
//
// Returns 0 on success.
// ---------------------------------------------------------------------------
void *shadowhook_hook_sym_addr(void *sym_addr, void *new_func, void **orig_func);

int shadowhook_unhook(void *stub);

// ---------------------------------------------------------------------------
// Symbol-name hooks (convenience wrappers when you have a library handle)
//
// shadowhook_hook_sym_name()
//   lib_name  — base name of the shared library (e.g. "libcameraservice.so")
//   sym_name  — mangled or unmangled symbol name
//   new_func  — replacement function
//   orig_func — (out) trampoline to original
//
// shadowhook_hook_sym_name_callback()
//   Same as above plus a hook-installed callback (can be NULL).
// ---------------------------------------------------------------------------
void *shadowhook_hook_sym_name(const char *lib_name,
                               const char *sym_name,
                               void       *new_func,
                               void      **orig_func);

void *shadowhook_hook_sym_name_callback(const char     *lib_name,
                                        const char     *sym_name,
                                        void           *new_func,
                                        void          **orig_func,
                                        void          (*installed_callback)(void *),
                                        void           *installed_callback_arg);

// ---------------------------------------------------------------------------
// Dynamic-linker helpers (ShadowHook's private dlopen/dlsym)
//
// These bypass Android's namespace restrictions and can find hidden symbols.
//
// shadowhook_dlopen()
//   libname — e.g. "libcameraservice.so"; must already be loaded in the process
//   Returns an opaque handle (NOT a real dlopen handle — do NOT pass to dlclose).
//
// shadowhook_dlsym()
//   Looks up a symbol in the .dynsym (exported) table.
//
// shadowhook_dlsym_symtab()
//   Looks up a symbol in the full .symtab (including unexported/hidden symbols).
//   Slower than shadowhook_dlsym; use as fallback.
//
// shadowhook_dlclose()
//   Releases the handle returned by shadowhook_dlopen.
// ---------------------------------------------------------------------------
void *shadowhook_dlopen(const char *lib_name);
void *shadowhook_dlsym(void *handle, const char *sym_name);
void *shadowhook_dlsym_symtab(void *handle, const char *sym_name);
int   shadowhook_dlclose(void *handle);

// ---------------------------------------------------------------------------
// SHADOWHOOK_STACK_SCOPE()
//
// Must be placed at the top of every hook proxy function body (UNIQUE mode).
// In UNIQUE mode it is a no-op macro; in SHARED mode it manages the call-stack
// trampoline so that a chain of hooks can call through to the original correctly.
//
// Usage:
//   static void my_hook(args...) {
//       SHADOWHOOK_STACK_SCOPE();
//       // ... your code ...
//       SHADOWHOOK_CALL_PREV(my_hook, args...);  // or call orig directly
//   }
//
// In UNIQUE mode the original function pointer is captured in the orig_func
// out-parameter of shadowhook_hook_sym_addr — call it directly.
// ---------------------------------------------------------------------------
#ifdef __cplusplus
#  define SHADOWHOOK_STACK_SCOPE() \
       shadowhook_stack_scope_t _sh_stack_scope_##__LINE__(shadowhook_get_stack())
#else
#  define SHADOWHOOK_STACK_SCOPE() \
       do { (void)shadowhook_get_stack(); } while(0)
#endif

// Internal helpers used by SHADOWHOOK_STACK_SCOPE in shared mode.
void *shadowhook_get_stack(void);

#ifdef __cplusplus
// RAII wrapper used by the C++ expansion of SHADOWHOOK_STACK_SCOPE.
class shadowhook_stack_scope_t {
public:
    explicit shadowhook_stack_scope_t(void *stack) : stack_(stack) {}
    ~shadowhook_stack_scope_t();
private:
    void *stack_;
};
#endif

// ---------------------------------------------------------------------------
// SHADOWHOOK_CALL_PREV — call the previous function in the hook chain (SHARED mode).
// In UNIQUE mode call your orig_func directly instead.
// ---------------------------------------------------------------------------
#define SHADOWHOOK_CALL_PREV(func, ...)                         \
    do {                                                        \
        __typeof__(func) _prev =                                \
            (__typeof__(func))shadowhook_get_prev_func(         \
                shadowhook_get_stack());                        \
        if (_prev) _prev(__VA_ARGS__);                         \
    } while(0)

void *shadowhook_get_prev_func(void *stack);

// ---------------------------------------------------------------------------
// Convenience: SHADOWHOOK_RETURN_ADDRESS()
// Returns the return address from inside a hook proxy (useful for stack walks).
// ---------------------------------------------------------------------------
uintptr_t shadowhook_get_return_address(void);

#ifdef __cplusplus
}
#endif
