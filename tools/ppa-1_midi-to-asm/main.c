/*
 * PPA-1_MIDI-to-ASM terminal track editor.
 * Native C11 + ncursesw frontend, Python 3 stdlib MIDI conversion core.
 * No shell command concatenation: all converter invocations use execvp.
 * (c) 2026 Adava Development
 */
#define _POSIX_C_SOURCE 200809L
#include <ncurses.h>
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
#define TRACK_MAX 1024
#define ROW_MAX 512
#define FILE_MAX 4096

typedef struct {
    char id[32], name[ROW_MAX];
    unsigned notes;
    bool enabled;
    int voice, wave, poly, volume, transpose;
} Track;
typedef struct {
    char name[NAME_MAX + 8];
    bool directory;
} Entry;
static const char *waves[] = {"SQUARE", "SAW", "TRIANGLE", "SINE", "NOISE"};
static const char *symbols[] = {"|-|_|-|_", "/|/|/|/|", "/\\/\\/\\/\\", "~^~^~^~^", ".:*?^*.: "};
static const char *wave_id[] = {"square", "saw", "triangle", "sine", "noise"};
static const char *poly_id[] = {"low", "high", "latest"};
static Track tracks[TRACK_MAX];
static int ntracks = 0;
static char midi_path[PATH_MAX] = "";
static char backend_path[PATH_MAX] = "tools/PPA-1_MIDI-to-ASM.py";
static char repository_root[PATH_MAX] = ".";

/* Resolve companion Python conversion core independently of CWD.
 * The native TUI normally lives under <repository>/out/tools/. */
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
            snprintf(candidate, sizeof(candidate), "%s/../../tools/PPA-1_MIDI-to-ASM.py", exe);
            if (access(candidate, R_OK) == 0) {
                snprintf(backend_path, sizeof(backend_path), "%s", candidate);
                snprintf(candidate, sizeof(candidate), "%s/../..", exe);
                if (realpath(candidate, repository_root) == NULL)
                    snprintf(repository_root, sizeof(repository_root), ".");
            }
        }
    }
