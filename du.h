#ifndef DUDU_H
#define DUDU_H

#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>

// function prototypes
uintmax_t du(char *);
uintmax_t du_cache(char *);
uintmax_t du_cache_ex(char *path, int cache_level);
uintmax_t du_cache_ex2(char *path, int force_renew, int cache_level);

#ifdef __cplusplus
}
#endif
#endif
