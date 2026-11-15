# PPA-1 interrupt controller (IRQ/1) — hardware specification v1

The CPU is microstepped and uses a byte-wide data bus. This is a
**buildable, deterministic controller**, not a simulated host-level callback.
All interrupt entry/return steps use the existing CPU stack bus and
program-memory read bus.

## CPU instruction encodings (from PPA-1 register/opcode table)

| Opcode | Instruction | Bytes | Effect |
|---|---|---|---|
| 28 | EI | 1 | SR.I <- 1 |
| 29 | DI | 1 | SR.I <- 0 |
| 2A | IRET | 1 | restore saved SR, BR and PC |
| 2B | INT #n | 2 | synchronous interrupt, n = 0..7 |

IVR-HI = operational register A(hex), IVR-LO = B(hex).
SR bit2 (mask 04) is I, alongside Z=01 and C=02.
RESET clears SR.I, IVR, pending, masks, timer and active IRQ.

## Controller (internal IO, bank F000)

| Offset | Register | Access | Meaning |
|---|---|---|---|
| FF10 | IPR | R | edge-latched pending requests, bits 0..7 |
| FF11 | IMR | RW | IRQ enable mask, bits 0..7 |
| FF12 | IRQ-ACK | W | write-one-to-clear IPR bits |
| FF13 | ISR | R | bit0 in service; bit1 timer running |
| FF14 | IAR | R | currently serviced IRQ 0..7, FF if idle |
| FF20 | TIMER-LO | RW | low byte of 16-bit reload count |
| FF21 | TIMER-HI | RW | high byte of 16-bit reload count |
| FF22 | TIMER-CTRL | RW | bit0 enable, bit1 periodic |
| FF23 | TIMER-COUNT-LO | R | current countdown low byte |
| FF24 | TIMER-COUNT-HI | R | current countdown high byte |

8 IRQ lines: IRQ0 is the internal microcycle timer, IRQ2 is SERIAL/1
receiver (raised at RX_READY transition). IRQ1/3..7 are available for
future PPAMOD and external hardware. A request sets a bit in IPR even
when masked. The controller selects the lowest-numbered eligible
pending line: `IPR & IMR`. Pending bits remain set until software writes
IRQ-ACK. During an ISR no new interrupt is accepted (no nesting, v1).
IRQ0 timer counts one tick per CPU microstep and latches IRQ0 on zero.
A TIMER-CTRL write with enable set reloads the counter. Reload=0
disables countdown. Oneshot clears enable when countdown reaches zero.

## Vector table and IRQ bus sequence

The 16-bit IVR holds a **byte offset in EEPROM bank 0000**.
Vectors are 2 bytes each, high byte first:
`0000:(IVR + 2*n)` = handler PC high;
`0000:(IVR + 2*n + 1)` = handler PC low.
IVR must leave room for 16 bytes (for instance FF00) in bank 0000.
BR does **not** select the vector bank. This deliberately matches
the PPA-1's existing fixed bank-0000 instruction fetch and can be
implemented without extra instruction-bank hardware.

At an instruction boundary after an enabled IRQ, a microcoded hardware
sequence performs these seven microsteps:
1. PUSH PC high
2. PUSH PC low
3. PUSH BR high
4. PUSH BR low
5. PUSH SR; clear SR.I
6. READ vector high from EEPROM bank 0000
7. READ vector low; load PC

The stack is the existing last 256 bytes of installed RAM.
Software must configure at least 256 bytes of RAM. The handler saves
any general registers it uses with PUSH/POP. ISR should acknowledge its
source with IRQ-ACK before IRET; otherwise it may retrigger.

IRET performs five microsteps: POP SR, POP BR low, POP BR high, POP PC
low, POP PC high. It restores the instruction boundary PC and returns
controller state to idle. INT #n executes the same entry sequence but
**does not** require SR.I or IMR, does not set IPR, and cannot nest.
HLT remains a hard halt, not an interruptible sleep instruction.

## Minimal physical hardware design

- Eight IRQ input flip-flops feed the 8-bit IPR latch.
- IMR is an 8-bit register, bitwise AND with IPR.
- A priority encoder selects IRQ0 first.
- IAR is a 3-bit latch (plus valid flag); ISR is state decode.
- IRQ_ACK is a decoded write pulse that clears IPR flip-flops.
- Timer: 16-bit down-counter, reload register and control flip-flops.
- CPU: IVR two 8-bit latches, SR.I flip-flop, seven-step entry
  sequencer and five-step IRET sequencer.
- No extra interrupt stack is required. Each microstep performs
  at most one byte-wide bus transaction.

This is the **first implementation**; the table did not specify
priority, vectors, timing or nested IRQ semantics, so the preceding
choices are explicit extension rules, not claims about existing hardware.
