#define _GNU_SOURCE
#define _XOPEN_SOURCE 700

/****************************************************************/
/* Simple PPA-1 Emulator project for C                           */
/* (c) 2026 Adam Cír (Adava), Adava Software, Adava Development */
/****************************************************************/

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>

#define BANK_SIZE       0x10000u
#define ROM_BANK_COUNT  0x40u
#define RAM_BANK_COUNT  0x10u
#define ROM_SIZE         (ROM_BANK_COUNT * BANK_SIZE)   /* 4 MiB */
#define RAM_SIZE         (RAM_BANK_COUNT * BANK_SIZE)   /* 1 MiB */
#define ROM_BANK_FIRST   0x0000u
#define ROM_BANK_LAST    0x003Fu
#define RAM_BANK_FIRST   0x0040u
#define RAM_BANK_LAST    0x004Fu
#define SYSTEM_BANK      0x0050u
#define STACK_PAGE       0xFE00u
#define SYSTEM_IO_START  0xFF00u
#define SYSTEM_IO_END    0xFF7Fu
#define SYSTEM_RSVD_START 0xFF80u

#define SERIAL_TX      0xFF00
#define SERIAL_RX      0xFF01
#define SERIAL_STATUS  0xFF02
#define SERIAL_CONTROL 0xFF03

#define SERIAL_ST_TX_READY   0x01
#define SERIAL_ST_RX_READY   0x02
#define SERIAL_ST_TX_BUSY    0x04
#define SERIAL_ST_RX_OVERRUN 0x08

#define SERIAL_CTL_ENABLE    0x01
#define SERIAL_CTL_TX_ENABLE 0x02
#define SERIAL_CTL_RX_ENABLE 0x04
#define SERIAL_CTL_RESET     0x08

#define FL_Z 0x01
#define FL_C 0x02

#define AOS_ADD 0x0
#define AOS_SUB 0x1
#define AOS_AND 0x2
#define AOS_OR  0x3
#define AOS_XOR 0x4
#define AOS_NOT 0x5
#define AOS_ROR 0x6
#define AOS_ROL 0x7
#define AOS_SHR 0x8
#define AOS_SHL 0x9
#define AOS_INC 0xA
#define AOS_DEC 0xB
#define AOS_CMP 0xC

typedef struct {
    uint8_t A;
    uint8_t B;
    uint8_t C;
    uint8_t D;

    /* 16-bit index / pointer registers.
       Register encoding according to PPA-1 table:
         0x4 = X-HI, 0x5 = X-LO
         0x6 = Y-HI, 0x7 = Y-LO */
    uint16_t X;
    uint16_t Y;
    uint16_t BR;  /* programmer-visible bank register: BR-HI=0x8, BR-LO=0x9 */

    uint8_t IMM;
    uint8_t DST;
    uint8_t STEP;
    uint8_t AOS;
    uint8_t AAR;
    uint8_t ABR;
    uint8_t RS;
    uint8_t IR;
    uint8_t SR;
    uint8_t SP;

    uint16_t AR;
    uint16_t PC;

    bool halted;
} CPU;

typedef struct {
    uint8_t rom[ROM_SIZE];
    uint8_t ram[RAM_SIZE];
    uint8_t stack[0x100];
    uint8_t reserved[0x80];
    FILE *rom_file;
} Memory;

typedef struct {
    bool enabled;
    int master_fd;
    int slave_fd;
    char slave_name[128];
    uint8_t rx;
    uint8_t status;
    uint8_t control;
} Serial;

static inline void set_flag(CPU *cpu, uint8_t flag, bool on) {
    if (on) cpu->SR |= flag;
    else cpu->SR &= (uint8_t)~flag;
}

static inline void set_Z(CPU *cpu, uint8_t v) {
    set_flag(cpu, FL_Z, v == 0);
}

static int load_rom(Memory *memory, const char *path, uint16_t base) {
    memset(memory->rom, 0xFF, sizeof(memory->rom));
    memset(memory->ram, 0x00, sizeof(memory->ram));
    memset(memory->stack, 0x00, sizeof(memory->stack));
    memset(memory->reserved, 0xFF, sizeof(memory->reserved));

    FILE *f = fopen(path, "r+b");
    if (!f) {
        perror("fopen");
        return 0;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        perror("fseek");
        fclose(f);
        return 0;
    }
    long file_size = ftell(f);
    if (file_size < 0) {
        perror("ftell");
        fclose(f);
        return 0;
    }
    if ((unsigned long)file_size > ROM_SIZE) {
        fprintf(stderr, "ROM image is larger than 4 MiB (%ld bytes)\n", file_size);
        fclose(f);
        return 0;
    }
    rewind(f);

    size_t offset = (size_t)base;
    size_t max_read = ROM_SIZE - offset;
    size_t n = fread(memory->rom + offset, 1, max_read, f);
    if (ferror(f)) {
        perror("fread");
        fclose(f);
        return 0;
    }

    memory->rom_file = f;
    return (int)n;
}

static void persist_rom_byte(Memory *memory, uint32_t physical, uint8_t value) {
    if (!memory->rom_file || physical >= ROM_SIZE) return;
    if (fseek(memory->rom_file, (long)physical, SEEK_SET) != 0) return;
    if (fputc(value, memory->rom_file) == EOF) return;
    fflush(memory->rom_file);
}

static void serial_poll_rx(Serial *serial) {
    if (!serial || !serial->enabled)
        return;

    if ((serial->control &
         (SERIAL_CTL_ENABLE | SERIAL_CTL_RX_ENABLE)) !=
        (SERIAL_CTL_ENABLE | SERIAL_CTL_RX_ENABLE))
        return;

    if (serial->status & SERIAL_ST_RX_READY)
        return;

    uint8_t ch;

    ssize_t n = read(serial->master_fd, &ch, 1);

    if (n == 1) {
        serial->rx = ch;
        serial->status |= SERIAL_ST_RX_READY;
    }
}

