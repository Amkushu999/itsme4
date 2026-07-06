#pragma once
#ifdef __cplusplus
extern "C" {
#endif

int  crash_guard_init(void);
void crash_guard_cleanup(void);
int  crash_guard_enter(void);
void crash_guard_exit(void);

#ifdef __cplusplus
}
#endif
