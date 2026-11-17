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
static const char *poly_id[] = {"LOW", "HIGH", "LATEST"};
static const char *wave_name[] = {"SQUARE", "SAWTOOTH", "TRIANGLE", "SINE", "NOISE"};
enum { UI_HEADER=1, UI_GREEN=2, UI_ORANGE=3, UI_SELECTED=4,
       UI_DIM=5, UI_PANEL=6, UI_BAR=7, UI_WARNING=8 };
static PpaMidiPreview preview;
static bool preview_valid = false;
static bool source_expanded = false;
static int visible_track[2048];
static int visible_y[2048];
static int visible_count = 0;
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

static void update_preview(void);

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
    update_preview();
}
static void update_preview(void) {
    preview_valid = false;
    if (!ntracks || !midi_path[0]) return;
    char error[256] = "";
    preview_valid = ppa_midi_preview(midi_path, tracks, ntracks,
                                     optimize, &preview, error, sizeof(error));
    if (!preview_valid)
        snprintf(message, sizeof(message), "Preview error: %.180s", error);
}

static void draw_meter(WINDOW *win, int y, int x, int width,
                       int volume, int scheme) {
    if (width < 2) return;
    int fill = volume * width / 255;
    wattron(win, COLOR_PAIR(scheme));
    for (int i = 0; i < width; ++i)
        mvwaddch(win, y, x + i, i < fill ? ACS_CKBOARD : ACS_HLINE);
    wattroff(win, COLOR_PAIR(scheme));
}

/* Compact graph of notes actually allocated to a track. Lane 0 is
 * its first physical voice, lane 1 its second, etc. Note clusters
 * are sampled from the very same allocator used for ASM generation. */
static void draw_track_timeline(int y, int x, int width,
                                int track, int lane) {
    if (!preview_valid || width <= 0) return;
    if (width > (int)preview.cells) width = (int)preview.cells;
    for (int c = 0; c < width; ++c) {
        int sample = c * (int)preview.cells / width;
        int rank = 0, found = 0;
        for (int voice = 0; voice < 4; ++voice) {
            if (preview.owner[voice][sample] != track + 1) continue;
            if (rank++ == lane) { found = 1; break; }
        }
        attron(COLOR_PAIR(found ? UI_ORANGE : UI_DIM));
        mvaddch(y, x + c, found ? ACS_CKBOARD : ACS_BULLET);
        attroff(COLOR_PAIR(found ? UI_ORANGE : UI_DIM));
    }
}

