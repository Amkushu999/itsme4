#pragma once

typedef struct {
    void       *ptr;
    const char *symbol;
    const char *source;
} ResolvedSymbol;

#ifdef __cplusplus
extern "C" {
#endif

ResolvedSymbol resolve_process_capture_result(void);
ResolvedSymbol resolve_configure_streams(void);

#ifdef __cplusplus
}
#endif
