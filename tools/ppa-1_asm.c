/****************************************************************/
/* PPA-1 Assembler                                              */
/* (c) 2026 Adam Cír (Adava), Adava Software, Adava Development */
/****************************************************************/

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>

#define MAX_SOURCE_LINES 8192
#define MAX_LINE_LEN     1024
#define MAX_SYMBOLS      1024
#define MAX_NAME_LEN     64
#define BANK_SIZE        0x10000u
#define ROM_BANK_COUNT   0x40u
#define ROM_IMAGE_SIZE   (BANK_SIZE * ROM_BANK_COUNT)

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct {
    char name[MAX_NAME_LEN];
    uint16_t value;
    bool is_const;
} Symbol;

typedef struct {
    char *text;
    char *file;
    int number;
} SourceLine;

typedef struct {
    SourceLine lines[MAX_SOURCE_LINES];
    size_t line_count;

    Symbol symbols[MAX_SYMBOLS];
    size_t symbol_count;

    uint8_t output[ROM_IMAGE_SIZE];
    uint32_t pc;          /* 16-bit address inside current ROM bank */
    uint16_t bank;        /* 0000-003F */
    uint32_t highest;
    bool wrote_anything;

    /* Scope used by local labels such as .loop */
    char current_global[MAX_NAME_LEN];
} Assembler;

typedef enum {
    OT_NONE,
    OT_REG,       /* 8-bit visible register: A-D, X/Y halves, BR halves */
    OT_REG16,     /* 16-bit pointer register: X or Y */
    OT_INDIRECT,  /* [X] or [Y] */
    OT_IMM,
    OT_ADDR,
    OT_VALUE
} OperandType;

typedef struct {
    OperandType type;
    char text[MAX_LINE_LEN];
} Operand;

static const SourceLine *g_current_source = NULL;

static int first_nonspace_column(const char *s) {
    int col = 1;
    while (*s && isspace((unsigned char)*s)) {
        s++;
        col++;
    }
    return col;
}

static int token_column(const char *line, const char *token) {
    if (!line || !token || !*token) return first_nonspace_column(line ? line : "");

    const char *p = strstr(line, token);
    if (!p) return first_nonspace_column(line);
    return (int)(p - line) + 1;
}

static void print_source_error(const char *kind, int line, int column, const char *msg) {
    const char *file = (g_current_source && g_current_source->file)
        ? g_current_source->file
        : "<input>";

    if (g_current_source && g_current_source->number == line) {
        fprintf(stderr, "%s:%d:%d: %s: %s\n",
                file, line, column, kind, msg);

        const char *src = g_current_source->text ? g_current_source->text : "";
        size_t len = strlen(src);
        while (len > 0 && (src[len - 1] == '\n' || src[len - 1] == '\r')) len--;

        fprintf(stderr, " %5d | %.*s\n", line, (int)len, src);
        fprintf(stderr, "       | ");
        for (int i = 1; i < column; i++) {
            char c = (i - 1 < (int)len) ? src[i - 1] : ' ';
            fputc(c == '\t' ? '\t' : ' ', stderr);
        }
        fprintf(stderr, "^\n");
    } else {
        fprintf(stderr, "%s:%d:%d: %s: %s\n",
                file, line, column, kind, msg);
    }
}

static void fatal_line(int line, const char *msg) {
    int col = g_current_source ? first_nonspace_column(g_current_source->text) : 1;
    print_source_error("error", line, col, msg);
    exit(1);
}

static void fatal_linef(int line, const char *fmt, const char *arg) {
    char msg[MAX_LINE_LEN * 2];
    snprintf(msg, sizeof(msg), fmt, arg);

    int col = 1;
    if (g_current_source) col = token_column(g_current_source->text, arg);

    print_source_error("error", line, col, msg);
    exit(1);
}

static void fatal_at_token(int line, const char *token, const char *msg) {
    int col = g_current_source ? token_column(g_current_source->text, token) : 1;
    print_source_error("error", line, col, msg);
    exit(1);
}

static char *ltrim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    return s;
}

static void rtrim(char *s) {
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) {
        s[--n] = '\0';
    }
}

static char *trim(char *s) {
    s = ltrim(s);
    rtrim(s);
    return s;
}

static void strtoupper(char *s) {
    for (; *s; s++) *s = (char)toupper((unsigned char)*s);
}

