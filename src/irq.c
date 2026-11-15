#include "irq.h"
void ppa_irq_reset(PpaIrqController *irq) {
    if (!irq) return;
    *irq = (PpaIrqController){0};
    irq->active = 0xFF;
}
void ppa_irq_request(PpaIrqController *irq, unsigned line) {
    if (irq && line < PPA_IRQ_LINES)
        irq->pending |= (uint8_t)(1u << line);
}
int ppa_irq_next(const PpaIrqController *irq) {
    if (!irq || irq->servicing) return -1;
    uint8_t eligible = irq->pending & irq->mask;
    for (int line = 0; line < PPA_IRQ_LINES; ++line)
        if (eligible & (1u << line)) return line; /* fixed priority IRQ0 first */
    return -1;
}
void ppa_irq_enter(PpaIrqController *irq, unsigned line) {
    if (!irq || line >= PPA_IRQ_LINES) return;
    irq->active = (uint8_t)line;
    irq->servicing = true;
}
void ppa_irq_leave(PpaIrqController *irq) {
    if (!irq) return;
    irq->servicing = false;
    irq->active = 0xFF;
}
void ppa_irq_tick(PpaIrqController *irq) {
    if (!irq || !(irq->timer_control & 1) || !irq->timer_reload) return;
    if (irq->timer_count) --irq->timer_count;
    if (!irq->timer_count) {
        ppa_irq_request(irq, PPA_IRQ_TIMER);
        if (irq->timer_control & 2) irq->timer_count = irq->timer_reload;
        else irq->timer_control &= (uint8_t)~1u;
    }
}
uint8_t ppa_irq_read(const PpaIrqController *irq, uint16_t address) {
    if (!irq) return 0;
    switch (address) {
        case PPA_IRQ_PENDING: return irq->pending;
        case PPA_IRQ_MASK: return irq->mask;
        case PPA_IRQ_ACK: return 0;
        case PPA_IRQ_STATUS: return (uint8_t)((irq->servicing ? 1 : 0) |
                                              ((irq->timer_control & 1) ? 2 : 0));
        case PPA_IRQ_ACTIVE: return irq->active;
        case PPA_TIMER_LO: return (uint8_t)irq->timer_reload;
        case PPA_TIMER_HI: return (uint8_t)(irq->timer_reload >> 8);
        case PPA_TIMER_CONTROL: return irq->timer_control;
        case PPA_TIMER_COUNT_LO: return (uint8_t)irq->timer_count;
        case PPA_TIMER_COUNT_HI: return (uint8_t)(irq->timer_count >> 8);
        default: return 0xFF;
    }
}
void ppa_irq_write(PpaIrqController *irq, uint16_t address, uint8_t value) {
    if (!irq) return;
    switch (address) {
        case PPA_IRQ_MASK: irq->mask = value; break;
        case PPA_IRQ_ACK: irq->pending &= (uint8_t)~value; break; /* W1C */
        case PPA_TIMER_LO:
            irq->timer_reload = (uint16_t)((irq->timer_reload & 0xFF00u) | value);
            break;
        case PPA_TIMER_HI:
            irq->timer_reload = (uint16_t)((irq->timer_reload & 0x00FFu) | ((uint16_t)value << 8));
            break;
        case PPA_TIMER_CONTROL:
            irq->timer_control = value & 3u;
            if (irq->timer_control & 1) irq->timer_count = irq->timer_reload;
            else irq->timer_count = 0;
            break;
        default: break;
    }
}
