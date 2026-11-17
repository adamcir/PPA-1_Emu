/*
 * PPA-1_MIDI-to-ASM terminal track editor.
 * Native C11 + ncursesw frontend and native C11 MIDI conversion engine.
 * No Python runtime, external MIDI library or shell invocation required.
 * (c) 2026 Adava Development
 */
#define _POSIX_C_SOURCE 200809L
#include <ncurses.h>
#include "midi_core.h"
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#define TRACK_MAX PPA_MIDI_MAX_TRACKS
#define ROW_MAX 512
#define FILE_MAX 4096

typedef struct {
    char name[NAME_MAX + 8];
    bool directory;
} Entry;
static const char *symbols[] = {"|-|_|-|_", "/|/|/|/|", "/\\/\\/\\/\\", "~^~^~^~^", ".:*?^*.: "};
static const char *poly_id[] = {"low", "high", "latest"};
static PpaMidiTrack tracks[TRACK_MAX];
static int ntracks = 0;
static char midi_path[PATH_MAX] = "";
static char repository_root[PATH_MAX] = ".";

/* The native ncurses editor is fully C-based; Python is optional legacy CLI.
 * Resolve the repository root independently of the working directory. */
static void initialize_paths(void) {
#if defined(__linux__)
    char exe[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n > 0) {
        exe[n] = 0;
        char *slash = strrchr(exe, '/');
        if (slash) {
            *slash = 0;
            char candidate[PATH_MAX];
            snprintf(candidate, sizeof(candidate), "%s/../..", exe);
            if (!realpath(candidate, repository_root))
                snprintf(repository_root, sizeof(repository_root), ".");
        }
    }
#endif
}
static char output_path[PATH_MAX] = "examples/music.asm";
static char message[256] = "O=open MIDI  Space=remove/restore  W=wave  P=polyphony  +/-=volume  F=optimize";
static bool optimize = false;
static int clock_hz = 100000;
static int tick_ms = 10;

static int file_cmp(const void *va, const void *vb) {
    const Entry *a = va, *b = vb;
    if (a->directory != b->directory) return b->directory - a->directory;
    return strcasecmp(a->name, b->name);
}
static bool midi_extension(const char *name) {
    const char *dot = strrchr(name, '.');
    return dot && (!strcasecmp(dot, ".mid") || !strcasecmp(dot, ".midi"));
}
static void line_at(int row, int color, const char *label) {
    if (row < 0 || row >= LINES) return;
    attron(COLOR_PAIR(color));
    mvaddnstr(row, 0, label, COLS - 1);
    clrtoeol();
    attroff(COLOR_PAIR(color));
}
static void title(const char *what) {
    attron(A_BOLD | COLOR_PAIR(1));
    mvhline(0, 0, ' ', COLS);
    mvaddnstr(0, 2, what, COLS - 4);
    attroff(A_BOLD | COLOR_PAIR(1));
}
static void ask_string(const char *prompt, char *out, size_t cap) {
    echo(); curs_set(1);
    int row = LINES - 2;
    move(row, 0); clrtoeol();
    mvaddnstr(row, 0, prompt, COLS - 2);
    move(row, (int)strlen(prompt));
    getnstr(out, (int)(cap - 1));
    noecho(); curs_set(0);
}
static bool browse_file(char *selected, size_t size) {
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) return false;
    int top = 0, cursor = 0;
    for (;;) {
        Entry *entries = calloc(FILE_MAX, sizeof(*entries));
        if (!entries) return false;
        size_t count = 0;
        DIR *dir = opendir(cwd);
        if (!dir) { free(entries); return false; }
        while (count < FILE_MAX - 1) {
            struct dirent *de = readdir(dir);
            if (!de) break;
            if (!strcmp(de->d_name, ".")) continue;
            char path[PATH_MAX];
            snprintf(path, sizeof(path), "%s/%s", cwd, de->d_name);
            struct stat sb;
            if (stat(path, &sb) != 0) continue;
            if (!S_ISDIR(sb.st_mode) && (!S_ISREG(sb.st_mode) ||
                !midi_extension(de->d_name))) continue;
            snprintf(entries[count].name, sizeof(entries[count].name),
                     "%s", de->d_name);
            entries[count].directory = S_ISDIR(sb.st_mode);
            count++;
        }
        closedir(dir);
        qsort(entries, count, sizeof(*entries), file_cmp);
        if (cursor >= (int)count) cursor = count ? (int)count - 1 : 0;
        for (;;) {
            erase();
            title("PPA-1_MIDI-to-ASM / OPEN MIDI");
            mvaddnstr(2, 2, cwd, COLS - 3);
            mvaddstr(3, 2, "UP/DOWN: choose   ENTER: open   BACKSPACE: parent   ESC: cancel");
            int visible = LINES - 7;
            if (cursor < top) top = cursor;
            if (cursor >= top + visible) top = cursor - visible + 1;
            for (int i = 0; i < visible && top + i < (int)count; ++i) {
                int idx = top + i;
                if (idx == cursor) attron(A_REVERSE);
                mvprintw(i + 5, 3, "%-3s %-*.*s",
                         entries[idx].directory ? "[D]" : "MID",
                         COLS - 10, COLS - 10, entries[idx].name);
                if (idx == cursor) attroff(A_REVERSE);
            }
            refresh();
            int key = getch();
            if (key == 27) { free(entries); return false; }
            if (key == KEY_DOWN && cursor + 1 < (int)count) cursor++;
            if (key == KEY_UP && cursor > 0) cursor--;
            if (key == KEY_BACKSPACE || key == 127 || key == 8) {
                char *slash = strrchr(cwd, '/');
                if (slash && slash != cwd) *slash = '\0';
                else snprintf(cwd, sizeof(cwd), "/");
                cursor = top = 0;
                break;
            }
            if ((key == '\n' || key == KEY_ENTER) && count) {
                char path[PATH_MAX];
                snprintf(path, sizeof(path), "%s/%s", cwd, entries[cursor].name);
                if (entries[cursor].directory) {
                    snprintf(cwd, sizeof(cwd), "%s", path);
                    cursor = top = 0;
                    break;
                }
                snprintf(selected, size, "%s", path);
                free(entries);
                return true;
            }
        }
        free(entries);
    }
}