static int stricmp_ascii(const char *a, const char *b) {
    while (*a && *b) {
        int ca = toupper((unsigned char)*a);
        int cb = toupper((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++;
        b++;
    }
    return toupper((unsigned char)*a) - toupper((unsigned char)*b);
}

static bool valid_symbol_name(const char *s) {
    if (!s[0]) return false;
    if (!(isalpha((unsigned char)s[0]) || s[0] == '_')) return false;

    for (size_t i = 1; s[i]; i++) {
        if (!(isalnum((unsigned char)s[i]) || s[i] == '_' || s[i] == '.')) return false;
    }
    return true;
}

static void strip_comment(char *s) {
    bool in_quote = false;

    for (size_t i = 0; s[i]; i++) {
        if (s[i] == '"' && (i == 0 || s[i - 1] != '\\')) {
            in_quote = !in_quote;
            continue;
        }

        if (!in_quote && s[i] == ';') {
            s[i] = '\0';
            return;
        }

        if (!in_quote && s[i] == '/' && s[i + 1] == '/') {
            s[i] = '\0';
            return;
        }
    }
}

static bool normalize_symbol_name(Assembler *a, const char *raw,
                                  char out[MAX_NAME_LEN], int line) {
    if (!raw || !*raw) return false;

    if (raw[0] == '.') {
        if (!a->current_global[0]) {
            fatal_linef(line, "local label '%s' used before any global label", raw);
        }

        int n = snprintf(out, MAX_NAME_LEN, "%s%s", a->current_global, raw);
        if (n < 0 || n >= MAX_NAME_LEN) {
            fatal_line(line, "qualified local label is too long");
        }
        return true;
    }

    snprintf(out, MAX_NAME_LEN, "%s", raw);
    return true;
}

static Symbol *find_symbol(Assembler *a, const char *name) {
    for (size_t i = 0; i < a->symbol_count; i++) {
        if (stricmp_ascii(a->symbols[i].name, name) == 0) {
            return &a->symbols[i];
        }
    }
    return NULL;
}

static void add_symbol(Assembler *a, const char *name, uint16_t value, bool is_const, int line) {
    if (!valid_symbol_name(name)) {
        fatal_linef(line, "invalid symbol name '%s'", name);
    }

    if (find_symbol(a, name)) {
        fatal_linef(line, "duplicate symbol '%s'", name);
    }

    if (a->symbol_count >= MAX_SYMBOLS) {
        fatal_line(line, "too many symbols");
    }

    Symbol *s = &a->symbols[a->symbol_count++];
    snprintf(s->name, sizeof(s->name), "%s", name);
    s->value = value;
    s->is_const = is_const;
}

static bool valid_digits_for_base(const char *s, int base) {
    if (!s || !*s) return false;

    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;

        if (base == 2) {
            if (c != '0' && c != '1') return false;
        } else if (base == 10) {
            if (c < '0' || c > '9') return false;
        } else if (base == 16) {
            /*
             * PPA-1 numeric syntax is intentionally case-sensitive:
             *   A-F = hexadecimal digits
             *   d   = decimal suffix
             *   h   = hexadecimal suffix
             *
             * Lowercase a-f are therefore not accepted as hexadecimal
             * digits. This avoids ambiguity such as 0D vs 0d.
             */
            if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) {
                return false;
            }
        } else {
            return false;
        }
    }

    return true;
}

static uint32_t parse_char_literal(const char *s, bool *ok) {
    *ok = false;

    size_t len = strlen(s);
    if (len < 3 || s[0] != '\'' || s[len - 1] != '\'') return 0;

    const char *p = s + 1;
    const char *end = s + len - 1;
    uint32_t value = 0;

    if (p >= end) return 0;

    if (*p == '\\') {
        p++;
        if (p >= end) return 0;

        switch (*p) {
            case 'n': value = '\n'; p++; break;
            case 'r': value = '\r'; p++; break;
            case 't': value = '\t'; p++; break;
            case '0': value = '\0'; p++; break;
            case 'b': value = '\b'; p++; break;
            case 'e': value = 0x1B; p++; break;
            case '\\': value = '\\'; p++; break;
            case '\'': value = '\''; p++; break;
            case '"': value = '"'; p++; break;

            case 'x': {
                p++;
                if (end - p != 2) return 0;

                int hi, lo;
                if (p[0] >= '0' && p[0] <= '9') hi = p[0] - '0';
                else if (p[0] >= 'A' && p[0] <= 'F') hi = p[0] - 'A' + 10;
                else return 0;

                if (p[1] >= '0' && p[1] <= '9') lo = p[1] - '0';
                else if (p[1] >= 'A' && p[1] <= 'F') lo = p[1] - 'A' + 10;
                else return 0;

                value = (uint32_t)((hi << 4) | lo);
                p += 2;
                break;
            }

            default:
                return 0;
        }
    } else {
        value = (unsigned char)*p++;
    }

    if (p != end) return 0;

    *ok = true;
    return value;
}

static uint32_t parse_number_literal(const char *text, bool *ok) {
    char buf[MAX_LINE_LEN];
    snprintf(buf, sizeof(buf), "%s", text);
    char *s = trim(buf);

    if (!*s) {
        *ok = false;
        return 0;
    }

    if (*s == '\'') {
        return parse_char_literal(s, ok);
    }

    int base = 16;
    size_t len = strlen(s);

    /*
     * Suffixes are case-sensitive on purpose.
     * Only lowercase 'd' means decimal and only lowercase 'h' means
     * hexadecimal. Uppercase A-F always remain hexadecimal digits.
     */
    if (len > 1 && s[len - 1] == 'd') {
        s[len - 1] = '\0';
        base = 10;
    } else if (len > 1 && s[len - 1] == 'h') {
        s[len - 1] = '\0';
        base = 16;
    } else if (strncmp(s, "0x", 2) == 0 || strncmp(s, "0X", 2) == 0) {
        s += 2;
        base = 16;
    } else if (strncmp(s, "0b", 2) == 0 || strncmp(s, "0B", 2) == 0) {
        s += 2;
        base = 2;
    } else if (s[0] == '%') {
        s++;
        base = 2;
    } else {
        /* Bare PPA-1 numbers are hexadecimal by default. */
        base = 16;
    }

    if (!valid_digits_for_base(s, base)) {
        *ok = false;
        return 0;
    }

    errno = 0;
    char *end = NULL;
    unsigned long v = strtoul(s, &end, base);

    if (errno != 0 || !end || *end != '\0') {
        *ok = false;
        return 0;
    }

    *ok = true;
    return (uint32_t)v;
}

