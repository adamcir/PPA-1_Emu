/*
 * PPA-1_MIDI-to-ASM terminal track editor.
 * Native C11 + ncursesw frontend and native C11 MIDI conversion engine.
 * No Python runtime, external MIDI library or shell invocation required.
 * (c) 2026 Adava Development
 */
#define _POSIX_C_SOURCE 200809L
#include <ncurses.h>
#include "midi_core.h"
#include "project.h"
#include "browser.h"
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
static int visible_part[2048];
static int visible_y[2048];
static int visible_count = 0;
static PpaMidiTrack tracks[TRACK_MAX];
static PpaMidiPart parts[TRACK_MAX * PPA_MIDI_PARTS_PER_TRACK];
static int selected_part = -1;
static char project_name[160] = "Untitled";
static char owned_midi[PATH_MAX] = "";
static PpaBrowserState browser;
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
            size_t len = strlen(exe);
            if (len + sizeof("/../..") <= sizeof(candidate)) {
                memcpy(candidate, exe, len);
                memcpy(candidate + len, "/../..", sizeof("/../.."));
                if (!realpath(candidate, repository_root))
                    snprintf(repository_root, sizeof(repository_root), ".");
            }
        }
    }
#endif
}
static char output_path[PATH_MAX] = "examples/music.asm";
static char message[256] = "O=open MIDI  Space=remove/restore  W=wave  P=polyphony  +/-=volume  F=optimize";
static bool optimize = false;
static int clock_hz = 100000;
static int tick_ms = 10;

static void ask_string(const char *prompt, char *out, size_t cap) {
    echo(); curs_set(1);
    int row = LINES - 2;
    move(row, 0); clrtoeol();
    mvaddnstr(row, 0, prompt, COLS - 2);
    move(row, (int)strlen(prompt));
    getnstr(out, (int)(cap - 1));
    noecho(); curs_set(0);
}
/* All filesystem operations use the same persistent ncurses popup. */
static bool browse_file(char *selected, size_t size) {
    return ppa_browser_show(&browser, PPA_BROWSER_MIDI, NULL, selected, size);
}
static bool export_dialog(char *destination, size_t capacity) {
    const char *name = strrchr(destination, '/');
    name = name ? name+1 : destination;
    return ppa_browser_show(&browser, PPA_BROWSER_ASM_SAVE,
                            name, destination, capacity);
}

static void update_preview(void);