static void serial_reset(Serial *serial) {
    serial->rx = 0;
    serial->status = 0;
    if ((serial->control & (SERIAL_CTL_ENABLE | SERIAL_CTL_TX_ENABLE)) ==
        (SERIAL_CTL_ENABLE | SERIAL_CTL_TX_ENABLE)) {
        serial->status |= SERIAL_ST_TX_READY;
    }
}

static int serial_open_pty(Serial *serial) {
    memset(serial, 0, sizeof(*serial));
    serial->master_fd = -1;
    serial->slave_fd = -1;

    int master = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (master < 0) {
        perror("posix_openpt");
        return 0;
    }

    if (grantpt(master) != 0 || unlockpt(master) != 0) {
        perror("grantpt/unlockpt");
        close(master);
        return 0;
    }

    char *name = ptsname(master);
    if (!name) {
        perror("ptsname");
        close(master);
        return 0;
    }

    int slave = open(name, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (slave < 0) {
        perror("open PTY slave");
        close(master);
        return 0;
    }

    struct termios tio;
    if (tcgetattr(slave, &tio) == 0) {
        cfmakeraw(&tio);
        tcsetattr(slave, TCSANOW, &tio);
    }

    /* Keep only the PTY master open in the emulator.
       This lets us detect when an external program (screen, picocom, ...)
       actually opens the slave side. */
    close(slave);

    serial->enabled = true;
    serial->master_fd = master;
    serial->slave_fd = -1;
    snprintf(serial->slave_name, sizeof(serial->slave_name), "%s", name);
    serial->control = SERIAL_CTL_ENABLE | SERIAL_CTL_TX_ENABLE | SERIAL_CTL_RX_ENABLE;
    serial_reset(serial);
    return 1;
}

static int serial_wait_for_client(Serial *serial) {
    if (!serial || !serial->enabled || serial->master_fd < 0) return 0;

    printf("Waiting for terminal connection...\n");
    fflush(stdout);

    for (;;) {
        uint8_t ch;
        ssize_t n = read(serial->master_fd, &ch, 1);

        if (n == 1) {
            /* A client is connected and already sent a byte. Preserve it. */
            serial->rx = ch;
            serial->status |= SERIAL_ST_RX_READY;
            printf("Terminal connected.\n");
            fflush(stdout);
            return 1;
        }

        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                /* Slave is open, but there is currently no input data. */
                printf("Terminal connected.\n");
                fflush(stdout);
                return 1;
            }

            if (errno == EIO) {
                /* No process has the slave PTY open yet. */
                usleep(50000);
                continue;
            }

            if (errno == EINTR) continue;
            perror("PTY wait");
            return 0;
        }

        /* A zero-length read is not useful here; wait and try again. */
        usleep(50000);
    }
}

static void serial_wait_for_disconnect(Serial *serial) {
    if (!serial || !serial->enabled || serial->master_fd < 0) return;

    printf("CPU stopped. Close the terminal connection to exit.\n");
    fflush(stdout);

    for (;;) {
        uint8_t buf[64];
        ssize_t n = read(serial->master_fd, buf, sizeof(buf));

        if (n > 0) continue;

        if (n < 0) {
            if (errno == EIO) return;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(50000);
                continue;
            }
            if (errno == EINTR) continue;
            return;
        }

        usleep(50000);
    }
}

static void serial_close(Serial *serial) {
    if (!serial) return;
    if (serial->slave_fd >= 0) close(serial->slave_fd);
    if (serial->master_fd >= 0) close(serial->master_fd);
    serial->slave_fd = -1;
    serial->master_fd = -1;
    serial->enabled = false;
}

static uint8_t system_read8(Memory *memory, Serial *serial, uint16_t addr) {
    if (addr >= STACK_PAGE && addr <= 0xFEFFu) {
        return memory->stack[addr & 0x00FFu];
    }

    if (addr >= SYSTEM_IO_START && addr <= SYSTEM_IO_END) {
        if (serial && serial->enabled) serial_poll_rx(serial);
        switch (addr) {
            case SERIAL_TX: return 0;
            case SERIAL_RX: {
                if (!serial || !serial->enabled) return 0;
                uint8_t v = serial->rx;
                serial->status &= (uint8_t)~SERIAL_ST_RX_READY;
                return v;
            }
            case SERIAL_STATUS: return (serial && serial->enabled) ? serial->status : 0;
            case SERIAL_CONTROL: return (serial && serial->enabled) ? serial->control : 0;
            default: return 0;
        }
    }

    if (addr >= SYSTEM_RSVD_START) {
        return memory->reserved[addr - SYSTEM_RSVD_START];
    }

    /* Unused portion of the SYSTEM bank. */
    return 0xFF;
}