static uint16_t eval_expr(Assembler *a, const char *expr, int line, bool allow_undefined, bool *defined) {
    char buf[MAX_LINE_LEN];
    snprintf(buf, sizeof(buf), "%s", expr);
    char *s = trim(buf);

    if (*s == '#') s++;
    if (*s == '$') s++;
    s = trim(s);

    char *op = NULL;
    for (char *p = s + 1; *p; p++) {
        if (*p == '+' || *p == '-') {
            op = p;
            break;
        }
    }

    char lhs[MAX_LINE_LEN];
    char rhs[MAX_LINE_LEN] = {0};
    char operation = 0;

    if (op) {
        operation = *op;
        *op = '\0';
        snprintf(lhs, sizeof(lhs), "%s", trim(s));
        snprintf(rhs, sizeof(rhs), "%s", trim(op + 1));
    } else {
        snprintf(lhs, sizeof(lhs), "%s", s);
    }

    uint32_t base_value = 0;
    bool num_ok = false;
    base_value = parse_number_literal(lhs, &num_ok);

    if (!num_ok) {
        char normalized[MAX_NAME_LEN];
        normalize_symbol_name(a, lhs, normalized, line);
        Symbol *sym = find_symbol(a, normalized);
        if (!sym) {
            if (allow_undefined) {
                *defined = false;
                return 0;
            }
            fatal_linef(line, "unknown symbol '%s'", lhs);
        }
        base_value = sym->value;
    }

    if (operation) {
        bool off_ok = false;
        uint32_t off = parse_number_literal(rhs, &off_ok);
        if (!off_ok) {
            char normalized[MAX_NAME_LEN];
            normalize_symbol_name(a, rhs, normalized, line);
            Symbol *sym = find_symbol(a, normalized);
            if (!sym) {
                if (allow_undefined) {
                    *defined = false;
                    return 0;
                }
                fatal_linef(line, "unknown symbol '%s'", rhs);
            }
            off = sym->value;
        }

        if (operation == '+') base_value += off;
        else base_value -= off;
    }

    if (base_value > 0xFFFFu) {
        fatal_line(line, "value does not fit in 16 bits");
    }

    *defined = true;
    return (uint16_t)base_value;
}

static int parse_reg(const char *text) {
    if (stricmp_ascii(text, "A") == 0) return 0x0;
    if (stricmp_ascii(text, "B") == 0) return 0x1;
    if (stricmp_ascii(text, "C") == 0) return 0x2;
    if (stricmp_ascii(text, "D") == 0) return 0x3;
    if (stricmp_ascii(text, "X-HI") == 0) return 0x4;
    if (stricmp_ascii(text, "X-LO") == 0) return 0x5;
    if (stricmp_ascii(text, "Y-HI") == 0) return 0x6;
    if (stricmp_ascii(text, "Y-LO") == 0) return 0x7;
    if (stricmp_ascii(text, "BR-HI") == 0) return 0x8;
    if (stricmp_ascii(text, "BR-LO") == 0) return 0x9;
    return -1;
}

/* 16-bit pointer selector: use the code of the high-byte register. */
static int parse_reg16(const char *text) {
    if (stricmp_ascii(text, "X") == 0) return 0x4;
    if (stricmp_ascii(text, "Y") == 0) return 0x6;
    return -1;
}

static int parse_indirect_reg(const char *text) {
    char buf[MAX_LINE_LEN];
    snprintf(buf, sizeof(buf), "%s", text);
    char *t = trim(buf);
    size_t n = strlen(t);

    if (n < 3 || t[0] != '[' || t[n - 1] != ']') return -1;
    t[n - 1] = '\0';
    t = trim(t + 1);
    return parse_reg16(t);
}

static Operand classify_operand(const char *text) {
    Operand o;
    memset(&o, 0, sizeof(o));

    snprintf(o.text, sizeof(o.text), "%s", text);
    char *t = trim(o.text);

    if (!*t) {
        o.type = OT_NONE;
    } else if (parse_reg(t) >= 0) {
        o.type = OT_REG;
    } else if (parse_reg16(t) >= 0) {
        o.type = OT_REG16;
    } else if (parse_indirect_reg(t) >= 0) {
        o.type = OT_INDIRECT;
    } else if (*t == '#') {
        o.type = OT_IMM;
    } else if (*t == '$') {
        o.type = OT_ADDR;
    } else {
        o.type = OT_VALUE;
    }

    return o;
}

static int split_operands(char *s, Operand out[8]) {
    int count = 0;
    bool in_quote = false;
    char *start = s;

    for (char *p = s;; p++) {
        if (*p == '"' && (p == s || p[-1] != '\\')) in_quote = !in_quote;

        if ((!in_quote && *p == ',') || *p == '\0') {
            char saved = *p;
            *p = '\0';

            char *part = trim(start);
            if (*part) {
                if (count >= 8) return -1;
                out[count++] = classify_operand(part);
            }

            if (saved == '\0') break;
            start = p + 1;
        }
    }

    return count;
}

static void emit8(Assembler *a, uint8_t v, int line) {
    if (a->bank >= ROM_BANK_COUNT) fatal_line(line, "ROM bank out of range (0000-003F)");
    if (a->pc >= BANK_SIZE) fatal_line(line, "output exceeds 64 KiB ROM bank");

    uint32_t physical = (uint32_t)a->bank * BANK_SIZE + a->pc;
    a->output[physical] = v;
    a->pc++;
    if (!a->wrote_anything || physical + 1 > a->highest) a->highest = physical + 1;
    a->wrote_anything = true;
}

static void emit16be(Assembler *a, uint16_t v, int line) {
    emit8(a, (uint8_t)(v >> 8), line);
    emit8(a, (uint8_t)(v & 0xFF), line);
}

