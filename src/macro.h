#ifndef _MACRO_H
#define _MACRO_H

#include <stdlib.h>

static inline void freep(void *p) { free(*(void **)p); }
#define _cleanup_(x) __attribute__((cleanup(x)))
#define _cleanup_free_ _cleanup_(freep)

#define ARRAYSIZE(x) (sizeof(x)/sizeof(x[0]))

#endif  /* _MACRO_H */