static void system_write8(Memory *memory, Serial *serial, uint16_t addr, uint8_t v) {
    if (addr >= STACK_PAGE && addr <= 0xFEFFu) {
        memory->stack[addr & 0x00FFu] = v;
        return;
    }

    if (addr >= SYSTEM_IO_START && addr <= SYSTEM_IO_END) {
        if (!serial || !serial->enabled) return;
        switch (addr) {
            case SERIAL_TX:
                if ((serial->control & (SERIAL_CTL_ENABLE | SERIAL_CTL_TX_ENABLE)) ==
                    (SERIAL_CTL_ENABLE | SERIAL_CTL_TX_ENABLE)) {
                    serial->status &= (uint8_t)~SERIAL_ST_TX_READY;
                    serial->status |= SERIAL_ST_TX_BUSY;
                    ssize_t n = write(serial->master_fd, &v, 1);
                    (void)n;
                    serial->status &= (uint8_t)~SERIAL_ST_TX_BUSY;
                    serial->status |= SERIAL_ST_TX_READY;
                }
                return;
            case SERIAL_RX:
                return;
            case SERIAL_STATUS:
                serial->status &= (uint8_t)~(v & SERIAL_ST_RX_OVERRUN);
                return;
            case SERIAL_CONTROL:
                serial->control = v;
                if (v & SERIAL_CTL_RESET) {
                    serial->control &= (uint8_t)~SERIAL_CTL_RESET;
                    serial_reset(serial);
                } else if ((serial->control & (SERIAL_CTL_ENABLE | SERIAL_CTL_TX_ENABLE)) ==
                           (SERIAL_CTL_ENABLE | SERIAL_CTL_TX_ENABLE)) {
                    serial->status |= SERIAL_ST_TX_READY;
                } else {
                    serial->status &= (uint8_t)~SERIAL_ST_TX_READY;
                }
                return;
            default:
                return;
        }
    }

    /* RESERVED and the unused SYSTEM area ignore writes. */
}

/*
 * BR selects the data-memory bank:
 *   0000-003F = 64 EEPROM/ROM banks (4 MiB total)
 *   0040-004F = 16 RAM banks       (1 MiB total)
 *   0050      = SYSTEM bank (stack, serial, reserved)
 *
 * Instruction fetch deliberately comes from EEPROM bank 0000.  This keeps
 * execution stable while BR is changed to access RAM, I/O, or another ROM
 * data bank.  BR is therefore a data-bank register in this implementation.
 */
static uint8_t bus_read8(CPU *cpu, Memory *memory, Serial *serial, uint16_t addr) {
    uint16_t bank = cpu->BR;
    if (bank <= ROM_BANK_LAST) {
        uint32_t physical = (uint32_t)bank * BANK_SIZE + addr;
        return memory->rom[physical];
    }
    if (bank >= RAM_BANK_FIRST && bank <= RAM_BANK_LAST) {
        uint32_t physical = (uint32_t)(bank - RAM_BANK_FIRST) * BANK_SIZE + addr;
        return memory->ram[physical];
    }
    if (bank == SYSTEM_BANK) {
        return system_read8(memory, serial, addr);
    }
    return 0xFF;
}

static void bus_write8(CPU *cpu, Memory *memory, Serial *serial, uint16_t addr, uint8_t v) {
    uint16_t bank = cpu->BR;
    if (bank <= ROM_BANK_LAST) {
        uint32_t physical = (uint32_t)bank * BANK_SIZE + addr;
        memory->rom[physical] = v;      /* EEPROM is writable */
        persist_rom_byte(memory, physical, v);
        return;
    }
    if (bank >= RAM_BANK_FIRST && bank <= RAM_BANK_LAST) {
        uint32_t physical = (uint32_t)(bank - RAM_BANK_FIRST) * BANK_SIZE + addr;
        memory->ram[physical] = v;
        return;
    }
    if (bank == SYSTEM_BANK) {
        system_write8(memory, serial, addr, v);
    }
}

static inline uint8_t fetch_program8(CPU *cpu, Memory *memory, Serial *serial) {
    (void)serial;
    uint8_t v = memory->rom[cpu->PC];  /* program fetch = ROM bank 0000 */
    cpu->PC++;
    return v;
}

static inline uint16_t stack_addr(const CPU *cpu) {
    return (uint16_t)(STACK_PAGE | cpu->SP);
}

/* Stack is controller-owned: PUSH/POP/CALL/RET always access SYSTEM stack,
   independently of programmer-visible BR. */
static inline void push8(CPU *cpu, Memory *memory, Serial *serial, uint8_t v) {
    (void)serial;
    cpu->AR = stack_addr(cpu);
    memory->stack[cpu->SP] = v;
    cpu->SP--;
}

static inline uint8_t pop8(CPU *cpu, Memory *memory, Serial *serial) {
    (void)serial;
    cpu->SP++;
    cpu->AR = stack_addr(cpu);
    return memory->stack[cpu->SP];
}

static void dump_regs(const CPU *c) {
    printf(
        "PC=%04X AR=%04X SP=%02X IR=%02X SR=%02X RS=%02X DST=%02X STEP=%02X "
        "AOS=%02X IMM=%02X AAR=%02X ABR=%02X A=%02X B=%02X C=%02X D=%02X "
        "X=%04X Y=%04X BR=%04X HALT=%d\n",
        c->PC,
        c->AR,
        c->SP,
        c->IR,
        c->SR,
        c->RS,
        c->DST,
        c->STEP,
        c->AOS,
        c->IMM,
        c->AAR,
        c->ABR,
        c->A,
        c->B,
        c->C,
        c->D,
        c->X,
        c->Y,
        c->BR,
        c->halted ? 1 : 0
    );
}

static void dump_mem(CPU *cpu, Memory *memory, Serial *serial, uint16_t start, uint16_t len) {
    for (uint16_t i = 0; i < len; i++) {
        if ((i % 16) == 0) printf("%04X: ", (uint16_t)(start + i));
        printf("%02X ", bus_read8(cpu, memory, serial, (uint16_t)(start + i)));
        if ((i % 16) == 15) printf("\n");
    }
    if ((len % 16) != 0) printf("\n");
}

static inline bool valid_reg(uint8_t r) {
    return r <= 0x9;
}

