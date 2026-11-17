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
    /* Divider /4 and 3-microtick period: wait decrements after 12 edges. */
    ppa_irq_write(&irq, PPA_TIMER_CONTROL, 0);
    ppa_irq_write(&irq, PPA_TIMER_PRESCALE, 1);
    ppa_irq_write(&irq, PPA_TIMER_LO, 3);
    ppa_irq_write(&irq, PPA_TIMER_HI, 0);
    ppa_irq_write(&irq, PPA_TIMER_CONTROL, 3);
    ppa_irq_write(&irq, PPA_TIMER_WAIT_LO, 2);
    ppa_irq_write(&irq, PPA_TIMER_WAIT_HI, 0);
    for (int i = 0; i < 11; ++i) ppa_irq_tick(&irq);
    assert(ppa_irq_read(&irq, PPA_TIMER_WAIT_LO) == 2);
    ppa_irq_tick(&irq);
    assert(ppa_irq_read(&irq, PPA_TIMER_WAIT_LO) == 1);
    for (int i = 0; i < 12; ++i) ppa_irq_tick(&irq);
    assert(ppa_irq_read(&irq, PPA_TIMER_WAIT_LO) == 0);
    /* Re-arming halfway through a period resets its phase and provides
     * the FULL duration from this hardware register write. */
    for (int i = 0; i < 7; ++i) ppa_irq_tick(&irq);
    ppa_irq_write(&irq, PPA_TIMER_WAIT_LO, 1);
    ppa_irq_write(&irq, PPA_TIMER_WAIT_HI, 0);
    for (int i = 0; i < 11; ++i) ppa_irq_tick(&irq);
    assert(ppa_irq_read(&irq, PPA_TIMER_WAIT_LO) == 1);
    ppa_irq_tick(&irq);
    assert(ppa_irq_read(&irq, PPA_TIMER_WAIT_LO) == 0);
    for (int i = 0; i < 100; ++i) ppa_irq_tick(&irq);
    assert(ppa_irq_read(&irq, PPA_TIMER_WAIT_LO) == 0);
    /* High-byte write loads both bytes into a single 16-bit counter. */
    ppa_irq_write(&irq, PPA_TIMER_WAIT_LO, 1);
    ppa_irq_write(&irq, PPA_TIMER_WAIT_HI, 1);
    assert(ppa_irq_read(&irq, PPA_TIMER_WAIT_LO) == 1);
    assert(ppa_irq_read(&irq, PPA_TIMER_WAIT_HI) == 1);
    ppa_irq_write(&irq, PPA_TIMER_CONTROL, 0);
    assert(ppa_irq_read(&irq, PPA_TIMER_WAIT_LO) == 0);
    puts("PPA-1 IRQ controller tests OK");
    return 0;
}