static void advance_pc(Assembler *a, uint32_t count, int line) {
    if (a->pc + count > BANK_SIZE) {
        fatal_line(line, "program/data exceeds current 64 KiB ROM bank");
    }
    a->pc += count;
}

static uint8_t get_u8_expr(Assembler *a, const char *text, int line, bool allow_undefined) {
    bool defined = false;
    uint16_t v = eval_expr(a, text, line, allow_undefined, &defined);
    if (!defined && allow_undefined) return 0;
    if (v > 0xFF) fatal_line(line, "8-bit value out of range");
    return (uint8_t)v;
}

static uint16_t get_u16_expr(Assembler *a, const char *text, int line, bool allow_undefined) {
    bool defined = false;
    uint16_t v = eval_expr(a, text, line, allow_undefined, &defined);
    return v;
}

static int instruction_size(const char *mnemonic, Operand ops[], int n, int line) {
    if (stricmp_ascii(mnemonic, "NOP") == 0 || stricmp_ascii(mnemonic, "HLT") == 0) {
        if (n != 0) fatal_line(line, "instruction takes no operands");
        return 1;
    }

    if (stricmp_ascii(mnemonic, "MOV") == 0) {
        if (n != 2 || ops[0].type != OT_REG) fatal_line(line, "MOV requires destination register and source");
        if (ops[1].type == OT_REG) return 2;
        if (ops[1].type == OT_IMM || ops[1].type == OT_VALUE) return 3;
        fatal_line(line, "invalid MOV operands");
    }

    if (stricmp_ascii(mnemonic, "LDA") == 0 || stricmp_ascii(mnemonic, "STA") == 0) {
        if (n != 1) fatal_line(line, "LDA/STA requires one address");
        if (ops[0].type == OT_INDIRECT) return 2;
        if (ops[0].type == OT_ADDR || ops[0].type == OT_VALUE) return 3;
        fatal_line(line, "LDA/STA requires $address, label, [X], or [Y]");
    }

    if (stricmp_ascii(mnemonic, "LDX") == 0 || stricmp_ascii(mnemonic, "LDY") == 0) {
        if (n != 1 || (ops[0].type != OT_ADDR && ops[0].type != OT_VALUE &&
                       ops[0].type != OT_IMM)) {
            fatal_line(line, "LDX/LDY requires one 16-bit address/value");
        }
        return 3;
    }

    if ((stricmp_ascii(mnemonic, "INC") == 0 || stricmp_ascii(mnemonic, "DEC") == 0) &&
        n == 1 && ops[0].type == OT_REG16) {
        return 2;
    }

    if (stricmp_ascii(mnemonic, "PUSH") == 0 || stricmp_ascii(mnemonic, "POP") == 0 ||
        stricmp_ascii(mnemonic, "NOT") == 0 || stricmp_ascii(mnemonic, "ROR") == 0 ||
        stricmp_ascii(mnemonic, "ROL") == 0 || stricmp_ascii(mnemonic, "SHR") == 0 ||
        stricmp_ascii(mnemonic, "SHL") == 0 || stricmp_ascii(mnemonic, "INC") == 0 ||
        stricmp_ascii(mnemonic, "DEC") == 0) {
        if (n != 1 || ops[0].type != OT_REG) fatal_line(line, "instruction requires one 8-bit register");
        return 2;
    }

    if (stricmp_ascii(mnemonic, "ADD") == 0 || stricmp_ascii(mnemonic, "SUB") == 0 ||
        stricmp_ascii(mnemonic, "AND") == 0 || stricmp_ascii(mnemonic, "OR") == 0 ||
        stricmp_ascii(mnemonic, "XOR") == 0) {
        if (n != 2 || ops[0].type != OT_REG) fatal_line(line, "ALU instruction requires destination register and source");
        if (ops[1].type == OT_REG) return 2;
        if (ops[1].type == OT_IMM || ops[1].type == OT_VALUE) return 3;
        fatal_line(line, "invalid ALU operands");
    }

    if (stricmp_ascii(mnemonic, "CMP") == 0) {
        if (n == 1) {
            if (ops[0].type == OT_REG) return 2;
            if (ops[0].type == OT_IMM || ops[0].type == OT_VALUE) return 2;
            if (ops[0].type == OT_ADDR) return 3;
        }

        if (n == 2 && ops[0].type == OT_REG) {
            if (ops[1].type == OT_REG) return 2;
            if (ops[1].type == OT_IMM || ops[1].type == OT_VALUE) return 3;
            if (ops[1].type == OT_ADDR) return 4;
        }

        fatal_line(line, "invalid CMP operands");
    }

    if (stricmp_ascii(mnemonic, "JMP") == 0 || stricmp_ascii(mnemonic, "JC") == 0 ||
        stricmp_ascii(mnemonic, "JNC") == 0 || stricmp_ascii(mnemonic, "JZ") == 0 ||
        stricmp_ascii(mnemonic, "JNZ") == 0) {
        if (n != 1) fatal_line(line, "jump requires one address or label");
        return 3;
    }

    if (stricmp_ascii(mnemonic, "CALL") == 0) {
        if (n != 1) fatal_line(line, "CALL requires one address or label");
        return 3;
    }

    if (stricmp_ascii(mnemonic, "RET") == 0) {
        if (n != 0) fatal_line(line, "RET takes no operands");
        return 1;
    }

    fatal_linef(line, "unknown instruction '%s'", mnemonic);
    return 0;
}