/* Modal "Save ASM as" dialog. Arrow keys navigate directories, F2 edits
 * filename, S/Enter writes path, Esc cancels. No file is created here. */
static bool export_dialog(char *destination, size_t capacity) {
    char dir[PATH_MAX];
    char filename[256] = "";
    snprintf(dir, sizeof(dir), "%s", destination);
    char *slash = strrchr(dir, '/');
    if (slash) {
        snprintf(filename, sizeof(filename), "%s", slash + 1);
        if (slash == dir) dir[1] = '\0';
        else *slash = '\0';
    } else {
        snprintf(filename, sizeof(filename), "%s", destination);
        if (!getcwd(dir, sizeof(dir))) return false;
    }
    if (!*filename) snprintf(filename, sizeof(filename), "music.asm");
    int cursor = 0, top = 0;
    for (;;) {
        Entry *entries = calloc(FILE_MAX, sizeof(*entries));
        if (!entries) return false;
        size_t count = 0;
        DIR *dp = opendir(dir);
        if (!dp) {
            snprintf(message, sizeof(message), "Cannot browse export directory");
            free(entries); return false;
        }
        struct dirent *de;
        while ((de = readdir(dp)) && count < FILE_MAX - 1) {
            if (!strcmp(de->d_name, ".")) continue;
            char full[PATH_MAX];
            int n = snprintf(full, sizeof(full), "%s/%s", dir, de->d_name);
            if (n < 0 || (size_t)n >= sizeof(full)) continue;
            struct stat st;
            if (stat(full, &st)) continue;
            if (!S_ISDIR(st.st_mode) &&
                (!S_ISREG(st.st_mode) || !strrchr(de->d_name, '.') ||
                 strcasecmp(strrchr(de->d_name, '.'), ".asm")))
                continue;
            snprintf(entries[count].name, sizeof(entries[count].name),
                     "%s", de->d_name);
            entries[count].directory = S_ISDIR(st.st_mode);
            count++;
        }
        closedir(dp);
        qsort(entries, count, sizeof(*entries), file_cmp);
        if (cursor >= (int)count) cursor = count ? (int)count - 1 : 0;
        bool navigate = false;
        for (;;) {
            int height = LINES > 17 ? LINES - 4 : LINES;
            int width = COLS > 78 ? COLS - 8 : COLS;
            if (height < 9 || width < 32) {
                free(entries);
                snprintf(message, sizeof(message),
                         "Terminal too small for Save ASM dialog");
                return false;
            }
            int y = (LINES - height) / 2;
            int x = (COLS - width) / 2;
            WINDOW *win = newwin(height, width, y, x);
            if (!win) { free(entries); return false; }
            keypad(win, TRUE);
            box(win, 0, 0);
            wattron(win, A_BOLD);
            mvwaddnstr(win, 0, 3, " SAVE ASM AS ", width - 6);
            wattroff(win, A_BOLD);
            mvwprintw(win, 2, 2, "Directory: %.*s",
                      width - 15, dir);
            mvwprintw(win, 3, 2, "Filename:  %.*s",
                      width - 15, filename);
            mvwaddnstr(win, 4, 2,
                "UP/DOWN directory | ENTER open/save | F2 name | S save | ESC cancel",
                width - 4);
            int visible = height - 8;
            if (cursor < top) top = cursor;
            if (cursor >= top + visible) top = cursor - visible + 1;
            for (int i = 0; i < visible && top + i < (int)count; ++i) {
                int idx = top + i;
                if (idx == cursor) wattron(win, A_REVERSE);
                mvwprintw(win, 6 + i, 2, "%-3s %.*s",
                          entries[idx].directory ? "[D]" : "ASM",
                          width - 9, entries[idx].name);
                if (idx == cursor) wattroff(win, A_REVERSE);
            }
            mvwaddnstr(win, height - 2, 2,
                       "Backspace: parent | F2: edit filename | S: confirm",
                       width - 4);
            wrefresh(win);
            int key = wgetch(win);
            if (key == 27) {
                delwin(win); free(entries); touchwin(stdscr); refresh();
                return false;
            }
            if (key == KEY_UP && cursor > 0) cursor--;
            if (key == KEY_DOWN && cursor + 1 < (int)count) cursor++;
            if (key == KEY_BACKSPACE || key == 127 || key == 8) {
                char *parent = strrchr(dir, '/');
                if (parent && parent != dir) *parent = '\0';
                else snprintf(dir, sizeof(dir), "/");
                cursor = top = 0; navigate = true;
            }
            if (key == KEY_F(2)) {
                char typed[256] = "";
                echo(); curs_set(1);
                mvwprintw(win, 3, 2, "Filename:  %-*s", width - 16, "");
                wmove(win, 3, 13); wrefresh(win);
                wgetnstr(win, typed, (int)sizeof(typed) - 1);
                noecho(); curs_set(0);
                if (*typed && !strchr(typed, '/')) {
                    snprintf(filename, sizeof(filename), "%s", typed);
                }
            }
            if ((key == '\n' || key == KEY_ENTER) && count) {
                if (entries[cursor].directory) {
                    char next[PATH_MAX];
                    int n = snprintf(next, sizeof(next), "%s/%s",
                                     dir, entries[cursor].name);
                    if (n > 0 && (size_t)n < sizeof(next)) {
                        snprintf(dir, sizeof(dir), "%s", next);
                        cursor = top = 0; navigate = true;
                    }
                } else {
                    snprintf(filename, sizeof(filename), "%s",
                             entries[cursor].name);
                    key = 's';
                }
            } else if (key == '\n' || key == KEY_ENTER) {
                key = 's';
            }
            if (key == 's' || key == 'S') {
                size_t len = strlen(filename);
                if (len && !strchr(filename, '/') && strcmp(filename, ".") &&
                    strcmp(filename, "..")) {
                    if (len < 4 || strcasecmp(filename + len - 4, ".asm")) {
                        if (len + 4 < sizeof(filename)) strcat(filename, ".asm");
                        else { delwin(win); continue; }
                    }
                    char full[PATH_MAX];
                    int n = snprintf(full, sizeof(full), "%s/%s", dir, filename);
                    if (n > 0 && (size_t)n < capacity && (size_t)n < sizeof(full)) {
                        struct stat st;
                        if (!stat(full, &st) && !S_ISREG(st.st_mode)) {
                            snprintf(message, sizeof(message),
                                     "Export target is not a regular file");
                        } else {
                            /* Do not overwrite without explicit confirmation. */
                            bool exists = !stat(full, &st);
                            if (exists) {
                                mvwaddnstr(win, height - 2, 2,
                                    "FILE EXISTS. Overwrite? (y/N) ", width - 4);
                                wclrtoeol(win); wrefresh(win);
                                int answer = wgetch(win);
                                if (answer != 'y' && answer != 'Y') {
                                    delwin(win); continue;
                                }
                            }
                            snprintf(destination, capacity, "%s", full);
                            delwin(win); free(entries);
                            touchwin(stdscr); refresh();
                            return true;
                        }
                    } else {
                        snprintf(message, sizeof(message),
                                 "ASM path exceeds maximum length");
                    }
                }
            }
            delwin(win);
            if (navigate) break;
        }
        free(entries);
    }
}