#endif
}
static char output_path[PATH_MAX] = "examples/music.asm";
static char message[256] = "O=open MIDI  Space=remove/restore  W=wave  P=polyphony  +/-=volume  F=optimize";
static bool optimize = false;

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
static int spawn_converter(char *const argv[], char *capture, size_t cap) {
    int fds[2];
    if (pipe(fds) != 0) return -1;
    pid_t pid = fork();
    if (pid < 0) { close(fds[0]); close(fds[1]); return -1; }
    if (pid == 0) {
        close(fds[0]);
        dup2(fds[1], STDOUT_FILENO);
        dup2(fds[1], STDERR_FILENO);
        close(fds[1]);
        execvp(argv[0], argv);
        _exit(127);
    }
    close(fds[1]);
    size_t pos = 0;
    char buf[2048];
    ssize_t n;
    while ((n = read(fds[0], buf, sizeof(buf))) > 0) {
        if (capture && cap > 1 && pos < cap - 1) {
            size_t add = (size_t)n;
            if (add > cap - pos - 1) add = cap - pos - 1;
            memcpy(capture + pos, buf, add);
            pos += add;
        }
    }
    close(fds[0]);
    if (capture && cap) capture[pos] = '\0';
    int status;
    if (waitpid(pid, &status, 0) < 0) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
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
static void load_tracks(void) {
    char *args[] = {"python3", backend_path,
                     midi_path, "--list", NULL};
    /* Avoid losing long track listings. */
    char *output = malloc(512 * 1024);
    if (!output) return;
    int status = spawn_converter(args, output, 512 * 1024);
    if (status) {
        snprintf(message, sizeof(message), "MIDI parser: %.180s", output);
        free(output);
        return;
    }
    ntracks = 0;
    for (char *line = strtok(output, "\n"); line;
         line = strtok(NULL, "\n")) {
        if (ntracks >= TRACK_MAX) break;
        char *sep = strstr(line, " | ");
        if (!sep) continue;
        *sep = 0;
        unsigned tr, ch;
        if (sscanf(line, "%u:%u", &tr, &ch) != 2) continue;
        Track *t = &tracks[ntracks];
        memset(t, 0, sizeof(*t));
        snprintf(t->id, sizeof(t->id), "%u:%u", tr, ch);
        char *name = sep + 3, *end = strstr(name, " | notes=");
        if (!end) continue;
        *end = 0;
        snprintf(t->name, sizeof(t->name), "%s", name);
        t->notes = (unsigned)strtoul(end + 9, NULL, 10);
        /* Start with every track included: the user removes unwanted ones.
         * More than four tracks require pressing OPTIMIZE before export. */
        t->enabled = true;
        t->voice = ntracks < 4 ? ntracks + 1 : 0;
        t->wave = ntracks % 4;
        t->volume = 200;
        t->poly = 1;
        ntracks++;
    }
    free(output);
    const char *base = strrchr(midi_path, '/');
    base = base ? base + 1 : midi_path;
    char stem[160];
    snprintf(stem, sizeof(stem), "%s", base);
    char *dot = strrchr(stem, '.'); if (dot) *dot = 0;
    snprintf(output_path, sizeof(output_path), "%s/examples/%s.asm",
             repository_root, stem);
    snprintf(message, sizeof(message), "Loaded %d MIDI track/channel streams", ntracks);
}
static void json_string(FILE *f, const char *s) {
    fputc('"', f);
    for (; *s; s++) {
        unsigned char ch = (unsigned char)*s;
        if (ch == '"' || ch == '\\') { fputc('\\', f); fputc(ch, f); }
        else if (ch == '\n') fputs("\\n", f);
        else if (ch >= 32) fputc(ch, f);
    }
    fputc('"', f);
}
static bool write_config(const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return false;
    fprintf(f, "{\n  \"format\": \"PPA-1_MIDI-to-ASM/1\",\n"
               "  \"optimize\": %s,\n  \"tracks\": {\n",
            optimize ? "true" : "false");
    int selected = 0;
    for (int i = 0; i < ntracks; i++) {
        Track *t = &tracks[i];
        if (t->enabled) selected++;
        fprintf(f, "    ");
        json_string(f, t->id);
        fprintf(f, ": {\"enabled\": %s, \"voice\": %d, "
                   "\"waveform\": \"%s\", \"polyphony\": \"%s\", "
                   "\"volume\": %d, \"transpose\": %d}%s\n",
                t->enabled ? "true" : "false",
                !t->enabled ? 0 : (optimize ? 0 : t->voice),
                wave_id[t->wave], poly_id[t->poly], t->volume,
                t->transpose, i + 1 == ntracks ? "" : ",");
    }
    fprintf(f, "  }\n}\n");
    return fclose(f) == 0 && selected > 0;
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
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "/tmp/ppa-midi-XXXXXX");
    int fd = mkstemp(path);
    if (fd < 0) { snprintf(message, sizeof(message), "Cannot create temp config."); return; }
    close(fd);
    if (!write_config(path)) {
        unlink(path);
        snprintf(message, sizeof(message), "No enabled MIDI tracks.");
        return;
    }
    char *args[] = {"python3", backend_path,
                    midi_path, "--config", path, "-o", output_path, NULL};
    char response[512];
    int result = spawn_converter(args, response, sizeof(response));
    unlink(path);
    if (result == 0)
        snprintf(message, sizeof(message), "Exported: %.185s", output_path);
    else
        snprintf(message, sizeof(message), "Export error: %.180s", response);
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
    printw("%d / %d", count, ntracks);
    if (COLS >= 40) {
        if (optimize) attron(A_BOLD | COLOR_PAIR(3));
        mvaddstr(5, 2, "[ OPTIMIZE 4 CHANNELS ]  (F or click)");
        if (optimize) attroff(A_BOLD | COLOR_PAIR(3));
    }
    line_at(6, 2, " #  Use  MIDI track / channel                  Notes    Waveform       Poly    Vol");
    int rows = LINES - 11;
    for (int y = 0; y < rows && y + top < ntracks; y++) {
        int i = top + y;
        Track *t = &tracks[i];
        if (i == selected) attron(A_REVERSE);
        mvprintw(7 + y, 1,
                 "%3d  %-3s  %-35.35s  %6u   %-10s %-7s  %3d",
                 i + 1, t->enabled ? "YES" : "NO",
                 t->name, t->notes, symbols[t->wave],
                 poly_id[t->poly], t->volume);
        if (i == selected) attroff(A_REVERSE);
    }
    line_at(LINES - 4, 2, " UP/DOWN: select  SPACE: remove/restore  W: waveform  P: low/high/latest  +/-: volume");
    line_at(LINES - 3, 2, " O: open MIDI  F: optimize  A: output path  ENTER: export ASM  Q: quit");
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
            char path[PATH_MAX] = "";
            ask_string("Output ASM path: ", path, sizeof(path));
            if (*path) snprintf(output_path, sizeof(output_path), "%s", path);
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
        Track *t = &tracks[selected];
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
