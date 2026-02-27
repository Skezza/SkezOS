#ifndef KERRNO_H
#define KERRNO_H

/* Kernel-internal errno-style values. Functions should return
 * negative values (e.g. -KERR_INVAL) on failure.
 */
enum {
    KERR_NOENT  = 2,
    KERR_INVAL  = 22,
    KERR_NOMEM  = 12,
    KERR_FAULT  = 14,
    KERR_NOTSUP = 95,
};

#endif /* KERRNO_H */