static void encode_instruction(Assembler *a, const char *mnemonic, Operand ops[], int n, int line) {
    if (stricmp_ascii(mnemonic, "NOP") == 0) {
        emit8(a, 0x00, line);
        return;
    }

    if (stricmp_ascii(mnemonic, "HLT") == 0) {
        emit8(a, 0x01, line);
        return;
    }

    if (stricmp_ascii(mnemonic, "MOV") == 0) {
        int dst = parse_reg(ops[0].text);

        if (ops[1].type == OT_REG) {
            int src = parse_reg(ops[1].text);
            emit8(a, 0x02, line);
            emit8(a, (uint8_t)((dst << 4) | src), line);
        } else {
            emit8(a, 0x03, line);
            emit8(a, (uint8_t)dst, line);
            emit8(a, get_u8_expr(a, ops[1].text, line, false), line);
        }
        return;
    }

    if (stricmp_ascii(mnemonic, "LDA") == 0) {
        if (ops[0].type == OT_INDIRECT) {
            emit8(a, 0x26, line);
            emit8(a, (uint8_t)parse_indirect_reg(ops[0].text), line);
        } else {
            emit8(a, 0x04, line);
            emit16be(a, get_u16_expr(a, ops[0].text, line, false), line);
        }
        return;
    }

    if (stricmp_ascii(mnemonic, "STA") == 0) {
        if (ops[0].type == OT_INDIRECT) {
            emit8(a, 0x27, line);
            emit8(a, (uint8_t)parse_indirect_reg(ops[0].text), line);
        } else {
            emit8(a, 0x05, line);
            emit16be(a, get_u16_expr(a, ops[0].text, line, false), line);
        }
        return;
    }

    if (stricmp_ascii(mnemonic, "LDX") == 0 || stricmp_ascii(mnemonic, "LDY") == 0) {
        emit8(a, stricmp_ascii(mnemonic, "LDX") == 0 ? 0x2A : 0x2B, line);
        emit16be(a, get_u16_expr(a, ops[0].text, line, false), line);
        return;
    }

    if (stricmp_ascii(mnemonic, "PUSH") == 0 || stricmp_ascii(mnemonic, "POP") == 0) {
        emit8(a, stricmp_ascii(mnemonic, "PUSH") == 0 ? 0x06 : 0x07, line);
        emit8(a, (uint8_t)parse_reg(ops[0].text), line);
        return;
    }

    struct Pair {
        const char *name;
        uint8_t rr;
        uint8_t ri;
    };

    static const struct Pair pairs[] = {
        {"ADD", 0x08, 0x09},
        {"SUB", 0x0A, 0x0B},
        {"AND", 0x0C, 0x0D},
        {"OR",  0x0E, 0x0F},
        {"XOR", 0x10, 0x11}
    };

    for (size_t i = 0; i < sizeof(pairs) / sizeof(pairs[0]); i++) {
        if (stricmp_ascii(mnemonic, pairs[i].name) == 0) {
            int r1 = parse_reg(ops[0].text);

            if (ops[1].type == OT_REG) {
                int r2 = parse_reg(ops[1].text);
                emit8(a, pairs[i].rr, line);
                emit8(a, (uint8_t)((r1 << 4) | r2), line);
            } else {
                emit8(a, pairs[i].ri, line);
                emit8(a, (uint8_t)r1, line);
                emit8(a, get_u8_expr(a, ops[1].text, line, false), line);
            }
            return;
        }
    }

    if ((stricmp_ascii(mnemonic, "INC") == 0 || stricmp_ascii(mnemonic, "DEC") == 0) &&
        n == 1 && ops[0].type == OT_REG16) {
        emit8(a, stricmp_ascii(mnemonic, "INC") == 0 ? 0x28 : 0x29, line);
        emit8(a, (uint8_t)parse_reg16(ops[0].text), line);
        return;
    }

    struct Unary {
        const char *name;
        uint8_t opcode;
    };

    static const struct Unary unary[] = {
        {"NOT", 0x12},
        {"ROR", 0x13},
        {"ROL", 0x14},
        {"SHR", 0x15},
        {"SHL", 0x16},
        {"INC", 0x17},
        {"DEC", 0x18}
    };

    for (size_t i = 0; i < sizeof(unary) / sizeof(unary[0]); i++) {
        if (stricmp_ascii(mnemonic, unary[i].name) == 0) {
            emit8(a, unary[i].opcode, line);
            emit8(a, (uint8_t)parse_reg(ops[0].text), line);
            return;
        }
    }

    if (stricmp_ascii(mnemonic, "CMP") == 0) {
        if (n == 1) {
            if (ops[0].type == OT_REG) {
                emit8(a, 0x19, line);
                emit8(a, (uint8_t)parse_reg(ops[0].text), line);
            } else if (ops[0].type == OT_ADDR) {
                emit8(a, 0x1D, line);
                emit16be(a, get_u16_expr(a, ops[0].text, line, false), line);
            } else {
                emit8(a, 0x1B, line);
                emit8(a, get_u8_expr(a, ops[0].text, line, false), line);
            }
            return;
        }

        if (n == 2) {
            int r1 = parse_reg(ops[0].text);

            if (ops[1].type == OT_REG) {
                int r2 = parse_reg(ops[1].text);
                emit8(a, 0x1A, line);
                emit8(a, (uint8_t)((r1 << 4) | r2), line);
            } else if (ops[1].type == OT_ADDR) {
                emit8(a, 0x1E, line);
                emit8(a, (uint8_t)r1, line);
                emit16be(a, get_u16_expr(a, ops[1].text, line, false), line);
            } else {
                emit8(a, 0x1C, line);
                emit8(a, (uint8_t)r1, line);
                emit8(a, get_u8_expr(a, ops[1].text, line, false), line);
            }
            return;
        }
    }

    struct Jump {
        const char *name;
        uint8_t opcode;
    };

    static const struct Jump jumps[] = {
        {"JMP", 0x1F},
        {"JC",  0x20},
        {"JNC", 0x21},
        {"JZ",  0x22},
        {"JNZ", 0x23}
    };

    for (size_t i = 0; i < sizeof(jumps) / sizeof(jumps[0]); i++) {
        if (stricmp_ascii(mnemonic, jumps[i].name) == 0) {
            emit8(a, jumps[i].opcode, line);
            emit16be(a, get_u16_expr(a, ops[0].text, line, false), line);
            return;
        }
    }

    if (stricmp_ascii(mnemonic, "CALL") == 0) {
        emit8(a, 0x24, line);
        emit16be(a, get_u16_expr(a, ops[0].text, line, false), line);
        return;
    }

    if (stricmp_ascii(mnemonic, "RET") == 0) {
        emit8(a, 0x25, line);
        return;
    }

    fatal_linef(line, "cannot encode instruction '%s'", mnemonic);
}