/*
 * 8-bit operational register encoding:
 *   0x0 A, 0x1 B, 0x2 C, 0x3 D
 *   0x4 X-HI, 0x5 X-LO
 *   0x6 Y-HI, 0x7 Y-LO
 *   0x8 BR-HI, 0x9 BR-LO
 *
 * X/Y are stored as uint16_t, therefore their byte halves are accessed
 * explicitly instead of taking host-endian byte pointers.
 */
static inline uint8_t selected_reg_read(CPU *cpu) {
    switch (cpu->RS) {
        case 0x0: return cpu->A;
        case 0x1: return cpu->B;
        case 0x2: return cpu->C;
        case 0x3: return cpu->D;
        case 0x4: return (uint8_t)(cpu->X >> 8);
        case 0x5: return (uint8_t)(cpu->X & 0xFF);
        case 0x6: return (uint8_t)(cpu->Y >> 8);
        case 0x7: return (uint8_t)(cpu->Y & 0xFF);
        case 0x8: return (uint8_t)(cpu->BR >> 8);
        case 0x9: return (uint8_t)(cpu->BR & 0xFF);
        default:  return 0;
    }
}

static inline void selected_reg_write(CPU *cpu, uint8_t value) {
    switch (cpu->RS) {
        case 0x0: cpu->A = value; break;
        case 0x1: cpu->B = value; break;
        case 0x2: cpu->C = value; break;
        case 0x3: cpu->D = value; break;
        case 0x4: cpu->X = (uint16_t)(((uint16_t)value << 8) | (cpu->X & 0x00FF)); break;
        case 0x5: cpu->X = (uint16_t)((cpu->X & 0xFF00) | value); break;
        case 0x6: cpu->Y = (uint16_t)(((uint16_t)value << 8) | (cpu->Y & 0x00FF)); break;
        case 0x7: cpu->Y = (uint16_t)((cpu->Y & 0xFF00) | value); break;
        case 0x8: cpu->BR = (uint16_t)(((uint16_t)value << 8) | (cpu->BR & 0x00FF)); break;
        case 0x9: cpu->BR = (uint16_t)((cpu->BR & 0xFF00) | value); break;
        default: break;
    }
}

/* 16-bit pointer selector stored in instruction operand byte.
   We reuse the code of the corresponding high byte from the register table. */
static inline bool valid_ptr_reg(uint8_t r) {
    return r == 0x4 || r == 0x6;
}

static inline uint16_t selected_ptr_read(const CPU *cpu, uint8_t r) {
    switch (r) {
        case 0x4: return cpu->X;
        case 0x6: return cpu->Y;
        default:  return 0;
    }
}

static inline void selected_ptr_write(CPU *cpu, uint8_t r, uint16_t value) {
    switch (r) {
        case 0x4: cpu->X = value; break;
        case 0x6: cpu->Y = value; break;
        default: break;
    }
}

static bool controller_select_alu(CPU *cpu) {
    switch (cpu->IR) {
        case 0x0A:
        case 0x0B:
            cpu->AOS = AOS_ADD;
            return true;

        case 0x0C:
        case 0x0D:
            cpu->AOS = AOS_SUB;
            return true;

        case 0x0E:
        case 0x0F:
            cpu->AOS = AOS_AND;
            return true;

        case 0x10:
        case 0x11:
            cpu->AOS = AOS_OR;
            return true;

        case 0x12:
        case 0x13:
            cpu->AOS = AOS_XOR;
            return true;

        case 0x14:
            cpu->AOS = AOS_NOT;
            return true;

        case 0x15:
            cpu->AOS = AOS_ROR;
            return true;

        case 0x16:
            cpu->AOS = AOS_ROL;
            return true;

        case 0x17:
            cpu->AOS = AOS_SHR;
            return true;

        case 0x18:
            cpu->AOS = AOS_SHL;
            return true;

        case 0x19:
            cpu->AOS = AOS_INC;
            return true;

        case 0x1A:
            cpu->AOS = AOS_DEC;
            return true;

        case 0x1B:
        case 0x1C:
        case 0x1D:
        case 0x1E:
        case 0x1F:
        case 0x20:
            cpu->AOS = AOS_CMP;
            return true;

        default:
            return false;
    }
}

static uint8_t alu_execute(CPU *cpu) {
    uint8_t out = cpu->AAR;

    switch (cpu->AOS) {
        case AOS_ADD: {
            uint16_t res = (uint16_t)cpu->AAR + (uint16_t)cpu->ABR;
            out = (uint8_t)res;
            set_flag(cpu, FL_C, res > 0xFF);
            set_Z(cpu, out);
            break;
        }

        case AOS_SUB:
            out = (uint8_t)(cpu->AAR - cpu->ABR);
            set_flag(cpu, FL_C, cpu->AAR < cpu->ABR);
            set_Z(cpu, out);
            break;

        case AOS_AND:
            out = (uint8_t)(cpu->AAR & cpu->ABR);
            set_flag(cpu, FL_C, false);
            set_Z(cpu, out);
            break;

        case AOS_OR:
            out = (uint8_t)(cpu->AAR | cpu->ABR);
            set_flag(cpu, FL_C, false);
            set_Z(cpu, out);
            break;

        case AOS_XOR:
            out = (uint8_t)(cpu->AAR ^ cpu->ABR);
            set_flag(cpu, FL_C, false);
            set_Z(cpu, out);
            break;

        case AOS_NOT:
            out = (uint8_t)~cpu->AAR;
            set_flag(cpu, FL_C, false);
            set_Z(cpu, out);
            break;

        case AOS_ROR:
            out = (uint8_t)((cpu->AAR >> 1) | (cpu->AAR << 7));
            set_flag(cpu, FL_C, (cpu->AAR & 0x01) != 0);
            set_Z(cpu, out);
            break;

        case AOS_ROL:
            out = (uint8_t)((cpu->AAR << 1) | (cpu->AAR >> 7));
            set_flag(cpu, FL_C, (cpu->AAR & 0x80) != 0);
            set_Z(cpu, out);
            break;

        case AOS_SHR:
            out = (uint8_t)(cpu->AAR >> 1);
            set_flag(cpu, FL_C, (cpu->AAR & 0x01) != 0);
            set_Z(cpu, out);
            break;

        case AOS_SHL:
            out = (uint8_t)(cpu->AAR << 1);
            set_flag(cpu, FL_C, (cpu->AAR & 0x80) != 0);
            set_Z(cpu, out);
            break;

        case AOS_INC:
            out = (uint8_t)(cpu->AAR + 1);
            set_flag(cpu, FL_C, cpu->AAR == 0xFF);
            set_Z(cpu, out);
            break;

        case AOS_DEC:
            out = (uint8_t)(cpu->AAR - 1);
            set_flag(cpu, FL_C, cpu->AAR == 0x00);
            set_Z(cpu, out);
            break;

        case AOS_CMP:
            out = (uint8_t)(cpu->AAR - cpu->ABR);
            set_flag(cpu, FL_C, cpu->AAR < cpu->ABR);
            set_Z(cpu, out);
            break;

        default:
            break;
    }

    return out;
}

