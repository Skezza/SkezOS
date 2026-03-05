#ifndef KPIPE_H
#define KPIPE_H

#include <stdint.h>

struct kfile;

/* Create a new anonymous pipe and initialize read/write endpoint kfiles. */
int kpipe_create(struct kfile *out_read_end, struct kfile *out_write_end);

#endif /* KPIPE_H */