static int parse_string_literal(const char *text, uint8_t *out, size_t out_cap, int line) {
    char buf[MAX_LINE_LEN];
    snprintf(buf, sizeof(buf), "%s", text);
    char *s = trim(buf);

    if (*s != '"') fatal_line(line, "string must start with a quote");
    s++;

    int count = 0;
    bool closed = false;

    while (*s) {
        if (*s == '"') {
            s++;
            closed = true;
            break;
        }

        unsigned char ch;

        if (*s == '\\') {
            s++;
            if (!*s) fatal_line(line, "unfinished escape sequence");

            switch (*s) {
                case 'n': ch = '\n'; break;
                case 'r': ch = '\r'; break;
                case 't': ch = '\t'; break;
                case '0': ch = '\0'; break;
                case '\\': ch = '\\'; break;
                case '"': ch = '"'; break;
                default: ch = (unsigned char)*s; break;
            }
        } else {
            ch = (unsigned char)*s;
        }

        if ((size_t)count >= out_cap) fatal_line(line, "string is too long");
        out[count++] = ch;
        s++;
    }

    if (!closed) fatal_line(line, "unterminated string literal");
    if (*trim(s)) fatal_line(line, "unexpected text after string literal");

    return count;
}

static void handle_directive(Assembler *a, char *line, int line_no, int pass) {
    char *p = line;
    char *space = p;

    while (*space && !isspace((unsigned char)*space)) space++;

    char directive[64];
    size_t dlen = (size_t)(space - p);
    if (dlen >= sizeof(directive)) fatal_line(line_no, "directive name is too long");

    memcpy(directive, p, dlen);
    directive[dlen] = '\0';
    strtoupper(directive);

    char *args = trim(space);

    if (strcmp(directive, ".BANK") == 0) {
        if (!*args) fatal_line(line_no, ".bank requires ROM bank 0000-003F");
        bool defined = false;
        uint16_t bank = eval_expr(a, args, line_no, pass == 1, &defined);
        if (pass == 1 && !defined) fatal_line(line_no, ".bank cannot use a forward label");
        if (bank >= ROM_BANK_COUNT) fatal_line(line_no, "ROM bank must be 0000-003F");
        a->bank = bank;
        return;
    }

    if (strcmp(directive, ".ORG") == 0) {
        if (!*args) fatal_line(line_no, ".org requires an address");

        bool defined = false;
        uint16_t addr = eval_expr(a, args, line_no, pass == 1, &defined);

        if (pass == 1 && !defined) {
            fatal_line(line_no, ".org cannot use a forward label");
        }

        a->pc = (uint32_t)addr;
        if (pass == 2 && a->wrote_anything && a->pc < a->highest) {
            /* Overwriting with .org is allowed, matching ROM-style assembly. */
        }
        return;
    }

    if (strcmp(directive, ".BYTE") == 0 || strcmp(directive, "DB") == 0) {
        Operand ops[8];
        int n = split_operands(args, ops);
        if (n <= 0) fatal_line(line_no, ".byte requires at least one value");

        if (pass == 1) {
            advance_pc(a, (uint16_t)n, line_no);
        } else {
            for (int i = 0; i < n; i++) {
                emit8(a, get_u8_expr(a, ops[i].text, line_no, false), line_no);
            }
        }
        return;
    }

    if (strcmp(directive, ".WORD") == 0 || strcmp(directive, "DW") == 0) {
        Operand ops[8];
        int n = split_operands(args, ops);
        if (n <= 0) fatal_line(line_no, ".word requires at least one value");

        if (pass == 1) {
            advance_pc(a, (uint16_t)(n * 2), line_no);
        } else {
            for (int i = 0; i < n; i++) {
                emit16be(a, get_u16_expr(a, ops[i].text, line_no, false), line_no);
            }
        }
        return;
    }

    if (strcmp(directive, ".ASCII") == 0 || strcmp(directive, ".ASCIZ") == 0) {
        uint8_t bytes[MAX_LINE_LEN];
        int n = parse_string_literal(args, bytes, sizeof(bytes), line_no);
        int total = n + (strcmp(directive, ".ASCIZ") == 0 ? 1 : 0);

        if (pass == 1) {
            advance_pc(a, (uint16_t)total, line_no);
        } else {
            for (int i = 0; i < n; i++) emit8(a, bytes[i], line_no);
            if (strcmp(directive, ".ASCIZ") == 0) emit8(a, 0, line_no);
        }
        return;
    }

    fatal_linef(line_no, "unknown directive '%s'", directive);
}