static inline void finish_instruction(CPU *cpu) {
    cpu->STEP = 0;
}

static bool cpu_microstep(CPU *cpu, Memory *memory, Serial *serial) {
    if (cpu->halted) return true;

    if (cpu->STEP == 0) {
        cpu->IR = fetch_program8(cpu, memory, serial);
        controller_select_alu(cpu);
        cpu->STEP = 1;
        return false;
    }

    switch (cpu->IR) {
        case 0x00: /* NOP */
            finish_instruction(cpu);
            return true;

        case 0x01: /* HLT */
            cpu->halted = true;
            finish_instruction(cpu);
            return true;

        case 0x02: /* MOV DST,SRC */
            if (cpu->STEP == 1) {
                uint8_t rp = fetch_program8(cpu, memory, serial);
                cpu->DST = (uint8_t)((rp >> 4) & 0x0F);
                cpu->RS  = (uint8_t)(rp & 0x0F);
                if (!valid_reg(cpu->DST) || !valid_reg(cpu->RS)) {
                    cpu->halted = true;
                    return true;
                }
                cpu->STEP = 2;
                return false;
            }
            if (cpu->STEP == 2) {
                cpu->AAR = selected_reg_read(cpu);
                cpu->RS = cpu->DST;
                selected_reg_write(cpu, cpu->AAR);
                set_Z(cpu, cpu->AAR);
                finish_instruction(cpu);
                return true;
            }
            break;

        case 0x03: /* MOV DST,#NUM */
            if (cpu->STEP == 1) {
                cpu->DST = fetch_program8(cpu, memory, serial);
                if (!valid_reg(cpu->DST)) {
                    cpu->halted = true;
                    return true;
                }
                cpu->STEP = 2;
                return false;
            }
            if (cpu->STEP == 2) {
                cpu->IMM = fetch_program8(cpu, memory, serial);
                cpu->STEP = 3;
                return false;
            }
            if (cpu->STEP == 3) {
                cpu->RS = cpu->DST;
                selected_reg_write(cpu, cpu->IMM);
                set_Z(cpu, cpu->IMM);
                finish_instruction(cpu);
                return true;
            }
            break;

        case 0x04: /* LD R1,$ADDR */
            if (cpu->STEP == 1) {
                cpu->DST = fetch_program8(cpu, memory, serial);
                if (!valid_reg(cpu->DST)) {
                    cpu->halted = true;
                    return true;
                }
                cpu->STEP = 2;
                return false;
            }
            if (cpu->STEP == 2) {
                cpu->AR = (uint16_t)fetch_program8(cpu, memory, serial) << 8;
                cpu->STEP = 3;
                return false;
            }
            if (cpu->STEP == 3) {
                cpu->AR |= fetch_program8(cpu, memory, serial);
                cpu->STEP = 4;
                return false;
            }
            if (cpu->STEP == 4) {
                cpu->AAR = bus_read8(cpu, memory, serial, cpu->AR);
                cpu->RS = cpu->DST;
                selected_reg_write(cpu, cpu->AAR);
                set_Z(cpu, cpu->AAR);
                finish_instruction(cpu);
                return true;
            }
            break;

        case 0x05: /* LD R1,R2 -- indirect, R2 is X or Y */
            if (cpu->STEP == 1) {
                uint8_t rp = fetch_program8(cpu, memory, serial);
                cpu->DST = (uint8_t)((rp >> 4) & 0x0F);
                cpu->RS  = (uint8_t)(rp & 0x0F);
                if (!valid_reg(cpu->DST) || !valid_ptr_reg(cpu->RS)) {
                    cpu->halted = true;
                    return true;
                }
                cpu->STEP = 2;
                return false;
            }
            if (cpu->STEP == 2) {
                uint8_t ptr = cpu->RS;
                cpu->AR = selected_ptr_read(cpu, ptr);
                cpu->AAR = bus_read8(cpu, memory, serial, cpu->AR);
                cpu->RS = cpu->DST;
                selected_reg_write(cpu, cpu->AAR);
                set_Z(cpu, cpu->AAR);
                finish_instruction(cpu);
                return true;
            }
            break;

        case 0x06: /* ST $ADDR,R1 */
            if (cpu->STEP == 1) {
                cpu->AR = (uint16_t)fetch_program8(cpu, memory, serial) << 8;
                cpu->STEP = 2;
                return false;
            }
            if (cpu->STEP == 2) {
                cpu->AR |= fetch_program8(cpu, memory, serial);
                cpu->STEP = 3;
                return false;
            }
            if (cpu->STEP == 3) {
                cpu->RS = fetch_program8(cpu, memory, serial);
                if (!valid_reg(cpu->RS)) {
                    cpu->halted = true;
                    return true;
                }
                cpu->STEP = 4;
                return false;
            }
            if (cpu->STEP == 4) {
                bus_write8(cpu, memory, serial, cpu->AR, selected_reg_read(cpu));
                finish_instruction(cpu);
                return true;
            }
            break;

        case 0x07: /* ST R1,R2 -- indirect, R1 is X/Y, R2 is source */
            if (cpu->STEP == 1) {
                uint8_t rp = fetch_program8(cpu, memory, serial);
                cpu->DST = (uint8_t)((rp >> 4) & 0x0F); /* pointer */
                cpu->RS  = (uint8_t)(rp & 0x0F);        /* source */
                if (!valid_ptr_reg(cpu->DST) || !valid_reg(cpu->RS)) {
                    cpu->halted = true;
                    return true;
                }
                cpu->STEP = 2;
                return false;
            }
            if (cpu->STEP == 2) {
                uint8_t src = cpu->RS;
                cpu->AR = selected_ptr_read(cpu, cpu->DST);
                cpu->RS = src;
                bus_write8(cpu, memory, serial, cpu->AR, selected_reg_read(cpu));
                finish_instruction(cpu);
                return true;
            }
            break;

        case 0x08: /* PUSH R1 */
            if (cpu->STEP == 1) {
                cpu->RS = fetch_program8(cpu, memory, serial);
                if (!valid_reg(cpu->RS)) {
                    cpu->halted = true;
                    return true;
                }
                cpu->STEP = 2;
                return false;
            }
            if (cpu->STEP == 2) {
                push8(cpu, memory, serial, selected_reg_read(cpu));
                finish_instruction(cpu);
                return true;
            }
            break;

        case 0x09: /* POP R1 */
            if (cpu->STEP == 1) {
                cpu->DST = fetch_program8(cpu, memory, serial);
                if (!valid_reg(cpu->DST)) {
                    cpu->halted = true;
                    return true;
                }
                cpu->STEP = 2;
                return false;
            }
            if (cpu->STEP == 2) {
                cpu->AAR = pop8(cpu, memory, serial);
                cpu->RS = cpu->DST;
                selected_reg_write(cpu, cpu->AAR);
                set_Z(cpu, cpu->AAR);
                finish_instruction(cpu);
                return true;
            }
            break;

        case 0x0A: /* ADD R,R */
        case 0x0C: /* SUB R,R */
        case 0x0E: /* AND R,R */
        case 0x10: /* OR R,R */
        case 0x12: /* XOR R,R */
            if (cpu->STEP == 1) {
                uint8_t rp = fetch_program8(cpu, memory, serial);
                cpu->DST = (uint8_t)((rp >> 4) & 0x0F);
                cpu->RS  = (uint8_t)(rp & 0x0F);
                if (!valid_reg(cpu->DST) || !valid_reg(cpu->RS)) {
                    cpu->halted = true;
                    return true;
                }
                cpu->STEP = 2;
                return false;
            }
            if (cpu->STEP == 2) {
                cpu->ABR = selected_reg_read(cpu);
                cpu->RS = cpu->DST;
                cpu->STEP = 3;
                return false;
            }
            if (cpu->STEP == 3) {
                cpu->AAR = selected_reg_read(cpu);
                cpu->STEP = 4;
                return false;
            }
            if (cpu->STEP == 4) {
                selected_reg_write(cpu, alu_execute(cpu));
                finish_instruction(cpu);
                return true;
            }
            break;

        case 0x0B: /* ADD R,#N */
        case 0x0D: /* SUB R,#N */
        case 0x0F: /* AND R,#N */
        case 0x11: /* OR R,#N */
        case 0x13: /* XOR R,#N */
            if (cpu->STEP == 1) {
                cpu->DST = fetch_program8(cpu, memory, serial);
                if (!valid_reg(cpu->DST)) {
                    cpu->halted = true;
                    return true;
                }
                cpu->STEP = 2;
                return false;
            }
            if (cpu->STEP == 2) {
                cpu->IMM = fetch_program8(cpu, memory, serial);
                cpu->STEP = 3;
                return false;
            }
            if (cpu->STEP == 3) {
                cpu->RS = cpu->DST;
                cpu->AAR = selected_reg_read(cpu);
                cpu->ABR = cpu->IMM;
                cpu->STEP = 4;
                return false;
            }
            if (cpu->STEP == 4) {
                selected_reg_write(cpu, alu_execute(cpu));
                finish_instruction(cpu);
                return true;
            }
            break;

        case 0x14: /* NOT */
        case 0x15: /* ROR */
        case 0x16: /* ROL */
        case 0x17: /* SHR */
        case 0x18: /* SHL */
        case 0x19: /* INC */
        case 0x1A: /* DEC */
            if (cpu->STEP == 1) {
                cpu->RS = fetch_program8(cpu, memory, serial);
                if (!valid_reg(cpu->RS)) {
                    cpu->halted = true;
                    return true;
                }
                cpu->STEP = 2;
                return false;
            }
            if (cpu->STEP == 2) {
                cpu->AAR = selected_reg_read(cpu);
                cpu->ABR = 0;
                cpu->STEP = 3;
                return false;
            }
            if (cpu->STEP == 3) {
                selected_reg_write(cpu, alu_execute(cpu));
                finish_instruction(cpu);
                return true;
            }
            break;

        case 0x1B: /* CMP R1 -- compare A with R1 */
            if (cpu->STEP == 1) {
                cpu->RS = fetch_program8(cpu, memory, serial);
                if (!valid_reg(cpu->RS)) {
                    cpu->halted = true;
                    return true;
                }
                cpu->STEP = 2;
                return false;
            }
            if (cpu->STEP == 2) {
                cpu->AAR = cpu->A;
                cpu->ABR = selected_reg_read(cpu);
                cpu->STEP = 3;
                return false;
            }
            if (cpu->STEP == 3) {
                (void)alu_execute(cpu);
                finish_instruction(cpu);
                return true;
            }
            break;

        case 0x1C: /* CMP R1,R2 */
            if (cpu->STEP == 1) {
                uint8_t rp = fetch_program8(cpu, memory, serial);
                cpu->DST = (uint8_t)((rp >> 4) & 0x0F);
                cpu->RS  = (uint8_t)(rp & 0x0F);
                if (!valid_reg(cpu->DST) || !valid_reg(cpu->RS)) {
                    cpu->halted = true;
                    return true;
                }
                cpu->STEP = 2;
                return false;
            }
            if (cpu->STEP == 2) {
                cpu->ABR = selected_reg_read(cpu);
                cpu->RS = cpu->DST;
                cpu->STEP = 3;
                return false;
            }
            if (cpu->STEP == 3) {
                cpu->AAR = selected_reg_read(cpu);
                cpu->STEP = 4;
                return false;
            }
            if (cpu->STEP == 4) {
                (void)alu_execute(cpu);
                finish_instruction(cpu);
                return true;
            }
            break;

        case 0x1D: /* CMP #NUM -- compare A with immediate */
            if (cpu->STEP == 1) {
                cpu->IMM = fetch_program8(cpu, memory, serial);
                cpu->STEP = 2;
                return false;
            }
            if (cpu->STEP == 2) {
                cpu->AAR = cpu->A;
                cpu->ABR = cpu->IMM;
                cpu->STEP = 3;
                return false;
            }
            if (cpu->STEP == 3) {
                (void)alu_execute(cpu);
                finish_instruction(cpu);
                return true;
            }
            break;

        case 0x1E: /* CMP R1,#NUM */
            if (cpu->STEP == 1) {
                cpu->DST = fetch_program8(cpu, memory, serial);
                if (!valid_reg(cpu->DST)) {
                    cpu->halted = true;
                    return true;
                }
                cpu->STEP = 2;
                return false;
            }
            if (cpu->STEP == 2) {
                cpu->IMM = fetch_program8(cpu, memory, serial);
                cpu->STEP = 3;
                return false;
            }
            if (cpu->STEP == 3) {
                cpu->RS = cpu->DST;
                cpu->AAR = selected_reg_read(cpu);
                cpu->ABR = cpu->IMM;
                cpu->STEP = 4;
                return false;
            }
            if (cpu->STEP == 4) {
                (void)alu_execute(cpu);
                finish_instruction(cpu);
                return true;
            }
            break;

        case 0x1F: /* CMP $ADDR -- compare A with memory */
            if (cpu->STEP == 1) {
                cpu->AR = (uint16_t)fetch_program8(cpu, memory, serial) << 8;
                cpu->STEP = 2;
                return false;
            }
            if (cpu->STEP == 2) {
                cpu->AR |= fetch_program8(cpu, memory, serial);
                cpu->STEP = 3;
                return false;
            }
            if (cpu->STEP == 3) {
                cpu->AAR = cpu->A;
                cpu->ABR = bus_read8(cpu, memory, serial, cpu->AR);
                cpu->STEP = 4;
                return false;
            }
            if (cpu->STEP == 4) {
                (void)alu_execute(cpu);
                finish_instruction(cpu);
                return true;
            }
            break;

        case 0x20: /* CMP R1,$ADDR */
            if (cpu->STEP == 1) {
                cpu->DST = fetch_program8(cpu, memory, serial);
                if (!valid_reg(cpu->DST)) {
                    cpu->halted = true;
                    return true;
                }
                cpu->STEP = 2;
                return false;
            }
            if (cpu->STEP == 2) {
                cpu->AR = (uint16_t)fetch_program8(cpu, memory, serial) << 8;
                cpu->STEP = 3;
                return false;
            }
            if (cpu->STEP == 3) {
                cpu->AR |= fetch_program8(cpu, memory, serial);
                cpu->STEP = 4;
                return false;
            }
            if (cpu->STEP == 4) {
                cpu->RS = cpu->DST;
                cpu->AAR = selected_reg_read(cpu);
                cpu->ABR = bus_read8(cpu, memory, serial, cpu->AR);
                cpu->STEP = 5;
                return false;
            }
            if (cpu->STEP == 5) {
                (void)alu_execute(cpu);
                finish_instruction(cpu);
                return true;
            }
            break;

        case 0x21: /* JMP */
        case 0x22: /* JC */
        case 0x23: /* JNC */
        case 0x24: /* JZ */
        case 0x25: /* JNZ */
            if (cpu->STEP == 1) {
                cpu->AR = (uint16_t)fetch_program8(cpu, memory, serial) << 8;
                cpu->STEP = 2;
                return false;
            }
            if (cpu->STEP == 2) {
                cpu->AR |= fetch_program8(cpu, memory, serial);
                cpu->STEP = 3;
                return false;
            }
            if (cpu->STEP == 3) {
                bool jump = false;
                switch (cpu->IR) {
                    case 0x21: jump = true; break;
                    case 0x22: jump = (cpu->SR & FL_C) != 0; break;
                    case 0x23: jump = (cpu->SR & FL_C) == 0; break;
                    case 0x24: jump = (cpu->SR & FL_Z) != 0; break;
                    case 0x25: jump = (cpu->SR & FL_Z) == 0; break;
                    default: break;
                }
                if (jump) cpu->PC = cpu->AR;
                finish_instruction(cpu);
                return true;
            }
            break;

        case 0x26: /* CALL $ADDR */
            if (cpu->STEP == 1) {
                cpu->AAR = fetch_program8(cpu, memory, serial);
                cpu->STEP = 2;
                return false;
            }
            if (cpu->STEP == 2) {
                cpu->ABR = fetch_program8(cpu, memory, serial);
                cpu->STEP = 3;
                return false;
            }
            if (cpu->STEP == 3) {
                push8(cpu, memory, serial, (uint8_t)(cpu->PC >> 8));
                cpu->STEP = 4;
                return false;
            }
            if (cpu->STEP == 4) {
                push8(cpu, memory, serial, (uint8_t)(cpu->PC & 0xFF));
                cpu->STEP = 5;
                return false;
            }
            if (cpu->STEP == 5) {
                cpu->PC = ((uint16_t)cpu->AAR << 8) | cpu->ABR;
                finish_instruction(cpu);
                return true;
            }
            break;

        case 0x27: /* RET */
            if (cpu->STEP == 1) {
                cpu->ABR = pop8(cpu, memory, serial);
                cpu->STEP = 2;
                return false;
            }
            if (cpu->STEP == 2) {
                cpu->AAR = pop8(cpu, memory, serial);
                cpu->STEP = 3;
                return false;
            }
            if (cpu->STEP == 3) {
                cpu->PC = ((uint16_t)cpu->AAR << 8) | cpu->ABR;
                finish_instruction(cpu);
                return true;
            }
            break;

        default:
            printf("Unknown opcode %02X at %04X\n", cpu->IR, (uint16_t)(cpu->PC - 1));
            cpu->halted = true;
            return true;
    }

    printf("Invalid microstep STEP=%02X for opcode %02X\n", cpu->STEP, cpu->IR);
    cpu->halted = true;
    return true;
}

