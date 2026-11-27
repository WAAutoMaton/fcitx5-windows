#ifndef FCITX5_WINDOWS_XKBCOMMON_SHIM_H
#define FCITX5_WINDOWS_XKBCOMMON_SHIM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t xkb_keysym_t;

#define XKB_KEY_NoSymbol 0x000000

enum xkb_keysym_flags {
    XKB_KEYSYM_NO_FLAGS = 0,
    XKB_KEYSYM_CASE_INSENSITIVE = (1 << 0)
};

int xkb_keysym_get_name(xkb_keysym_t keysym, char *buffer, size_t size);
xkb_keysym_t xkb_keysym_from_name(const char *name,
                                  enum xkb_keysym_flags flags);
int xkb_keysym_to_utf8(xkb_keysym_t keysym, char *buffer, size_t size);

#ifdef __cplusplus
}
#endif

#endif
