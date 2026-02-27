#ifndef KASSERT_H
#define KASSERT_H

#include "panic.h"

#define KASSERT(expr) \
    do { \
        if (!(expr)) { \
            panic_assert_failed(#expr, __FILE__, __LINE__); \
        } \
    } while (0)

#endif /* KASSERT_H */
