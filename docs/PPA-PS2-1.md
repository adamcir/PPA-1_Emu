# PPA-PS/2/1 — physical QWERTY PS/2 keyboard

PPA-PS/2/1 is a PPAMOD v1 card with module type **04h**.
It connects a virtual PS/2 MF2 keyboard to PPA-BUS/1 through registers.
The module has its own resizable **VIEW** window showing a US QWERTY
keyboard; pressing or releasing keys while that SDL window has keyboard
focus feeds **PS/2 scan-code set 2** bytes into a bounded FIFO.
Keys are highlighted while held; typed text is displayed for convenience
in the VIEW window. The text preview is **UI only**, not a CPU register.

## Installation and use

Run `make`. In the emulator's MODULES window, with CPU power off:

1. LOAD `out/modules/ppa-ps2-1.ppamod` into an empty slot.
2. ACTIVATE the card and press VIEW.
3. Click the keyboard window to give it keyboard focus.
4. Type on the physical keyboard. The virtual keys highlight and
   the module receives Make/Break scan codes.

The card consumes only the **first register bank** of its PPAM slot.
SLOT 1 maps to bank 2000h, SLOT 2 to bank 3000h, SLOT 3 to 4000h, etc.
The demo `examples/ps2_demo.asm` expects the keyboard in SLOT 2 and
logs raw scan bytes into RAM bank 1000h for inspection.

The physical position of host keys (SDL scancodes) is translated to
**US QWERTY** Set 2; this intentionally does not depend on the host
keyboard's software layout. Use the on-screen text preview to check
what the host keyboard layout actually types. The host OS handles
character composition, while the guest reads raw PS/2 scan codes.

## Registers (addresses in slot's register bank)

| Addr | Access | Register | Meaning |
|---|---|---|---|
| 0000–0003 | R | PPAM ID | `P`, `P`, `04h`, `01h` |
| 0010 | R | DATA | Pop next scan byte; 00h if empty |
| 0011 | R | STATUS | bit0 ready, bit1 FIFO overflow, bit2 scanning |
| 0012 | R/W | CONTROL | bit0 enable scan; bit7 clear FIFO/overflow |
| 0013 | W | COMMAND | keyboard commands (see below) |
| 0014 | R | COUNT | queued bytes (saturated at 255) |
| 0015 | R | LAST | last make/break scan code without prefixes |
| 0016 | R | LEDS | Scroll/Num/Caps lock LED mask |

The queue holds **256 bytes** and preserves complete key sequences
as individual bytes, so the guest must consume them promptly.

## Make/break format (Set 2)

- Press Q: `15`
- Release Q: `F0 15`
- Press Up Arrow: `E0 75`
- Release Up Arrow: `E0 F0 75`

Auto-repeat KEY_DOWN events from SDL are ignored; the card emits one
make event per physical press and the matching break event on release.
Losing keyboard-window focus releases held keys to prevent sticky
modifiers. The virtual keyboard does **not** synthesize ASCII characters
in its hardware register. A guest driver should interpret Set 2 bytes
and maintain shift/caps state separately.

The module supports a useful subset of PS/2 keyboard commands written
to 0013h: `FF` reset (responds `FA AA`), `F4` scan enable
(`FA`), `F5` scan disable (`FA`), `F2` identify
(`FA AB 83`), `ED` set LEDs (ACK, then send LED mask and get
another ACK), and `EE` echo (`EE`). Unsupported commands respond
`FE`. This models a practical keyboard byte-stream interface, **not**
the complete Intel 8042 controller, clock/data electrical timings or
full PS/2 scan-set negotiation.

## CPU example (SLOT 2)

```asm
    MOV BR-HI,#30
    MOV BR-LO,#00
.poll:
    LD A,$0011
    AND A,#01
    JZ .poll
    LD A,$0010      ; pop next scan-code byte into A
```

The keyboard does not directly write into the VGA framebuffer; the
guest program must read these scan codes and choose how to display them.
