#include "mouse.h"

#include <stdint.h>

#include "display.h"
#include "irq.h"
#include "klog.h"
#include "syscall_abi.h"
#include "utils.h"

#define PS2_DATA_PORT    0x60U
#define PS2_STATUS_PORT  0x64U
#define PS2_COMMAND_PORT 0x64U

#define PS2_STATUS_OUTPUT_FULL 0x01U
#define PS2_STATUS_INPUT_FULL  0x02U

static uint8_t g_mouse_packet[3];
static uint32_t g_mouse_packet_len;

static int mouse_wait_input_clear(void) {
    for (uint32_t i = 0; i < 100000U; i++) {
        if ((inb(PS2_STATUS_PORT) & PS2_STATUS_INPUT_FULL) == 0U) {
            return 0;
        }
        io_wait();
    }
    return -1;
}

static int mouse_wait_output_full(void) {
    for (uint32_t i = 0; i < 100000U; i++) {
        if ((inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) != 0U) {
            return 0;
        }
        io_wait();
    }
    return -1;
}

static void mouse_write_device(uint8_t value) {
    (void)mouse_wait_input_clear();
    outb(PS2_COMMAND_PORT, 0xD4U);
    (void)mouse_wait_input_clear();
    outb(PS2_DATA_PORT, value);
}

static int mouse_read_data(uint8_t *out_value) {
    if (!out_value) {
        return -1;
    }
    if (mouse_wait_output_full() < 0) {
        return -1;
    }
    *out_value = inb(PS2_DATA_PORT);
    return 0;
}

static void mouse_handler(void *ctx) {
    uint8_t packet0;
    uint32_t buttons;
    int32_t dx;
    int32_t dy;

    (void)ctx;
    g_mouse_packet[g_mouse_packet_len++] = inb(PS2_DATA_PORT);
    if (g_mouse_packet_len < 3U) {
        return;
    }
    g_mouse_packet_len = 0U;

    packet0 = g_mouse_packet[0];
    if ((packet0 & 0x08U) == 0U) {
        return;
    }

    dx = (int32_t)(int8_t)g_mouse_packet[1];
    dy = (int32_t)(int8_t)g_mouse_packet[2];
    buttons = 0U;
    if ((packet0 & 0x01U) != 0U) {
        buttons |= SYSCALL_GUI_BUTTON_LEFT;
    }
    if ((packet0 & 0x02U) != 0U) {
        buttons |= SYSCALL_GUI_BUTTON_RIGHT;
    }

    if (display_gui_mode_active()) {
        display_gui_handle_mouse_motion(dx, -dy);
        display_gui_handle_mouse_buttons(buttons);
    }
}

void mouse_init(void) {
    uint8_t config = 0U;
    uint8_t ack = 0U;

    irq_register(12, mouse_handler, 0);

    (void)mouse_wait_input_clear();
    outb(PS2_COMMAND_PORT, 0xA8U);

    (void)mouse_wait_input_clear();
    outb(PS2_COMMAND_PORT, 0x20U);
    if (mouse_read_data(&config) < 0) {
        KLOGW("mouse: controller config read timeout");
        irq_mask(12, 0);
        return;
    }

    config |= 0x02U;
    config &= (uint8_t)~0x20U;
    (void)mouse_wait_input_clear();
    outb(PS2_COMMAND_PORT, 0x60U);
    (void)mouse_wait_input_clear();
    outb(PS2_DATA_PORT, config);

    mouse_write_device(0xF6U);
    (void)mouse_read_data(&ack);
    mouse_write_device(0xF4U);
    if (mouse_read_data(&ack) < 0 || ack != 0xFAU) {
        KLOGW("mouse: device enable ack missing");
    } else {
        KLOGI("mouse: ps2 auxiliary device enabled");
    }

    irq_mask(12, 0);
}