static int cpu_execute_instruction(CPU *cpu, Memory *memory, Serial *serial) {
    if (cpu->halted) return 0;

    bool complete = false;

    while (!complete && !cpu->halted) {
        complete = cpu_microstep(cpu, memory, serial);
    }

    return 1;
}

static void usage(const char *prog) {
    printf("Usage:\n");
    printf("  %s rom.bin [--base HEX] [--pc HEX] [--steps N] [--dump HEX LEN] [--tty]\n", prog);
    printf("\n");
    printf("Without --steps, execution continues until HLT or Ctrl+C.\n");
    printf("--steps limits the number of complete CPU instructions, not microsteps.\n");
    printf("--tty creates a pseudo-terminal for SYSTEM bank 0050, SERIAL FF00-FF03.\n");
}

static uint32_t parse_hex(const char *s) {
    return (uint32_t)strtoul(s, NULL, 16);
}

int main(int argc, char **argv) {
    printf("PPA-1 Emu\n");
    printf("(c) 2026 Adam Cír (Adava), Adava Software, Adava Development\n");

    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    const char *rom_path = argv[1];

    CPU cpu = {0};
    Memory memory = {0};
    Serial serial = {0};
    serial.master_fd = -1;
    serial.slave_fd = -1;

    uint16_t base = 0x0000;
    cpu.PC = 0x0000;
    cpu.SP = 0xFF;
    cpu.BR = 0x0000;
    memory.rom_file = NULL;

    long long steps = -1;
    bool do_dump = false;
    bool tty_enabled = false;
    uint16_t dump_start = 0;
    uint16_t dump_len = 0;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--base") == 0 && i + 1 < argc) {
            base = (uint16_t)parse_hex(argv[++i]);
        } else if (strcmp(argv[i], "--pc") == 0 && i + 1 < argc) {
            cpu.PC = (uint16_t)parse_hex(argv[++i]);
        } else if (strcmp(argv[i], "--steps") == 0 && i + 1 < argc) {
            steps = strtoll(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--tty") == 0) {
            tty_enabled = true;
        } else if (strcmp(argv[i], "--dump") == 0 && i + 2 < argc) {
            do_dump = true;
            dump_start = (uint16_t)parse_hex(argv[++i]);
            dump_len = (uint16_t)parse_hex(argv[++i]);
        } else {
            usage(argv[0]);
            return 1;
        }
    }

    if (tty_enabled) {
        if (!serial_open_pty(&serial)) {
            return 1;
        }

        printf("TTY: %s\n", serial.slave_name);
        printf("Connect with: screen %s 9600\n", serial.slave_name);
        fflush(stdout);

        if (!serial_wait_for_client(&serial)) {
            serial_close(&serial);
            return 1;
        }
    }

    int loaded = load_rom(&memory, rom_path, base);
    if (!loaded) {
        printf("Failed to load ROM.\n");
        return 1;
    }

    printf("Loaded %d EEPROM bytes (ROM bank 0000 program fetch, BR=%04X)\n", loaded, cpu.BR);

    long long instruction_count = 0;

    while (!cpu.halted && (steps < 0 || instruction_count < steps)) {
        dump_regs(&cpu);
        cpu_execute_instruction(&cpu, &memory, &serial);
        instruction_count++;
    }

    dump_regs(&cpu);

    if (do_dump) {
        printf("\nMemory dump:\n");
        dump_mem(&cpu, &memory, &serial, dump_start, dump_len);
    }

    if (tty_enabled) {
        serial_wait_for_disconnect(&serial);
    }

    serial_close(&serial);
    if (memory.rom_file) fclose(memory.rom_file);
    return 0;
}