static void load_tracks(void) {
    if (owned_midi[0]) { unlink(owned_midi); owned_midi[0] = 0; }
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
    /* Clip the display/project stem deliberately; do not silently
     * overrun the fixed-size project name field. */
    size_t stem_len = strnlen(base, sizeof(stem) - 1);
    memcpy(stem, base, stem_len);
    stem[stem_len] = '\0';
    char *dot = strrchr(stem, '.');
    if (dot) *dot = 0;
    for (char *p = stem; *p; ++p)
        if (!isalnum((unsigned char)*p) && *p != '_' && *p != '-')
            *p = '_';
    /* ASM and .ppasp projects share ~/PPA-Songs as the initial folder;
     * after export we remember whichever directory the user chose. */
    snprintf(output_path, sizeof(output_path), "%s/%s.asm",
             ppa_browser_last(&browser, PPA_BROWSER_ASM_SAVE), stem);
    snprintf(project_name, sizeof(project_name), "%s", stem);
    for (int i=0; i<ntracks; ++i)
        for (int lane=0;lane<4;lane++)
            parts[i*4+lane]=(PpaMidiPart){true,tracks[i].wave,255,0};
    selected_part = -1;
    snprintf(message, sizeof(message), "Loaded %d MIDI track/channel streams",
             ntracks);
    update_preview();
}
static void update_preview(void) {
    preview_valid = false;
    if (!ntracks || !midi_path[0]) return;
    char error[256] = "";
    preview_valid = ppa_midi_preview_parts(midi_path, tracks, ntracks,
                                       parts, optimize, &preview,
                                       error, sizeof(error));
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
        int found = 0;
        for (int voice = 0; voice < 4; ++voice) {
            if (preview.owner[voice][sample] == track + 1 &&
                preview.part[voice][sample] == lane) {
                found = 1;
                break;
            }
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
            int previous_wave = tracks[idx].wave;
            tracks[idx] = edited;
            if (edited.wave != previous_wave)
                for (int lane=0;lane<4;lane++) {
                    PpaMidiPart *part=&parts[idx*4+lane];
                    if (part->wave==previous_wave)part->wave=edited.wave;
                }
            update_preview();
            snprintf(message, sizeof(message), "Updated track %d: %.190s",
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

/* Part-level editor: these settings follow one logical optimizer lane
 * independently of the parent track's default settings. */
static void part_settings_dialog(int track, int lane) {
    if (track < 0 || track >= ntracks || lane < 0 || lane >= 4) return;
    PpaMidiPart edited = parts[track*4+lane];
    int field = 0;
    for (;;) {
        int height=17, width=COLS>76?74:COLS-2;
        if (LINES<height+2||width<52) return;
        WINDOW *w=newwin(height,width,(LINES-height)/2,(COLS-width)/2);
        if (!w) return;
        keypad(w,TRUE);wbkgd(w,COLOR_PAIR(UI_PANEL));box(w,0,0);
        wattron(w,COLOR_PAIR(UI_ORANGE)|A_BOLD);
        mvwprintw(w,0,3," EDIT PART %d / TRACK %d ",lane+1,track+1);
        wattroff(w,COLOR_PAIR(UI_ORANGE)|A_BOLD);
        mvwaddnstr(w,2,3,tracks[track].name,width-6);
        const char *labels[]={"Enabled","Waveform","Volume","Transpose"};
        char values[4][110];
        snprintf(values[0],sizeof(values[0]),"%s",edited.enabled?"YES":"NO");
        snprintf(values[1],sizeof(values[1]),"%s   %s",
                 wave_name[edited.wave],symbols[edited.wave]);
        snprintf(values[2],sizeof(values[2]),"%d / 255",edited.volume);
        snprintf(values[3],sizeof(values[3]),"%+d semitones",edited.transpose);
        for(int i=0;i<4;i++){
            if(field==i)wattron(w,A_REVERSE|A_BOLD);
            mvwprintw(w,4+i*2,3," %-14s %-*.*s",
                      labels[i],width-23,width-23,values[i]);
            if(field==i)wattroff(w,A_REVERSE|A_BOLD);
        }
        draw_meter(w,9,37,width-41,edited.volume,UI_ORANGE);
        wattron(w,COLOR_PAIR(UI_GREEN));
        mvwaddnstr(w,13,3,"LEFT/RIGHT: adjust  UP/DOWN: select",width-6);
        mvwaddnstr(w,14,3,"ENTER: apply  ESC: cancel  W: waveform",width-6);
        wattroff(w,COLOR_PAIR(UI_GREEN));
        wrefresh(w);int key=wgetch(w);delwin(w);touchwin(stdscr);
        if(key==27)return;
        if(key=='\n'||key==KEY_ENTER){
            parts[track*4+lane]=edited;
            update_preview();
            snprintf(message,sizeof(message),"Updated track %d / part %d",
                     track+1,lane+1);
            return;
        }
        if(key==KEY_UP&&field>0)field--;
        if(key==KEY_DOWN&&field<3)field++;
        int delta=key==KEY_LEFT?-1:key==KEY_RIGHT?1:0;
        if(key=='w'||key=='W'){field=1;delta=1;}
        if(key=='+'||key=='='){field=2;delta=8;}
        if(key=='-'){field=2;delta=-8;}
        if(key==' '){if(field==0)edited.enabled=!edited.enabled;else delta=1;}
        if(delta)switch(field){
            case 0:edited.enabled=!edited.enabled;break;
            case 1:edited.wave=(edited.wave+5+(delta>0?1:-1))%5;break;
            case 2:edited.volume+=delta;
                   if(edited.volume<0)edited.volume=0;
                   if (edited.volume > 255) edited.volume = 255;
                   break;
            case 3:edited.transpose+=delta>0?1:-1;
                   if(edited.transpose< -48)edited.transpose=-48;
                   if (edited.transpose > 48) edited.transpose = 48;
                   break;
        }
    }
}
static void edit_selected_dialog(int track) {
    if (track<0||track>=ntracks) return;
    if (selected_part >= 0 && optimize)
        part_settings_dialog(track, selected_part);
    else
        track_settings_dialog(track);
}

/* The PPASP project keeps a full copy of the MIDI data and all settings.
 * Saving a project must NEVER write or replace an ASM file. */
static void save_project(void) {
    if (!ntracks||!midi_path[0]) {
        snprintf(message,sizeof(message),"Open MIDI before saving a PPASP project");
        return;
    }
    char saved[PATH_MAX],error[256]="";
    if (ppa_project_save(project_name,midi_path,tracks,parts,ntracks,
                         optimize,clock_hz,tick_ms,saved,sizeof(saved),
                         error,sizeof(error))) {
        snprintf(message,sizeof(message),"SAVED: %.210s",saved);
    } else snprintf(message,sizeof(message),"PPASP save failed: %.180s",error);
}

/* PPASP picker shares the same favorite and recent folders as MIDI/ASM. */
static bool pick_project(char *chosen, size_t cap) {
    return ppa_browser_show(&browser, PPA_BROWSER_PROJECT, NULL, chosen, cap);
}
static void load_project(void) {
    char path[PATH_MAX],temp[PATH_MAX],error[256]="";
    if(!pick_project(path,sizeof(path)))return;
    PpaMidiTrack *newtracks=calloc(TRACK_MAX,sizeof(*newtracks));
    PpaMidiPart *newparts=calloc(TRACK_MAX*4,sizeof(*newparts));
    if(!newtracks||!newparts) {
        free(newtracks);free(newparts);
        snprintf(message,sizeof(message),"Not enough memory to open project");
        return;
    }
    int count=0,clock=clock_hz,tick=tick_ms;bool optim=false;
    if(!ppa_project_load(path,temp,sizeof(temp),newtracks,newparts,
                         TRACK_MAX,&count,&optim,&clock,&tick,error,sizeof(error))) {
        snprintf(message,sizeof(message),"PPASP load failed: %.180s",error);
        free(newtracks);free(newparts);return;
    }
    if(owned_midi[0])unlink(owned_midi);
    snprintf(owned_midi,sizeof(owned_midi),"%s",temp);
    snprintf(midi_path,sizeof(midi_path),"%s",temp);
    memcpy(tracks,newtracks,(size_t)count*sizeof(*tracks));
    memcpy(parts,newparts,(size_t)count*4*sizeof(*parts));
    free(newtracks);free(newparts);
    ntracks=count;optimize=optim;clock_hz=clock;tick_ms=tick;
    selected_part=-1;source_expanded=optimize;
    const char *base=strrchr(path,'/');
    base=base?base+1:path;
    size_t project_len = strnlen(base, sizeof(project_name) - 1);
    memcpy(project_name, base, project_len);
    project_name[project_len] = '\0';
    char *dot=strrchr(project_name,'.');if(dot)*dot=0;
    snprintf(output_path,sizeof(output_path),"%s/%s.asm",
             ppa_browser_last(&browser,PPA_BROWSER_ASM_SAVE),project_name);
    update_preview();
    snprintf(message,sizeof(message),"Loaded PPASP project: %s",project_name);
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
    if (ppa_midi_export_parts(midi_path, tracks, ntracks, parts, optimize,
                              output_path, clock_hz, tick_ms,
                              &count, &banks, error, sizeof(error)))
        snprintf(message, sizeof(message), "Exported %d frames / %d ROM bank(s)",
                 count, banks);
    else
        snprintf(message, sizeof(message), "Export error: %.180s", error);
}
/* Track-list scroll is based on actual rendered rows, not the number of
 * MIDI tracks. Expanded PART 1-4 rows and the overflow notice consume
 * screen space, too. Keeping the selected line inside the viewport
 * prevents keyboard navigation from disappearing below the lower panel. */
static int track_view_rows(void) {
    int rows=LINES-19; /* source list occupies [8, LINES-11) */
    return rows>0?rows:1;
}
static int track_display_rows(int idx) {
    if(idx<0||idx>=ntracks)return 0;
    int rows=1;
    if(source_expanded&&optimize&&preview_valid&&tracks[idx].enabled) {
        int peak=preview.peak_polyphony[idx];
        int lanes=peak;
        if(lanes<1)lanes=1;
        if(lanes>4)lanes=4;
        rows+=lanes;
        if(peak>4)rows++;
    }
    return rows;
}
static int selected_display_offset(int idx,int part) {
    if(part<0||idx<0||idx>=ntracks)return 0;
    if(!(source_expanded&&optimize&&preview_valid&&tracks[idx].enabled))
        return 0;
    int lanes=preview.peak_polyphony[idx];
    if(lanes<1)lanes=1;
    if(lanes>4)lanes=4;
    if(part>=lanes)part=lanes-1;
    return 1+part; /* 0=parent row; 1..4=PART lanes */
}
static int scroll_selected_row(int first_track,int selected,int selected_part) {
    int offset=selected_display_offset(selected,selected_part);
    for(int i=first_track;i<selected;i++)offset+=track_display_rows(i);
    return offset;
}
static void ensure_track_visible(int *first_track,int selected,int selected_part) {
    if(ntracks<=0){*first_track=0;return;}
    if(selected<0)selected=0;
    if(selected>=ntracks)selected=ntracks-1;
    if(*first_track<0)*first_track=0;
    if(*first_track>selected)*first_track=selected;
    int visible=track_view_rows();
    /* Advance the first visible track until the selected parent or
     * expanded part is actually on-screen. */
    while(*first_track<selected &&
          scroll_selected_row(*first_track,selected,selected_part)>=visible)
        ++*first_track;
    /* A selected PART of the first visible track may also be beyond
     * viewport on unusually short terminals; scroll cannot move
     * within that track, but the UI requires >=25 lines (6 rows). */
}
static void move_track_selection(int direction,int *selected,int *part) {
    if(ntracks<=0)return;
    if(direction>0) {
        if(*part>=0) {
            int lanes=preview_valid?preview.peak_polyphony[*selected]:0;
            if(lanes>4)lanes=4;
            if(*part+1<lanes){++*part;return;}
        } else if(source_expanded&&optimize&&preview_valid&&
                  tracks[*selected].enabled) {
            int lanes=preview.peak_polyphony[*selected];
            if(lanes<1)lanes=1;
            *part=0;return;
        }
        if(*selected+1<ntracks){++*selected;*part=-1;}
    } else {
        if(*part>=0) {
            if(*part>0)--*part;
            else *part=-1;
            return;
        }
        if(*selected>0) {
            --*selected;
            *part=-1;
            if(source_expanded&&optimize&&preview_valid&&
               tracks[*selected].enabled) {
                int lanes=preview.peak_polyphony[*selected];
                if(lanes<1)lanes=1;
                if(lanes>4)lanes=4;
                *part=lanes-1;
            }
        }
    }
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
    if (COLS > 110) {
        attron(COLOR_PAIR(UI_ORANGE));
        mvprintw(4, 2, "PROJECT: %-40.40s    SAVE: ~/PPA-Songs/%s.ppasp",
                 project_name, project_name);
        attroff(COLOR_PAIR(UI_ORANGE));
    }

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
    if (ntracks && split > 78)
        mvprintw(7, split-24, " TRACK %d/%d  TOP %d ",
                 selected+1,ntracks,top+1);
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
            visible_part[visible_count] = -1;
            visible_y[visible_count++] = y;
        }
        if (i == selected && selected_part == -1)
            attron(COLOR_PAIR(UI_SELECTED) | A_BOLD);
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
        if (i == selected && selected_part == -1)
            attroff(COLOR_PAIR(UI_SELECTED) | A_BOLD);
        else attroff(COLOR_PAIR(t->enabled ? UI_GREEN : UI_DIM));
        ++y;
        if (source_expanded && optimize && t->enabled && preview_valid) {
            int lanes = preview.peak_polyphony[i];
            if (lanes > 4) lanes = 4;
            if (lanes < 1) lanes = 1;
            for (int lane = 0; lane < lanes && y < source_end; ++lane) {
                if (visible_count < 2048) {
                    visible_track[visible_count] = i;
                    visible_part[visible_count] = lane;
                    visible_y[visible_count++] = y;
                }
                attron(COLOR_PAIR(UI_ORANGE));
                if (i == selected && selected_part == lane)
                    attron(A_REVERSE | A_BOLD);
                PpaMidiPart *part=&parts[i*4+lane];
                mvprintw(y, 4, " |-- PART %d  %-9.9s %3d %s",
                          lane + 1, symbols[part->wave], part->volume,
                          part->enabled ? "ON " : "OFF");
                if (i == selected && selected_part == lane)
                    attroff(A_REVERSE | A_BOLD);
                attroff(COLOR_PAIR(UI_ORANGE));
                int plot_x = 34;
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
        if (t && selected_part >= 0) {
            PpaMidiPart *part=&parts[selected*4+selected_part];
            if (10 < source_end) mvprintw(10,split+2,"TRACK %d / PART %d",
                                             selected+1,selected_part+1);
            if (12 < source_end) mvprintw(12,split+2,"Wave  %-10s",
                                             wave_name[part->wave]);
            if (13 < source_end) mvaddnstr(13,split+2,
                                             symbols[part->wave],25);
            if (15 < source_end) mvprintw(15,split+2,"Volume %3d/255",
                                             part->volume);
            if (16 < source_end) draw_meter(stdscr,16,split+2,25,
                                             part->volume,UI_ORANGE);
            if (18 < source_end) mvprintw(18,split+2,"Pitch  %+d st",
                                             part->transpose);
            if (19 < source_end) mvprintw(19,split+2,"Enabled %s",
                                             part->enabled?"YES":"NO");
            if (20 < source_end) mvaddstr(20,split+2,"[F4] EDIT PART");
        } else if (t) {
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
    mvaddstr(LINES-5, 2,
       " F3 MIDI  F4 EDIT  F5 OPTIMIZE  F6 EXPORT ASM  F7 SAVE PPASP  F8 LOAD ");
    attroff(COLOR_PAIR(UI_HEADER));
    attron(COLOR_PAIR(UI_GREEN));
    mvaddnstr(LINES-4, 2,
       "DEL remove track  R restore  SPACE toggle  TAB expand  N project name",
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
    ppa_browser_init(&browser);
    snprintf(output_path,sizeof(output_path),"%s/music.asm",
             ppa_browser_last(&browser,PPA_BROWSER_ASM_SAVE));
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
        if (ntracks && (selected_part>=0) &&
            !(source_expanded && optimize && preview_valid &&
              tracks[selected].enabled))
            selected_part=-1;
        ensure_track_visible(&top,selected,selected_part);
        render_editor(selected, top);
        int key = getch();
        if (key == KEY_MOUSE) {
            MEVENT mouse;
            if (getmouse(&mouse) == OK) {
                /* Mouse wheel scrolls the track/part selection, not the
                 * lower mixer panel. Each step keeps the row visible. */
                bool wheel_up=false, wheel_down=false;
#ifdef BUTTON4_PRESSED
                wheel_up=(mouse.bstate & BUTTON4_PRESSED)!=0;
#endif
#ifdef BUTTON5_PRESSED
                wheel_down=(mouse.bstate & BUTTON5_PRESSED)!=0;
#endif
                if (wheel_up || wheel_down) {
                    if (mouse.y >= 7 && mouse.y < LINES-11) {
                        for(int step=0;step<3;step++)
                            move_track_selection(wheel_down?1:-1,
                                                 &selected,&selected_part);
                    }
                    continue;
                }
                if (!(mouse.bstate & (BUTTON1_CLICKED | BUTTON1_PRESSED |
                                      BUTTON1_DOUBLE_CLICKED))) continue;
                if (mouse.y == 5 && mouse.x >= 2 && mouse.x < 21) {
                    optimize = !optimize;
                    source_expanded = optimize;
                    if (!optimize) selected_part=-1;
                    update_preview();
                } else if (mouse.y >= 8 && mouse.y < LINES - 11) {
                    for (int i = 0; i < visible_count; ++i) {
                        if (mouse.y != visible_y[i]) continue;
                        selected = visible_track[i];
                        selected_part = visible_part[i];
                        if (mouse.bstate & BUTTON1_DOUBLE_CLICKED)
                            edit_selected_dialog(selected);
                        break;
                    }
                } else if (mouse.y == LINES - 5) {
                    if (mouse.x < 11) key = KEY_F(3);
                    else if (mouse.x < 21) key = KEY_F(4);
                    else if (mouse.x < 35) key = KEY_F(5);
                    else if (mouse.x < 51) key = KEY_F(6);
                    else if (mouse.x < 67) key = KEY_F(7);
                    else key = KEY_F(8);
                } else continue;
            } else continue;
        }
        if (key == KEY_F(7) || key == 's' || key == 'S') {
            save_project(); continue;
        }
        if (key == KEY_F(8) || key == 'l' || key == 'L') {
            load_project(); selected=top=0; continue;
        }
        if (key == 'n' || key == 'N') {
            char name[160]="";
            ask_string("Project name (saved as ~/PPA-Songs/<name>.ppasp): ",
                       name,sizeof(name));
            if(*name) {
                for(char *p=name;*p;p++)
                    if(!isalnum((unsigned char)*p)&&*p!='_'&&*p!='-')*p='_';
                snprintf(project_name,sizeof(project_name),"%s",name);
            }
            continue;
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
            if (!source_expanded) selected_part=-1;
            snprintf(message, sizeof(message), "Source logical lanes: %s",
                     source_expanded ? "EXPANDED" : "COMPACT");
            continue;
        }
        if (key == 'e' || key == 'E' || key == KEY_F(4)) {
            if (ntracks) edit_selected_dialog(selected);
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
            if (!optimize) selected_part=-1;
            update_preview();
            snprintf(message, sizeof(message), "4-voice polyphony optimizer: %s",
                     optimize ? "EXPANDED -> 4 MIXED OUTPUT VOICES" : "OFF");
            continue;
        }
        if (key == KEY_DOWN) move_track_selection(1,&selected,&selected_part);
        if (key == KEY_UP) move_track_selection(-1,&selected,&selected_part);
        if (key == KEY_NPAGE) {
            int step=track_view_rows();
            while(step-->0)move_track_selection(1,&selected,&selected_part);
        }
        if (key == KEY_PPAGE) {
            int step=track_view_rows();
            while(step-->0)move_track_selection(-1,&selected,&selected_part);
        }
        if (key == KEY_HOME && ntracks){selected=0;selected_part=-1;}
        if (key == KEY_END && ntracks){selected=ntracks-1;selected_part=-1;}
        if (key == '\n' || key == KEY_ENTER || key == KEY_F(6))
            do_export();
        if (!ntracks) continue;
        PpaMidiTrack *t = &tracks[selected];
        PpaMidiPart *part=selected_part>=0?
            &parts[selected*4+selected_part]:NULL;
        bool changed=false;
        if(key==KEY_DC||key=='d'||key=='D'){
            t->enabled=false;changed=true;
            snprintf(message,sizeof(message),"TRACK %d REMOVED",selected+1);
        }
        if(key=='r'||key=='R'){
            t->enabled=true;changed=true;
            snprintf(message,sizeof(message),"TRACK %d RESTORED",selected+1);
        }
        if(key==' '){
            if(part&&optimize)part->enabled=!part->enabled;
            else t->enabled=!t->enabled;
            changed=true;
        }
        if(key=='w'||key=='W'){
            if(part&&optimize)part->wave=(part->wave+1)%5;
            else{
                int previous=t->wave;
                t->wave=(t->wave+1)%5;
                for(int lane=0;lane<4;lane++){
                    PpaMidiPart *p=&parts[selected*4+lane];
                    if(p->wave==previous)p->wave=t->wave;
                }
            }
            changed=true;
        }
        if(key=='p'||key=='P'){
            t->poly=(t->poly+1)%3;changed=true;
        }
        if(key=='+'||key=='='||key=='-'){
            int delta=key=='-'?-8:8;
            int *volume=part&&optimize?&part->volume:&t->volume;
            *volume+=delta;
            if(*volume<0)*volume=0;
            if(*volume>255)*volume=255;
            changed=true;
        }
        if (changed) update_preview();
    }
    endwin();
    if(owned_midi[0])unlink(owned_midi);
    return 0;
}
