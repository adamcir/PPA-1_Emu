#define _POSIX_C_SOURCE 200809L
#include "browser.h"
#include <ncurses.h>
#include <dirent.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <ctype.h>
#include <limits.h>

#define MAX_ENTRIES 4096
#define MAX_FAVORITES 8
typedef struct { char name[NAME_MAX+8]; bool dir; } BrowserEntry;
static void safe_copy(char *dst,size_t cap,const char *src) {
    if(cap)snprintf(dst,cap,"%s",src?src:"");
}
static bool is_dir(const char *path) {
    struct stat st;
    return stat(path,&st)==0&&S_ISDIR(st.st_mode);
}
static bool regular_file(const char *path) {
    struct stat st;
    return stat(path,&st)==0&&S_ISREG(st.st_mode);
}
static bool join(char *dest,size_t cap,const char *parent,const char *file) {
    int n=snprintf(dest,cap,"%s%s%s",parent,strcmp(parent,"/")?"/":"",file);
    return n>0&&(size_t)n<cap;
}
static bool app_config_dir(char *out,size_t cap,bool create) {
    const char *base=getenv("XDG_CONFIG_HOME");
    char fallback[PATH_MAX];
    if(!base||!base[0]) {
        base=getenv("HOME");
        if(!base||!base[0])return false;
        if(!join(fallback,sizeof(fallback),base,".config"))return false;
        base=fallback;
        if(create&&mkdir(base,0700)!=0&&errno!=EEXIST)return false;
    }
    if(!join(out,cap,base,"ppa-1-midi-to-asm"))return false;
    if(create&&mkdir(out,0700)!=0&&errno!=EEXIST)return false;
    return !create||is_dir(out);
}
static char *state_path(char *dest,size_t cap,bool create) {
    char folder[PATH_MAX];
    if(!app_config_dir(folder,sizeof(folder),create)||
       !join(dest,cap,folder,"browser.conf"))return NULL;
    return dest;
}
static const char *suffix(const char *file) {
    return strrchr(file,'.');
}
bool ppa_browser_accepts(PpaBrowserMode mode,const char *name) {
    const char *ext=suffix(name);
    if(!ext)return false;
    if(mode==PPA_BROWSER_MIDI || mode==PPA_BROWSER_OPEN)
        return !strcasecmp(ext,".mid")||!strcasecmp(ext,".midi")||
               (mode==PPA_BROWSER_OPEN&&!strcasecmp(ext,".ppasp"));
    if(mode==PPA_BROWSER_PROJECT)return !strcasecmp(ext,".ppasp");
    return !strcasecmp(ext,".asm");
}
static int compare_entries(const void *a,const void *b) {
    const BrowserEntry *l=a,*r=b;
    if(l->dir!=r->dir)return r->dir-l->dir;
    return strcasecmp(l->name,r->name);
}
static size_t list_entries(const char *dir,PpaBrowserMode mode,BrowserEntry *list) {
    DIR *dp=opendir(dir);
    if(!dp)return 0;
    size_t count=0;
    struct dirent *de;
    while((de=readdir(dp))&&count<MAX_ENTRIES) {
        if(!strcmp(de->d_name,"."))continue;
        char full[PATH_MAX];
        if(!join(full,sizeof(full),dir,de->d_name))continue;
        struct stat st;
        if(stat(full,&st))continue;
        bool folder=S_ISDIR(st.st_mode);
        if(!folder&&(!S_ISREG(st.st_mode)||!ppa_browser_accepts(mode,de->d_name)))continue;
        safe_copy(list[count].name,sizeof(list[count].name),de->d_name);
        list[count].dir=folder;
        ++count;
    }
    closedir(dp);
    qsort(list,count,sizeof(list[0]),compare_entries);
    return count;
}
static bool parent_dir(char *dir) {
    if(!strcmp(dir,"/"))return false;
    char *slash=strrchr(dir,'/');
    if(!slash)return false;
    if(slash==dir)dir[1]='\0';
    else *slash='\0';
    return true;
}
static bool canonical_dir(const char *candidate,char *out,size_t cap) {
    char canonical[PATH_MAX];
    if(!realpath(candidate,canonical)||!is_dir(canonical))return false;
    if(strlen(canonical)>=cap)return false;
    safe_copy(out,cap,canonical);
    return true;
}
static void remember(PpaBrowserState *st,PpaBrowserMode mode,const char *dir) {
    char *target=((mode==PPA_BROWSER_MIDI||mode==PPA_BROWSER_OPEN)?st->last_midi:
                  mode==PPA_BROWSER_PROJECT?st->last_project:st->last_export);
    safe_copy(target,PATH_MAX,dir);
}
static void store_state(const PpaBrowserState *st) {
    char file[PATH_MAX];
    if(!state_path(file,sizeof(file),true))return;
    char temporary[PATH_MAX];
    if(snprintf(temporary,sizeof(temporary),"%s.tmp",file)>=(int)sizeof(temporary))return;
    FILE *out=fopen(temporary,"w");
    if(!out)return;
    fprintf(out,"PPA-MIDI-BROWSER-1\nmidi=%s\nproject=%s\nasm=%s\n",
            st->last_midi,st->last_project,st->last_export);
    for(int i=0;i<st->favorite_count;i++)
        fprintf(out,"favorite=%s\n",st->favorites[i]);
    if(fclose(out)==0) {
        if(rename(temporary,file)!=0)unlink(temporary);
    } else unlink(temporary);
}
static bool clean_path(char *value) {
    size_t n=strlen(value);
    if(n&&value[n-1]=='\n')value[--n]='\0';
    if(!n||value[0]!='/'||strchr(value,'\r')||strchr(value,'\n'))return false;
    return is_dir(value);
}
static void add_favorite(PpaBrowserState *st,const char *folder) {
    for(int i=0;i<st->favorite_count;i++)
        if(!strcmp(st->favorites[i],folder))return;
    if(st->favorite_count>=MAX_FAVORITES)return;
    safe_copy(st->favorites[st->favorite_count++],PATH_MAX,folder);
}
static void toggle_favorite(PpaBrowserState *st,const char *folder) {
    for(int i=0;i<st->favorite_count;i++) {
        if(strcmp(st->favorites[i],folder))continue;
        for(int j=i+1;j<st->favorite_count;j++)
            safe_copy(st->favorites[j-1],PATH_MAX,st->favorites[j]);
        st->favorite_count--;return;
    }
    add_favorite(st,folder);
}
void ppa_browser_init(PpaBrowserState *st) {
    memset(st,0,sizeof(*st));
    const char *home=getenv("HOME");
    char songs[PATH_MAX]="";
    if(home&&join(songs,sizeof(songs),home,"PPA-Songs")) {
        if(mkdir(songs,0755)!=0&&errno!=EEXIST) songs[0]=0;
    }
    if(home) {
        safe_copy(st->last_midi,PATH_MAX,home);
        safe_copy(st->last_project,PATH_MAX,is_dir(songs)?songs:home);
        safe_copy(st->last_export,PATH_MAX,is_dir(songs)?songs:home);
        if(is_dir(home))add_favorite(st,home);
    }
    if(is_dir(songs))add_favorite(st,songs);
    char path[PATH_MAX];
    if(!state_path(path,sizeof(path),false))return;
    FILE *in=fopen(path,"r");
    if(!in)return;
    char line[PATH_MAX+30];
    if(!fgets(line,sizeof(line),in)||strcmp(line,"PPA-MIDI-BROWSER-1\n")) {
        fclose(in);return;
    }
    /* Favorites saved on disk replace defaults when present. */
    int saved_favs=0;
    while(fgets(line,sizeof(line),in)) {
        char *eq=strchr(line,'=');
        if(!eq)continue;
        *eq++='\0';
        if(!clean_path(eq))continue;
        if(!strcmp(line,"midi"))safe_copy(st->last_midi,PATH_MAX,eq);
        else if(!strcmp(line,"project"))safe_copy(st->last_project,PATH_MAX,eq);
        else if(!strcmp(line,"asm"))safe_copy(st->last_export,PATH_MAX,eq);
        else if(!strcmp(line,"favorite")) {
            if(!saved_favs){st->favorite_count=0;saved_favs=1;}
            add_favorite(st,eq);
        }
    }
    fclose(in);
}
const char *ppa_browser_last(const PpaBrowserState *st,PpaBrowserMode mode) {
    return (mode==PPA_BROWSER_MIDI||mode==PPA_BROWSER_OPEN)?st->last_midi:
           mode==PPA_BROWSER_PROJECT?st->last_project:st->last_export;
}
static void edit_name(WINDOW *win,int width,int row,char *name,size_t cap) {
    echo();curs_set(1);
    mvwprintw(win,row,3,"File: %-*s",width-12,"");
    wmove(win,row,9);wrefresh(win);
    char entered[NAME_MAX+8]="";
    wgetnstr(win,entered,(int)sizeof(entered)-1);
    noecho();curs_set(0);
    if(*entered&&!strchr(entered,'/')&&strcmp(entered,".")&&
       strcmp(entered,".."))safe_copy(name,cap,entered);
}
bool ppa_browser_show(PpaBrowserState *st,PpaBrowserMode mode,
                      const char *suggested_name,char *result,size_t result_len) {
    char dir[PATH_MAX];
    safe_copy(dir,sizeof(dir),ppa_browser_last(st,mode));
    if(!is_dir(dir)) {
        const char *home=getenv("HOME");
        if(!home||!canonical_dir(home,dir,sizeof(dir)))return false;
    }
    char name[NAME_MAX+8]="";
    safe_copy(name,sizeof(name),suggested_name);
    int cursor=0,top=0;
    BrowserEntry *entries=calloc(MAX_ENTRIES,sizeof(*entries));
    if(!entries)return false;
    for(;;) {
        int height=LINES>24?LINES-6:LINES-2;
        int width=COLS>100?COLS-12:COLS-2;
        if(height<13||width<62){free(entries);return false;}
        WINDOW *w=newwin(height,width,(LINES-height)/2,(COLS-width)/2);
        if(!w){free(entries);return false;}
        keypad(w,TRUE);
        size_t count=list_entries(dir,mode,entries);
        if(cursor>=(int)count)cursor=count?(int)count-1:0;
        if(cursor<top)top=cursor;
        int visible=height-10;
        if(cursor>=top+visible)top=cursor-visible+1;
        werase(w);wbkgd(w,COLOR_PAIR(6));box(w,0,0);
        wattron(w,COLOR_PAIR(3)|A_BOLD);
        mvwprintw(w,0,3," %s ",
                  mode==PPA_BROWSER_OPEN?"OPEN MIDI / PPASP":
                  mode==PPA_BROWSER_MIDI?"OPEN MIDI":
                  mode==PPA_BROWSER_PROJECT?"OPEN PPA SOUND PROJECT":"SAVE ASM AS");
        wattroff(w,COLOR_PAIR(3)|A_BOLD);
        mvwprintw(w,2,2,"Path: %.*s",width-10,dir);
        if(mode==PPA_BROWSER_ASM_SAVE)
            mvwprintw(w,3,2,"File: %.*s",width-10,name);
        else mvwprintw(w,3,2,"Choose a %s file",
                       mode==PPA_BROWSER_OPEN?"MIDI or PPASP":
                       mode==PPA_BROWSER_MIDI?"MIDI":"PPASP");
        int sidebar=24;
        wattron(w,COLOR_PAIR(2)|A_BOLD);
        mvwaddstr(w,5,2,"FAVORITES");
        wattroff(w,COLOR_PAIR(2)|A_BOLD);
        for(int i=0;i<st->favorite_count&&i<MAX_FAVORITES;i++) {
            const char *label=strrchr(st->favorites[i],'/');
            label=label&&label[1]?label+1:st->favorites[i];
            mvwprintw(w,6+i,2,"%d %-*.*s",i+1,sidebar-5,sidebar-5,label);
        }
        wattron(w,COLOR_PAIR(2)|A_BOLD);
        mvwprintw(w,5,sidebar+3,"FILES / FOLDERS (%zu)",count);
        wattroff(w,COLOR_PAIR(2)|A_BOLD);
        for(int i=0;i<visible&&top+i<(int)count;i++) {
            int index=top+i;
            if(index==cursor)wattron(w,A_REVERSE);
            mvwprintw(w,6+i,sidebar+3,"%-3s %-*.*s",
                      entries[index].dir?"DIR":
                      (ppa_browser_accepts(PPA_BROWSER_PROJECT,entries[index].name)?"PPASP":
                       ppa_browser_accepts(PPA_BROWSER_MIDI,entries[index].name)?"MIDI":"ASM"),
                      width-sidebar-12,width-sidebar-12,entries[index].name);
            if(index==cursor)wattroff(w,A_REVERSE);
        }
        mvwaddnstr(w,height-3,2,
                   "1-8 favorite  B bookmark  H home  P PPA-Songs  R recent  Backspace up",
                   width-4);
        mvwaddnstr(w,height-2,2,
                   mode==PPA_BROWSER_ASM_SAVE?
                   "F2 rename  S save  Enter open/select  Esc cancel":
                   "Enter open/select  Up/Down move  Esc cancel",
                   width-4);
        wrefresh(w);
        int key=wgetch(w);
        if(key==27){delwin(w);free(entries);touchwin(stdscr);return false;}
        char next[PATH_MAX];
        bool navigate=false;
        if(key==KEY_UP&&cursor>0)cursor--;
        else if(key==KEY_DOWN&&cursor+1<(int)count)cursor++;
        else if(key>='1'&&key<='8') {
            int idx=key-'1';
            if(idx<st->favorite_count&&is_dir(st->favorites[idx])) {
                safe_copy(dir,sizeof(dir),st->favorites[idx]);navigate=true;
            }
        } else if(key=='b'||key=='B') {
            toggle_favorite(st,dir);store_state(st);
        } else if(key=='h'||key=='H') {
            const char *home=getenv("HOME");
            if(home&&is_dir(home)){safe_copy(dir,sizeof(dir),home);navigate=true;}
        } else if(key=='p'||key=='P') {
            const char *home=getenv("HOME");
            if(home&&join(next,sizeof(next),home,"PPA-Songs")&&is_dir(next)) {
                safe_copy(dir,sizeof(dir),next);navigate=true;
            }
        } else if(key=='r'||key=='R') {
            const char *last=ppa_browser_last(st,mode);
            if(is_dir(last)){safe_copy(dir,sizeof(dir),last);navigate=true;}
        } else if(key==KEY_BACKSPACE||key==127||key==8) {
            navigate=parent_dir(dir);
        } else if(mode==PPA_BROWSER_ASM_SAVE&&key==KEY_F(2)) {
            edit_name(w,width,3,name,sizeof(name));
        } else if((key=='\n'||key==KEY_ENTER||key=='s'||key=='S')&&
                  (count||mode==PPA_BROWSER_ASM_SAVE)) {
            bool select_current=key=='\n'||key==KEY_ENTER;
            if(select_current&&count&&entries[cursor].dir) {
                if(join(next,sizeof(next),dir,entries[cursor].name)) {
                    safe_copy(dir,sizeof(dir),next);navigate=true;
                }
            } else if(mode==PPA_BROWSER_ASM_SAVE) {
                if(select_current&&count&&!entries[cursor].dir)
                    safe_copy(name,sizeof(name),entries[cursor].name);
                size_t n=strlen(name);
                if(n&&!strchr(name,'/')&&strcmp(name,".")&&strcmp(name,"..")) {
                    if(n<4||strcasecmp(name+n-4,".asm")) {
                        if(n+4<sizeof(name))strcat(name,".asm");
                        else name[0]=0;
                    }
                    if(*name&&join(next,sizeof(next),dir,name)&&
                       strlen(next)<result_len) {
                        struct stat sb;
                        bool exists=stat(next,&sb)==0;
                        if(!exists||S_ISREG(sb.st_mode)) {
                            bool confirmed=true;
                            if(exists) {
                                mvwaddnstr(w,height-2,2,
                                   "File exists. Overwrite? (y/N)",width-4);
                                wclrtoeol(w);wrefresh(w);
                                int response=wgetch(w);
                                confirmed=response=='y'||response=='Y';
                            }
                            if(confirmed) {
                                safe_copy(result,result_len,next);
                                remember(st,mode,dir);store_state(st);
                                delwin(w);free(entries);touchwin(stdscr);return true;
                            }
                        }
                    }
                }
            } else if(select_current&&count&&!entries[cursor].dir) {
                if(join(next,sizeof(next),dir,entries[cursor].name)&&
                   regular_file(next)&&strlen(next)<result_len) {
                    safe_copy(result,result_len,next);
                    remember(st,mode,dir);store_state(st);
                    delwin(w);free(entries);touchwin(stdscr);return true;
                }
            }
        }
        if(navigate){cursor=0;top=0;remember(st,mode,dir);store_state(st);}
        delwin(w);
    }
}
