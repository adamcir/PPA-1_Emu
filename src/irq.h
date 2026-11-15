#ifndef PPA_IRQ_H
#define PPA_IRQ_H
#include <stdint.h>
#include <stdbool.h>

/* Eight edge-latched interrupt lines, plus a 16-bit hardware timer.
 * No host threads or callbacks: one deterministic controller tick per
 * emulated microcycle, suitable for a discrete-logic implementation.
 */
enum { PPA_IRQ_LINES = 8, PPA_IRQ_TIMER = 0, PPA_IRQ_SERIAL_RX = 2 };
enum {
    PPA_IRQ_PENDING = 0xFF10, PPA_IRQ_MASK = 0xFF11,
    PPA_IRQ_ACK = 0xFF12, PPA_IRQ_STATUS = 0xFF13,
    PPA_IRQ_ACTIVE = 0xFF14,
    PPA_TIMER_LO = 0xFF20, PPA_TIMER_HI = 0xFF21,
    PPA_TIMER_CONTROL = 0xFF22,
    PPA_TIMER_COUNT_LO = 0xFF23, PPA_TIMER_COUNT_HI = 0xFF24
};
typedef struct {
    uint8_t pending;
    uint8_t mask;
    uint8_t active;   /* number 0..7, or FF */
    bool servicing;
    uint16_t timer_reload;
    uint16_t timer_count;
    uint8_t timer_control; /* bit0 enable, bit1 periodic */
} PpaIrqController;

void ppa_irq_reset(PpaIrqController *irq);
void ppa_irq_request(PpaIrqController *irq, unsigned line);
int ppa_irq_next(const PpaIrqController *irq);
void ppa_irq_enter(PpaIrqController *irq, unsigned line);
void ppa_irq_leave(PpaIrqController *irq);
void ppa_irq_tick(PpaIrqController *irq);
uint8_t ppa_irq_read(const PpaIrqController *irq, uint16_t address);
void ppa_irq_write(PpaIrqController *irq, uint16_t address, uint8_t value);
#endif