static bool parse_equ(Assembler *a, char *line, int line_no, int pass) {
    char tmp[MAX_LINE_LEN];
    snprintf(tmp, sizeof(tmp), "%s", line);

    char *name = strtok(tmp, " \t,");
    char *kw = strtok(NULL, " \t,");
    char *value = strtok(NULL, "");

    if (!name || !kw || !value) return false;

    if (stricmp_ascii(kw, "EQU") != 0) return false;

    if (pass == 1) {
        bool defined = false;
        uint16_t v = eval_expr(a, trim(value), line_no, false, &defined);
        add_symbol(a, name, v, true, line_no);
    }

    return true;
}

static char *consume_labels(Assembler *a, char *line, int line_no, int pass) {
    char *p = trim(line);

    while (*p) {
        char *colon = strchr(p, ':');
        if (!colon) break;

        char *ws = p;
        while (*ws && !isspace((unsigned char)*ws) && *ws != ':') ws++;

        if (ws != colon) break;

        *colon = '\0';
        char *name = trim(p);

        char normalized[MAX_NAME_LEN];

        if (name[0] == '.') {
            normalize_symbol_name(a, name, normalized, line_no);
        } else {
            if (!valid_symbol_name(name)) {
                fatal_linef(line_no, "invalid symbol name '%s'", name);
            }
            snprintf(normalized, sizeof(normalized), "%s", name);
            snprintf(a->current_global, sizeof(a->current_global), "%s", name);
        }

        if (pass == 1) {
            add_symbol(a, normalized, (uint16_t)a->pc, false, line_no);
        }

        p = trim(colon + 1);
    }

    return p;
}

static void process_line(Assembler *a, const SourceLine *sl, int pass) {
    g_current_source = sl;

    char buf[MAX_LINE_LEN];
    snprintf(buf, sizeof(buf), "%s", sl->text);

    strip_comment(buf);
    char *line = trim(buf);
    if (!*line) return;

    line = consume_labels(a, line, sl->number, pass);
    if (!*line) return;

    if (parse_equ(a, line, sl->number, pass)) return;

    if (*line == '.') {
        handle_directive(a, line, sl->number, pass);
        return;
    }

    char *p = line;
    while (*p && !isspace((unsigned char)*p)) p++;

    char mnemonic[64];
    size_t mlen = (size_t)(p - line);
    if (mlen >= sizeof(mnemonic)) fatal_line(sl->number, "mnemonic is too long");

    memcpy(mnemonic, line, mlen);
    mnemonic[mlen] = '\0';
    strtoupper(mnemonic);

    char *optext = trim(p);
    Operand ops[8];
    int n = 0;

    if (*optext) {
        n = split_operands(optext, ops);
        if (n < 0) fatal_at_token(sl->number, optext, "too many operands");
    }

    int size = instruction_size(mnemonic, ops, n, sl->number);

    if (pass == 1) {
        advance_pc(a, (uint16_t)size, sl->number);
    } else {
        encode_instruction(a, mnemonic, ops, n, sl->number);
    }
}

static char *dup_string(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (!p) {
        fprintf(stderr, "Out of memory\n");
        exit(1);
    }
    memcpy(p, s, n);
    return p;
}

static void dirname_of(const char *path, char out[PATH_MAX]) {
    snprintf(out, PATH_MAX, "%s", path);

    char *slash = strrchr(out, '/');
    if (!slash) {
        snprintf(out, PATH_MAX, ".");
        return;
    }

    if (slash == out) {
        slash[1] = '\0';
        return;
    }

    *slash = '\0';
}

static bool parse_include_directive(const char *line, char out_path[PATH_MAX]) {
    char tmp[MAX_LINE_LEN];
    snprintf(tmp, sizeof(tmp), "%s", line);

    strip_comment(tmp);
    char *s = trim(tmp);

    const char *kw = ".include";
    size_t kwlen = strlen(kw);

    if (strncasecmp(s, kw, kwlen) != 0) return false;
    if (s[kwlen] && !isspace((unsigned char)s[kwlen])) return false;

    s = trim(s + kwlen);
    if (*s != '"') return false;
    s++;

    char *endq = strchr(s, '"');
    if (!endq) return false;
    *endq = '\0';

    if (!*s) return false;
    snprintf(out_path, PATH_MAX, "%s", s);
    return true;
}

static void load_source_recursive(Assembler *a, const char *path, int depth) {
    if (depth > 32) {
        fprintf(stderr, "%s: error: include nesting too deep\n", path);
        exit(1);
    }

    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "%s: error: cannot open source/include file: %s\n",
                path, strerror(errno));
        exit(1);
    }

    char line[MAX_LINE_LEN];
    int line_no = 0;

    while (fgets(line, sizeof(line), f)) {
        line_no++;

        size_t len = strlen(line);
        if (len > 0 && line[len - 1] != '\n' && !feof(f)) {
            SourceLine temp = { line, (char *)path, line_no };
            g_current_source = &temp;
            fatal_line(line_no, "source line is too long");
        }

        char include_name[PATH_MAX];
        if (parse_include_directive(line, include_name)) {
            char dir[PATH_MAX];
            char resolved[PATH_MAX];

            dirname_of(path, dir);

            if (include_name[0] == '/') {
                snprintf(resolved, sizeof(resolved), "%s", include_name);
            } else {
                int n = snprintf(resolved, sizeof(resolved), "%s/%s", dir, include_name);
                if (n < 0 || n >= (int)sizeof(resolved)) {
                    SourceLine temp = { line, (char *)path, line_no };
                    g_current_source = &temp;
                    fatal_at_token(line_no, include_name, "include path is too long");
                }
            }

            FILE *probe = fopen(resolved, "r");
            if (!probe) {
                SourceLine temp = { line, (char *)path, line_no };
                g_current_source = &temp;

                char msg[MAX_LINE_LEN];
                snprintf(msg, sizeof(msg),
                         "cannot open include file '%.800s': %.160s",
                         include_name, strerror(errno));
                fatal_at_token(line_no, include_name, msg);
            }
            fclose(probe);

            load_source_recursive(a, resolved, depth + 1);
            continue;
        }

        if (a->line_count >= MAX_SOURCE_LINES) {
            fprintf(stderr, "%s:%d: error: too many source lines\n", path, line_no);
            fclose(f);
            exit(1);
        }

        SourceLine *sl = &a->lines[a->line_count++];
        sl->text = dup_string(line);
        sl->file = dup_string(path);
        sl->number = line_no;
    }

    fclose(f);
}

