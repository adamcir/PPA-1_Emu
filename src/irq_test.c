#include "irq.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    PpaIrqController irq;
    ppa_irq_reset(&irq);
    assert(irq.active == 255 && ppa_irq_next(&irq) == -1);
    ppa_irq_request(&irq, 2);
    assert(ppa_irq_next(&irq) == -1);
    ppa_irq_write(&irq, PPA_IRQ_MASK, 0x05);
    assert(ppa_irq_next(&irq) == 2);
    ppa_irq_enter(&irq, 2);
    assert(ppa_irq_next(&irq) == -1);
    assert(ppa_irq_read(&irq, PPA_IRQ_ACTIVE) == 2);
    ppa_irq_write(&irq, PPA_IRQ_ACK, 4);
    ppa_irq_leave(&irq);
    assert(ppa_irq_next(&irq) == -1);
    ppa_irq_write(&irq, PPA_TIMER_LO, 3);
    ppa_irq_write(&irq, PPA_TIMER_CONTROL, 3);
    ppa_irq_tick(&irq); ppa_irq_tick(&irq);
    assert(ppa_irq_next(&irq) == -1);
    ppa_irq_tick(&irq);
    assert(ppa_irq_next(&irq) == 0);
    assert(ppa_irq_read(&irq, PPA_TIMER_COUNT_LO) == 3);
    ppa_irq_write(&irq, PPA_IRQ_ACK, 1);
    assert(ppa_irq_next(&irq) == -1);
    puts("PPA-1 IRQ controller tests OK");
    return 0;
}
