#include "kpipe.h"

#include <stdint.h>

#include "display.h"
#include "kerrno.h"
#include "kfile.h"
#include "kmalloc.h"
#include "sched.h"
#include "utils.h"

#define KPIPE_CAPACITY 512U
#define KFILE_PIPE_READ_ENDPOINT  (1U << 0)
#define KFILE_PIPE_WRITE_ENDPOINT (1U << 1)

struct kpipe_state {
    uint8_t buf[KPIPE_CAPACITY];
    uint32_t head;
    uint32_t tail;
    uint32_t used;
    uint32_t reader_refs;
    uint32_t writer_refs;
};

static int kpipe_retain(struct kfile *file);
static int kpipe_read(struct kfile *file, void *buf, uint32_t len, uint32_t *out_read);
static int kpipe_write(struct kfile *file, const void *buf, uint32_t len, uint32_t *out_written);
static int kpipe_close(struct kfile *file);
static void kpipe_wait_once(void);

static const struct kfile_ops g_kpipe_file_ops = {
    .read = kpipe_read,
    .write = kpipe_write,
    .retain = kpipe_retain,
    .close = kpipe_close,
};

int kpipe_create(struct kfile *out_read_end, struct kfile *out_write_end) {
    struct kpipe_state *state;

    if (!out_read_end || !out_write_end) {
        return -KERR_INVAL;
    }

    state = (struct kpipe_state *)kmalloc(sizeof(*state));
    if (!state) {
        return -KERR_NOMEM;
    }
    memset(state, 0, sizeof(*state));
    state->reader_refs = 1U;
    state->writer_refs = 1U;

    kfile_init(out_read_end, 0, &g_kpipe_file_ops, (void *)state, KFILE_PIPE_READ_ENDPOINT);
    kfile_init(out_write_end, 0, &g_kpipe_file_ops, (void *)state, KFILE_PIPE_WRITE_ENDPOINT);
    return 0;
}

static int kpipe_retain(struct kfile *file) {
    struct kpipe_state *state;
    uint32_t saved_flags;

    if (!file || !file->backend_private) {
        return -KERR_INVAL;
    }

    state = (struct kpipe_state *)file->backend_private;
    saved_flags = display_console_enter_critical();
    if ((file->flags & KFILE_PIPE_READ_ENDPOINT) != 0U) {
        state->reader_refs++;
    }
    if ((file->flags & KFILE_PIPE_WRITE_ENDPOINT) != 0U) {
        state->writer_refs++;
    }
    display_console_leave_critical(saved_flags);
    return 0;
}

static int kpipe_read(struct kfile *file, void *buf, uint32_t len, uint32_t *out_read) {
    struct kpipe_state *state;
    uint8_t *dst = (uint8_t *)buf;
    uint32_t total = 0U;

    if (!file || !file->backend_private || ((file->flags & KFILE_PIPE_READ_ENDPOINT) == 0U)) {
        return -KERR_INVAL;
    }
    if (!buf && len != 0U) {
        return -KERR_INVAL;
    }
    if (out_read) {
        *out_read = 0U;
    }
    if (len == 0U) {
        return 0;
    }

    state = (struct kpipe_state *)file->backend_private;
    for (;;) {
        uint32_t saved_flags = display_console_enter_critical();

        while (state->used != 0U && total < len) {
            dst[total++] = state->buf[state->head];
            state->head = (state->head + 1U) % KPIPE_CAPACITY;
            state->used--;
        }
        if (total != 0U) {
            display_console_leave_critical(saved_flags);
            if (out_read) {
                *out_read = total;
            }
            return 0;
        }
        if (state->writer_refs == 0U) {
            display_console_leave_critical(saved_flags);
            if (out_read) {
                *out_read = 0U;
            }
            return 0;
        }
        display_console_leave_critical(saved_flags);
        kpipe_wait_once();
    }
}

static int kpipe_write(struct kfile *file, const void *buf, uint32_t len, uint32_t *out_written) {
    struct kpipe_state *state;
    const uint8_t *src = (const uint8_t *)buf;
    uint32_t total = 0U;

    if (!file || !file->backend_private || ((file->flags & KFILE_PIPE_WRITE_ENDPOINT) == 0U)) {
        return -KERR_INVAL;
    }
    if (!buf && len != 0U) {
        return -KERR_INVAL;
    }
    if (out_written) {
        *out_written = 0U;
    }
    if (len == 0U) {
        return 0;
    }

    state = (struct kpipe_state *)file->backend_private;
    while (total < len) {
        uint32_t saved_flags = display_console_enter_critical();

        if (state->reader_refs == 0U) {
            display_console_leave_critical(saved_flags);
            if (out_written) {
                *out_written = total;
            }
            return -KERR_PIPE;
        }
        while (state->used < KPIPE_CAPACITY && total < len) {
            state->buf[state->tail] = src[total++];
            state->tail = (state->tail + 1U) % KPIPE_CAPACITY;
            state->used++;
        }
        display_console_leave_critical(saved_flags);

        if (total < len) {
            kpipe_wait_once();
        }
    }

    if (out_written) {
        *out_written = total;
    }
    return 0;
}

static int kpipe_close(struct kfile *file) {
    struct kpipe_state *state;
    uint32_t saved_flags;
    int free_state = 0;

    if (!file || !file->backend_private) {
        return -KERR_INVAL;
    }

    state = (struct kpipe_state *)file->backend_private;
    saved_flags = display_console_enter_critical();
    if ((file->flags & KFILE_PIPE_READ_ENDPOINT) != 0U && state->reader_refs != 0U) {
        state->reader_refs--;
    }
    if ((file->flags & KFILE_PIPE_WRITE_ENDPOINT) != 0U && state->writer_refs != 0U) {
        state->writer_refs--;
    }
    if (state->reader_refs == 0U && state->writer_refs == 0U) {
        free_state = 1;
    }
    display_console_leave_critical(saved_flags);

    if (free_state) {
        kfree(state);
    }
    return 0;
}

static void kpipe_wait_once(void) {
    if (sched_current_task_pid() > 0) {
        sched_sleep_ticks(1U);
    }
}