static void track_settings_dialog(int idx) {
    if (idx < 0 || idx >= ntracks) return;
    PpaMidiTrack edited = tracks[idx];
    int field = 0;
    for (;;) {
        int height = 19;
        int width = COLS < 80 ? COLS - 2 : 76;
        if (LINES < height + 2 || width < 52) return;
        WINDOW *w = newwin(height, width, (LINES-height)/2, (COLS-width)/2);
        if (!w) return;
        keypad(w, TRUE);
        wbkgd(w, COLOR_PAIR(UI_PANEL));
        box(w, 0, 0);
        wattron(w, COLOR_PAIR(UI_ORANGE) | A_BOLD);
        mvwaddstr(w, 0, 3, " EDIT TRACK / CHANNEL ");
        wattroff(w, COLOR_PAIR(UI_ORANGE) | A_BOLD);
        wattron(w, A_BOLD);
        mvwaddnstr(w, 2, 3, edited.name, width - 6);
        wattroff(w, A_BOLD);
        const char *labels[] = {"Enabled", "Waveform", "Volume", "Polyphony", "Transpose"};
        char values[5][100];
        snprintf(values[0], sizeof(values[0]), "%s", edited.enabled ? "YES" : "NO");
        snprintf(values[1], sizeof(values[1]), "%s  %s", wave_name[edited.wave],
                 symbols[edited.wave]);
        snprintf(values[2], sizeof(values[2]), "%3d / 255", edited.volume);
        snprintf(values[3], sizeof(values[3]), "%s", poly_id[edited.poly]);
        snprintf(values[4], sizeof(values[4]), "%+d semitones", edited.transpose);
        for (int i = 0; i < 5; ++i) {
            if (field == i) wattron(w, A_REVERSE | A_BOLD);
            mvwprintw(w, 4+i*2, 3, " %-13s  %-*.*s",
                      labels[i], width-22, width-22, values[i]);
            if (field == i) wattroff(w, A_REVERSE | A_BOLD);
        }
        draw_meter(w, 9, 40, width - 44, edited.volume, UI_ORANGE);
        wattron(w, COLOR_PAIR(UI_GREEN));
        mvwaddnstr(w, 15, 3,
            "ARROWS: select/adjust     SPACE: toggle     ENTER: apply",
            width-6);
        mvwaddnstr(w, 16, 3,
            "ESC: cancel     W: waveform     +/-: volume",
            width-6);
        wattroff(w, COLOR_PAIR(UI_GREEN));
        wrefresh(w);
        int key = wgetch(w);
        delwin(w);
        touchwin(stdscr);
        if (key == 27) return;
        if (key == '\n' || key == KEY_ENTER) {
            tracks[idx] = edited;
            update_preview();
            snprintf(message, sizeof(message), "Updated track %d: %s",
                     idx + 1, edited.name);
            return;
        }
        if (key == KEY_UP && field > 0) field--;
        if (key == KEY_DOWN && field < 4) field++;
        int delta = (key == KEY_LEFT ? -1 : key == KEY_RIGHT ? 1 : 0);
        if (key == 'w' || key == 'W') { field = 1; delta = 1; }
        if (key == '+' || key == '=') { field = 2; delta = 8; }
        if (key == '-') { field = 2; delta = -8; }
        if (key == ' ') {
            if (field == 0) edited.enabled = !edited.enabled;
            else delta = 1;
        }
        if (delta) switch (field) {
            case 0: edited.enabled = !edited.enabled; break;
            case 1: edited.wave = (edited.wave + 5 + (delta > 0 ? 1 : -1)) % 5; break;
            case 2: edited.volume += delta;
                    if (edited.volume < 0) edited.volume = 0;
                    if (edited.volume > 255) edited.volume = 255;
                    break;
            case 3: edited.poly = (edited.poly + 3 + (delta > 0 ? 1 : -1)) % 3; break;
            case 4: edited.transpose += delta;
                    if (edited.transpose < -48) edited.transpose = -48;
                    if (edited.transpose > 48) edited.transpose = 48;
                    break;
        }
    }
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
    visible_count = 0;
    if (COLS < 84 || LINES < 25) {
        attron(COLOR_PAIR(UI_ORANGE) | A_BOLD);
        mvaddstr(0, 0, "PPA-1 MIDI / MIXER");
        attroff(COLOR_PAIR(UI_ORANGE) | A_BOLD);
        mvprintw(2, 1, "Resize terminal to at least 84x25 (now %dx%d).",
                 COLS, LINES);
        mvaddstr(4, 1, "Q: quit");
        refresh();
        return;
    }
    bkgd(COLOR_PAIR(UI_PANEL));
    attron(COLOR_PAIR(UI_HEADER) | A_BOLD);
    mvhline(0, 0, ' ', COLS);
    mvaddstr(0, 2, " PPA-1 / MIDI-to-ASM ");
    mvprintw(0, COLS-30, "[ C11 / NCURSESW ]    %s",
             optimize ? "OPT ON " : "OPT OFF");
    attroff(COLOR_PAIR(UI_HEADER) | A_BOLD);

    attron(COLOR_PAIR(UI_GREEN));
    mvaddnstr(2, 2, "INPUT  ", COLS-3);
    attroff(COLOR_PAIR(UI_GREEN));
    mvaddnstr(2, 9, midi_path[0] ? midi_path : "(open MIDI with F3)",
              COLS-11);
    attron(COLOR_PAIR(UI_GREEN));
    mvaddstr(3, 2, "OUTPUT ");
    attroff(COLOR_PAIR(UI_GREEN));
    mvaddnstr(3, 9, output_path, COLS-11);

    int enabled = 0;
    for (int i = 0; i < ntracks; ++i) if (tracks[i].enabled) enabled++;
    attron(COLOR_PAIR(UI_ORANGE) | A_BOLD);
    mvprintw(5, 2, "%s", optimize ? "[ OPTIMIZE: ON ]" : "[ OPTIMIZE: OFF ]");
    attroff(COLOR_PAIR(UI_ORANGE) | A_BOLD);
    mvprintw(5, 22, "TRACKS %d/%d  |  CPU %d  |  %d ms",
             enabled, ntracks, clock_hz, tick_ms);
    if (preview_valid) {
        int x = COLS - 32;
        mvprintw(5, x, "PEAK %u  DROP* %u",
                 preview.max_simultaneous_notes,
                 preview.dropped_note_events);
    }
    int split = COLS >= 112 ? COLS - 33 : COLS;
    int source_end = LINES - 11;
    attron(COLOR_PAIR(UI_HEADER) | A_BOLD);
    mvhline(7, 0, ' ', COLS);
    mvaddstr(7, 2, source_expanded && optimize
                      ? " SOURCE TRACKS / EXPANDED NOTE LANES "
                      : " SOURCE TRACKS / COMPACT ");
    if (split < COLS) mvaddstr(7, split+2, " TRACK INSPECTOR ");
    attroff(COLOR_PAIR(UI_HEADER) | A_BOLD);
    if (split < COLS)
        for (int y=8; y<source_end; ++y) {
            attron(COLOR_PAIR(UI_GREEN));
            mvaddch(y, split, ACS_VLINE);
            attroff(COLOR_PAIR(UI_GREEN));
        }
    int y = 8;
    for (int i = top; i < ntracks && y < source_end; ++i) {
        PpaMidiTrack *t = &tracks[i];
        if (visible_count < 2048) {
            visible_track[visible_count] = i;
            visible_y[visible_count++] = y;
        }
        if (i == selected) attron(COLOR_PAIR(UI_SELECTED) | A_BOLD);
        else attron(COLOR_PAIR(t->enabled ? UI_GREEN : UI_DIM));
        mvhline(y, 1, ' ', split-2);
        mvprintw(y, 2, "%c%3d %c %-*.*s",
                 source_expanded && optimize ? '-' : '>',
                 i+1, t->enabled ? '+' : 'x',
                 split > 100 ? 27 : 18, split > 100 ? 27 : 18, t->name);
        int meta = split > 100 ? 54 : 40;
        if (meta + 23 < split) {
            mvprintw(y, meta, "%-9.9s %3d",
                     symbols[t->wave], t->volume);
            if (preview_valid) mvprintw(y, meta+15, "x%d",
                 preview.peak_polyphony[i] > 0 ? preview.peak_polyphony[i] : 1);
        }
        if (i == selected) attroff(COLOR_PAIR(UI_SELECTED) | A_BOLD);
        else attroff(COLOR_PAIR(t->enabled ? UI_GREEN : UI_DIM));
        ++y;
        if (source_expanded && optimize && t->enabled && preview_valid) {
            int lanes = preview.peak_polyphony[i];
            if (lanes > 4) lanes = 4;
            if (lanes < 1) lanes = 1;
            for (int lane = 0; lane < lanes && y < source_end; ++lane) {
                if (visible_count < 2048) {
                    visible_track[visible_count] = i;
                    visible_y[visible_count++] = y;
                }
                attron(COLOR_PAIR(UI_ORANGE));
                mvprintw(y, 4, " |-- NOTE LANE %d", lane + 1);
                attroff(COLOR_PAIR(UI_ORANGE));
                int plot_x = 23;
                int plot_w = split - plot_x - 3;
                if (plot_w > 48) plot_w = 48;
                draw_track_timeline(y, plot_x, plot_w, i, lane);
                y++;
            }
            if (preview.peak_polyphony[i] > 4 && y < source_end) {
                attron(COLOR_PAIR(UI_DIM));
                mvprintw(y++, 6, "... %d more logical pitches (4 physical voices max)",
                         preview.peak_polyphony[i] - 4);
                attroff(COLOR_PAIR(UI_DIM));
            }
        }
    }

    if (split < COLS) {
        PpaMidiTrack *t = ntracks ? &tracks[selected] : NULL;
        attron(COLOR_PAIR(UI_GREEN) | A_BOLD);
        if (9 < source_end) mvaddstr(9, split+2, "SELECTED TRACK");
        attroff(COLOR_PAIR(UI_GREEN) | A_BOLD);
        if (t) {
            if (10 < source_end) mvaddnstr(10, split+2, t->name, 29);
            if (12 < source_end)
                mvprintw(12, split+2, "Wave %-10s %s",
                         wave_name[t->wave], symbols[t->wave]);
            if (13 < source_end)
                mvprintw(13, split+2, "Volume  %3d / 255", t->volume);
            if (14 < source_end)
                draw_meter(stdscr, 14, split+2, 26, t->volume, UI_ORANGE);
            if (16 < source_end)
                mvprintw(16, split+2, "Poly    %s", poly_id[t->poly]);
            if (17 < source_end)
                mvprintw(17, split+2, "Pitch   %+d st", t->transpose);
            if (18 < source_end)
                mvprintw(18, split+2, "Enabled %s",
                         t->enabled ? "YES" : "NO");
            if (20 < source_end) {
                attron(COLOR_PAIR(UI_ORANGE) | A_BOLD);
                mvaddstr(20, split+2, "[F4] EDIT TRACK");
                attroff(COLOR_PAIR(UI_ORANGE) | A_BOLD);
            }
        } else if (11 < source_end)
            mvaddstr(11, split+2, "Open a MIDI file.");
    }
    int baseline = LINES - 10;
    attron(COLOR_PAIR(UI_HEADER) | A_BOLD);
    mvhline(baseline, 0, ' ', COLS);
    mvaddstr(baseline, 2, " OUTPUT / 4 PHYSICAL OSCILLATORS  (OPTIMIZED MIX) ");
    attroff(COLOR_PAIR(UI_HEADER) | A_BOLD);
    for (int v = 0; v < 4; ++v) {
        int row = baseline + 1 + v;
        attron(COLOR_PAIR(UI_GREEN) | A_BOLD);
        mvprintw(row, 2, "CH%d", v+1);
        attroff(COLOR_PAIR(UI_GREEN) | A_BOLD);
        int width = COLS - 16;
        if (width > PPA_MIDI_PREVIEW_COLUMNS) width = PPA_MIDI_PREVIEW_COLUMNS;
        if (preview_valid) for (int k = 0; k < width; ++k) {
            int sample = k * (int)preview.cells / width;
            int owner = preview.owner[v][sample];
            attron(COLOR_PAIR(owner ? UI_ORANGE : UI_DIM));
            mvaddch(row, 8+k, owner ? ACS_CKBOARD : ACS_HLINE);
            attroff(COLOR_PAIR(owner ? UI_ORANGE : UI_DIM));
        }
        if (preview_valid) {
            int owners[4] = {0}, seen = 0;
            for (unsigned c = 0; c < preview.cells; ++c) {
                int owner = preview.owner[v][c];
                if (!owner) continue;
                bool known = false;
                for (int j=0; j<seen; ++j) if (owners[j] == owner) known = true;
                if (!known && seen < 4) owners[seen++] = owner;
            }
            int labelx = 10 + width;
            if (labelx < COLS - 8) {
                if (seen) mvprintw(row, labelx, "T%d%s", owners[0],
                                  seen > 1 ? "+" : "");
                else mvaddstr(row, labelx, "idle");
            }
        }
    }
    attron(COLOR_PAIR(UI_HEADER));
    mvhline(LINES-5, 0, ' ', COLS);
    mvaddstr(LINES-5, 2, " F3 OPEN   F4 TRACK SETTINGS   F5 OPTIMIZE   F6 SAVE ASM   TAB EXPAND ");
    attroff(COLOR_PAIR(UI_HEADER));
    attron(COLOR_PAIR(UI_GREEN));
    mvaddnstr(LINES-4, 2,
       "SPACE enable  W wave  P priority  +/- volume  E edit  C clock  T tick",
       COLS-3);
    attroff(COLOR_PAIR(UI_GREEN));
    attron(COLOR_PAIR(UI_ORANGE));
    mvaddnstr(LINES-3, 2,
       "Orange = sounding voice    Green = controls    DIM = silence   Q = quit",
       COLS-3);
    attroff(COLOR_PAIR(UI_ORANGE));
    attron(COLOR_PAIR(UI_DIM));
    mvaddnstr(LINES-2, 2, "*DROP = unallocated note samples; preview is a 64-step snapshot",
              COLS-3);
    attroff(COLOR_PAIR(UI_DIM));
    attron(COLOR_PAIR(UI_GREEN) | A_BOLD);
    mvhline(LINES-1, 0, ' ', COLS);
    mvaddnstr(LINES-1, 2, message, COLS-3);
    attroff(COLOR_PAIR(UI_GREEN) | A_BOLD);
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
        init_pair(UI_HEADER, COLOR_BLACK, COLOR_GREEN);
        init_pair(UI_GREEN, COLOR_GREEN, -1);
        init_pair(UI_ORANGE, COLOR_YELLOW, -1);
        init_pair(UI_SELECTED, COLOR_BLACK, COLOR_YELLOW);
        init_pair(UI_DIM, COLOR_WHITE, -1);
        init_pair(UI_PANEL, COLOR_WHITE, -1);
        init_pair(UI_BAR, COLOR_BLACK, COLOR_YELLOW);
        init_pair(UI_WARNING, COLOR_RED, -1);
    }
    if (argv[1] && *argv[1]) {
        snprintf(midi_path, sizeof(midi_path), "%s", argv[1]);
        load_tracks();
    }
    int selected = 0, top = 0;
    for (;;) {
        if (selected >= ntracks) selected = ntracks ? ntracks - 1 : 0;
        int rows = LINES - 20;
        if (rows < 1) rows = 1;
        if (selected < top) top = selected;
        /* Expanded logical lanes need more space than the parent rows.
         * Scroll by track index and keep the highlighted parent visible. */
        if (selected >= top + (source_expanded && optimize ? rows / 3 + 1 : rows))
            top = selected - (source_expanded && optimize ? rows / 3 : rows - 1);
        render_editor(selected, top);
        int key = getch();
        if (key == KEY_MOUSE) {
            MEVENT mouse;
            if (getmouse(&mouse) == OK &&
                (mouse.bstate & (BUTTON1_CLICKED | BUTTON1_PRESSED |
                                 BUTTON1_DOUBLE_CLICKED))) {
                if (mouse.y == 5 && mouse.x >= 2 && mouse.x < 21) {
                    optimize = !optimize;
                    source_expanded = optimize;
                    update_preview();
                } else if (mouse.y >= 8 && mouse.y < LINES - 11) {
                    for (int i = 0; i < visible_count; ++i) {
                        if (mouse.y != visible_y[i]) continue;
                        selected = visible_track[i];
                        if (mouse.bstate & BUTTON1_DOUBLE_CLICKED)
                            track_settings_dialog(selected);
                        break;
                    }
                } else if (mouse.y == LINES - 5) {
                    if (mouse.x < 12) { /* F3 OPEN */ key = KEY_F(3); }
                    else if (mouse.x < 31) { key = KEY_F(4); }
                    else if (mouse.x < 45) { key = KEY_F(5); }
                    else if (mouse.x < 59) { key = KEY_F(6); }
                    else { key = '\t'; }
                } else continue;
            } else continue;
        }
        if (key == 'q' || key == 'Q') break;
        if (key == 'o' || key == 'O' || key == KEY_F(3)) {
            char path[PATH_MAX];
            if (browse_file(path, sizeof(path))) {
                snprintf(midi_path, sizeof(midi_path), "%s", path);
                optimize = false; source_expanded = false;
                load_tracks(); selected = top = 0;
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
        if (key == '\t') {
            source_expanded = !source_expanded;
            snprintf(message, sizeof(message), "Source logical lanes: %s",
                     source_expanded ? "EXPANDED" : "COMPACT");
            continue;
        }
        if (key == 'e' || key == 'E' || key == KEY_F(4)) {
            if (ntracks) track_settings_dialog(selected);
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
        if (key == 'f' || key == 'F' || key == KEY_F(5)) {
            optimize = !optimize;
            source_expanded = optimize;
            update_preview();
            snprintf(message, sizeof(message), "4-voice polyphony optimizer: %s",
                     optimize ? "EXPANDED -> 4 MIXED OUTPUT VOICES" : "OFF");
            continue;
        }
        if (key == KEY_DOWN && selected + 1 < ntracks) selected++;
        if (key == KEY_UP && selected > 0) selected--;
        if (key == '\n' || key == KEY_ENTER || key == KEY_F(6))
            do_export();
        if (!ntracks) continue;
        PpaMidiTrack *t = &tracks[selected];
        bool changed = false;
        if (key == ' ') { t->enabled = !t->enabled; changed = true; }
        if (key == 'w' || key == 'W') {
            t->wave = (t->wave + 1) % 5; changed = true;
        }
        if (key == 'p' || key == 'P') {
            t->poly = (t->poly + 1) % 3; changed = true;
        }
        if (key == '+' || key == '=') {
            t->volume += 8; if (t->volume > 255) t->volume = 255;
            changed = true;
        }
        if (key == '-') {
            t->volume -= 8; if (t->volume < 0) t->volume = 0;
            changed = true;
        }
        if (changed) update_preview();
    }
    endwin();
    return 0;
}