static void load_tracks(void) {
    ntracks = 0;
    char error[256] = "";
    if (!ppa_midi_open(midi_path, tracks, TRACK_MAX, &ntracks,
                       error, sizeof(error))) {
        snprintf(message, sizeof(message), "MIDI parser: %.180s", error);
        return;
    }
    const char *base = strrchr(midi_path, '/');
    base = base ? base + 1 : midi_path;
    char stem[160];
    snprintf(stem, sizeof(stem), "%s", base);
    char *dot = strrchr(stem, '.');
    if (dot) *dot = 0;
    for (char *p = stem; *p; ++p)
        if (!isalnum((unsigned char)*p) && *p != '_' && *p != '-')
            *p = '_';
    snprintf(output_path, sizeof(output_path), "%s/examples/%s.asm",
             repository_root, stem);
    snprintf(message, sizeof(message), "Loaded %d MIDI track/channel streams",
             ntracks);
}
static void do_export(void) {
    if (!ntracks) {
        snprintf(message, sizeof(message), "Open a MIDI file first (O).");
        return;
    }
    if (!optimize) {
        int count = 0;
        for (int i = 0; i < ntracks; i++) if (tracks[i].enabled) count++;
        if (count > 4) {
            snprintf(message, sizeof(message),
                     "More than 4 active tracks: press F to optimize.");
            return;
        }
        int voice = 1;
        for (int i = 0; i < ntracks; i++)
            tracks[i].voice = tracks[i].enabled ? voice++ : 0;
    }
    char chosen[PATH_MAX];
    snprintf(chosen, sizeof(chosen), "%s", output_path);
    if (!export_dialog(chosen, sizeof(chosen))) return;
    snprintf(output_path, sizeof(output_path), "%s", chosen);
    int count = 0, banks = 0;
    char error[256] = "";
    if (ppa_midi_export(midi_path, tracks, ntracks, optimize, output_path,
                        clock_hz, tick_ms, &count, &banks, error, sizeof(error)))
        snprintf(message, sizeof(message), "Exported %d frames / %d ROM bank(s)",
                 count, banks);
    else
        snprintf(message, sizeof(message), "Export error: %.180s", error);
}
static void render_editor(int selected, int top) {
    erase();
    title("PPA-1_MIDI-to-ASM  |  C / ncurses  |  4-VOICE TRACK MIXER");
    mvprintw(2, 2, "MIDI: %.*s", COLS - 10, midi_path[0] ? midi_path : "[none] Press O to browse");
    mvprintw(3, 2, "ASM:  %.*s", COLS - 10, output_path);
    mvprintw(4, 2, "Optimizer: %s  |  selected tracks: ",
             optimize ? "ON" : "OFF");
    int count = 0;
    for (int i = 0; i < ntracks; ++i) if (tracks[i].enabled) ++count;
    printw("%d / %d  |  Clock: %d  |  Tick: %d ms",
           count, ntracks, clock_hz, tick_ms);
    if (COLS >= 40) {
        if (optimize) attron(A_BOLD | COLOR_PAIR(3));
        mvaddstr(5, 2, "[ OPTIMIZE 4 CHANNELS ]  (F or click)");
        if (optimize) attroff(A_BOLD | COLOR_PAIR(3));
    }
    line_at(6, 2, " #  Use  MIDI track / channel                  Notes    Waveform       Poly    Vol");
    int rows = LINES - 11;
    for (int y = 0; y < rows && y + top < ntracks; y++) {
        int i = top + y;
        PpaMidiTrack *t = &tracks[i];
        if (i == selected) attron(A_REVERSE);
        mvprintw(7 + y, 1,
                 "%3d  %-3s  %-35.35s  %6u   %-10s %-7s  %3d",
                 i + 1, t->enabled ? "YES" : "NO",
                 t->name, t->notes, symbols[t->wave],
                 poly_id[t->poly], t->volume);
        if (i == selected) attroff(A_REVERSE);
    }
    line_at(LINES - 4, 2, " UP/DOWN: select  SPACE: remove/restore  W: waveform  P: low/high/latest  +/-: volume");
    line_at(LINES - 3, 2, " O: open MIDI  F: optimize  A: output  C: clock  T: tick  ENTER: export  Q: quit");
    line_at(LINES - 1, 3, message);
    refresh();
}
int main(int argc, char **argv) {
    (void)argc;
    initialize_paths();
    initscr(); cbreak(); noecho(); keypad(stdscr, TRUE); curs_set(0);
    mousemask(ALL_MOUSE_EVENTS, NULL);
    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(1, COLOR_BLACK, COLOR_CYAN);
        init_pair(2, COLOR_CYAN, -1);
        init_pair(3, COLOR_GREEN, -1);
    }
    if (argv[1] && *argv[1]) {
        snprintf(midi_path, sizeof(midi_path), "%s", argv[1]);
        load_tracks();
    }
    int selected = 0, top = 0;
    for (;;) {
        if (selected >= ntracks) selected = ntracks ? ntracks - 1 : 0;
        int rows = LINES - 11;
        if (rows < 1) rows = 1;
        if (selected < top) top = selected;
        if (selected >= top + rows) top = selected - rows + 1;
        render_editor(selected, top);
        int key = getch();
        if (key == KEY_MOUSE) {
            MEVENT mouse;
            if (getmouse(&mouse) == OK &&
                (mouse.bstate & (BUTTON1_CLICKED | BUTTON1_PRESSED))) {
                if (mouse.y == 5 && mouse.x >= 2 && mouse.x < 27) {
                    optimize = !optimize;
                    snprintf(message, sizeof(message), "Four-channel optimizer: %s",
                             optimize ? "ON" : "OFF");
                } else if (mouse.y >= 7 && mouse.y < LINES - 4) {
                    int index = top + mouse.y - 7;
                    if (index >= 0 && index < ntracks) selected = index;
                }
            }
            continue;
        }
        if (key == 'q' || key == 'Q') break;
        if (key == 'o' || key == 'O') {
            char path[PATH_MAX];
            if (browse_file(path, sizeof(path))) {
                snprintf(midi_path, sizeof(midi_path), "%s", path);
                load_tracks(); selected = top = 0; optimize = false;
            }
            continue;
        }
        if (key == 'a' || key == 'A') {
            char path[PATH_MAX];
            snprintf(path, sizeof(path), "%s", output_path);
            if (export_dialog(path, sizeof(path)))
                snprintf(output_path, sizeof(output_path), "%s", path);
            continue;
        }
        if (key == 'c' || key == 'C' || key == 't' || key == 'T') {
            bool change_clock = key == 'c' || key == 'C';
            char input[32] = "";
            ask_string(change_clock ? "CPU microsteps/second: " : "Time unit (ms): ",
                       input, sizeof(input));
            if (*input) {
                char *end = NULL;
                long value = strtol(input, &end, 10);
                if (!*end && value >= (change_clock ? 100 : 1) &&
                    value <= (change_clock ? 10000000 : 250)) {
                    if (change_clock) clock_hz = (int)value;
                    else tick_ms = (int)value;
                } else {
                    snprintf(message, sizeof(message), "Invalid clock/tick value");
                }
            }
            continue;
        }
        if (key == 'f' || key == 'F') {
            optimize = !optimize;
            snprintf(message, sizeof(message), "4-voice fill-optimization: %s",
                     optimize ? "ON (dynamic waveform switching)" : "OFF");
            continue;
        }
        if (key == KEY_DOWN && selected + 1 < ntracks) selected++;
        if (key == KEY_UP && selected > 0) selected--;
        if (key == '\n' || key == KEY_ENTER) do_export();
        if (!ntracks) continue;
        PpaMidiTrack *t = &tracks[selected];
        if (key == ' ') t->enabled = !t->enabled;
        if (key == 'w' || key == 'W') t->wave = (t->wave + 1) % 5;
        if (key == 'p' || key == 'P') t->poly = (t->poly + 1) % 3;
        if (key == '+' || key == '=') {
            t->volume += 8; if (t->volume > 255) t->volume = 255;
        }
        if (key == '-') {
            t->volume -= 8; if (t->volume < 0) t->volume = 0;
        }
    }
    endwin();
    return 0;
}