static void load_source(Assembler *a, const char *path) {
    load_source_recursive(a, path, 0);
}

static void free_source(Assembler *a) {
    for (size_t i = 0; i < a->line_count; i++) {
        free(a->lines[i].text);
        free(a->lines[i].file);
    }
}

static void pass1(Assembler *a) {
    a->pc = 0;
    a->bank = 0;
    a->current_global[0] = '\0';

    for (size_t i = 0; i < a->line_count; i++) {
        process_line(a, &a->lines[i], 1);
    }
}

static void pass2(Assembler *a) {
    memset(a->output, 0xFF, sizeof(a->output));
    a->pc = 0;
    a->bank = 0;
    a->highest = 0;
    a->wrote_anything = false;
    a->current_global[0] = '\0';

    for (size_t i = 0; i < a->line_count; i++) {
        process_line(a, &a->lines[i], 2);
    }
}

static void write_output(const Assembler *a, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror(path);
        exit(1);
    }

    size_t size = ROM_IMAGE_SIZE;
    if (fwrite(a->output, 1, size, f) != size) {
        perror("fwrite");
        fclose(f);
        exit(1);
    }

    fclose(f);
    printf("Wrote %zu bytes (64 x 64 KiB ROM banks) to %s\n", size, path);
}

static void print_symbols(const Assembler *a) {
    if (a->symbol_count == 0) return;

    printf("Symbols:\n");
    for (size_t i = 0; i < a->symbol_count; i++) {
        printf("  %-24s = %04X%s\n",
               a->symbols[i].name,
               a->symbols[i].value,
               a->symbols[i].is_const ? " (const)" : "");
    }
}

static void usage(const char *prog) {
    printf("PPA-1 Assembler\n");
    printf("Usage:\n");
    printf("  %s input.asm output.bin [--symbols]\n", prog);
    printf("\n");
    printf("Number syntax:\n");
    printf("  0A, FF, 1234       hexadecimal (default; A-F uppercase)\n");
    printf("  0x0A               hexadecimal\n");
    printf("  0b1010 or %%1010    binary\n");
    printf("  10d                 decimal (lowercase d only)\n");
    printf("  0Ah                 hexadecimal suffix (lowercase h only)\n");
    printf("  'A', '\\n', '\\x1B' character literals\n");
    printf("\n");
    printf("Operand syntax:\n");
    printf("  #0A                 immediate\n");
    printf("  #'A'                immediate character\n");
    printf("  $1234               address\n");
    printf("  label                label/address in jumps/CALL\n");
    printf("  .loop                local label under current global label\n");
    printf("\n");
    printf("Subroutines:\n");
    printf("  CALL label           opcode 24, pushes return address in CPU\n");
    printf("  RET                  opcode 25, returns from subroutine\n");
    printf("  X-HI/X-LO            8-bit halves of X (register codes 04/05)\n");
    printf("  Y-HI/Y-LO            8-bit halves of Y (register codes 06/07)\n");
    printf("  BR-HI/BR-LO          bank register halves (register codes 08/09)\n");
    printf("  SP/SR                controller-only; not valid ASM registers\n");
    printf("  LDX addr / LDY addr  load 16-bit pointer register (opcodes 2A/2B)\n");
    printf("  LDA [X] / LDA [Y]   indirect load through X/Y (opcode 26)\n");
    printf("  STA [X] / STA [Y]   indirect store through X/Y (opcode 27)\n");
    printf("  INC X/Y, DEC X/Y    16-bit pointer inc/dec (opcodes 28/29)\n");
    printf("\n");
    printf("Directives:\n");
    printf("  .include \"file.inc\"\n");
    printf("  .bank 0000          select ROM output bank 0000-003F\n");
    printf("  .org $8000\n");
    printf("  .byte 01, 02, FF\n");
    printf("  .word 1234, label\n");
    printf("  .ascii \"text\"\n");
    printf("  .asciz \"text\"\n");
    printf("  NAME EQU 0A\n");
    printf("\nOutput is always a complete 4 MiB EEPROM image; unused bytes are FF.\n");
}

int main(int argc, char **argv) {
    printf("PPA-1 Assembler\n");
    printf("(c) 2026 Adam Cír (Adava), Adava Software, Adava Development\n");

    if (argc < 3) {
        usage(argv[0]);
        return 1;
    }

    bool show_symbols = false;

    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--symbols") == 0) {
            show_symbols = true;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return 1;
        }
    }

    Assembler a;
    memset(&a, 0, sizeof(a));

    load_source(&a, argv[1]);
    pass1(&a);
    pass2(&a);
    write_output(&a, argv[2]);

    if (show_symbols) {
        print_symbols(&a);
    }

    free_source(&a);
    return 0;
}
