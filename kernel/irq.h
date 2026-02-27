#ifndef IRQ_H
#define IRQ_H

typedef void (*irq_fn_t)(void *ctx);

/* Register an IRQ handler for a given IRQ number (0-15).
 * Returns 0 on success, or a negative -KERR_* code on error.
 */
int irq_register(int irq, irq_fn_t handler, void *ctx);

/* Unregister the handler for a given IRQ number. */
void irq_unregister(int irq);

/* Mask or unmask the given IRQ line. If masked is non-zero, the IRQ is masked (disabled).
 * If zero, the IRQ is unmasked (enabled). */
void irq_mask(int irq, int masked);

/* Dispatch the handler for a given IRQ number. Called from the IRQ stubs. */
void irq_dispatch(int irq);

/* Initialise the IRQ stubs and mask all IRQs. Called during kernel init. */
void irq_init(void);

#endif /* IRQ_H */
