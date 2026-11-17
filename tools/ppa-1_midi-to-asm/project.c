#define _POSIX_C_SOURCE 200809L
#include "project.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#define PROJECT_MAGIC "PPASP1\r\n"
#define MAX_MIDI (128u * 1024u * 1024u)

static void problem(char *err,size_t cap,const char *msg) {
    if(cap)snprintf(err,cap,"%s",msg);
}
static bool w32(FILE *f,uint32_t v) {
    unsigned char b[4]={(unsigned char)v,(unsigned char)(v>>8),
                        (unsigned char)(v>>16),(unsigned char)(v>>24)};
    return fwrite(b,1,4,f)==4;
}
static bool r32(FILE *f,uint32_t *v) {
    unsigned char b[4];
    if(fread(b,1,4,f)!=4)return false;
    *v=(uint32_t)b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24);
    return true;
}
bool ppa_project_folder(char *path,size_t cap,char *err,size_t len) {
    const char *home=getenv("HOME");
    if(!home||!home[0]){problem(err,len,"HOME is not set");return false;}
    int n=snprintf(path,cap,"%s/PPA-Songs",home);
    if(n<0||(size_t)n>=cap){problem(err,len,"Project folder path too long");return false;}
    if(mkdir(path,0755)&&errno!=EEXIST){problem(err,len,"Cannot create ~/PPA-Songs");return false;}
    struct stat st;
    if(stat(path,&st)||!S_ISDIR(st.st_mode)){problem(err,len,"PPA-Songs is not a directory");return false;}
    return true;
}
static bool write_track(FILE *f,const PpaMidiTrack *t,const PpaMidiPart *p) {
    size_t len=strnlen(t->id,sizeof(t->id));
    if(len>31||!w32(f,(uint32_t)len)||fwrite(t->id,1,len,f)!=len)return false;
    if(!w32(f,t->enabled?1:0)||!w32(f,(uint32_t)t->voice)||
       !w32(f,(uint32_t)t->wave)||!w32(f,(uint32_t)t->poly)||
       !w32(f,(uint32_t)t->volume)||!w32(f,(uint32_t)(int32_t)t->transpose))return false;
    for(int i=0;i<4;i++)
        if(!w32(f,p[i].enabled?1:0)||!w32(f,(uint32_t)p[i].wave)||
           !w32(f,(uint32_t)p[i].volume)||
           !w32(f,(uint32_t)(int32_t)p[i].transpose))return false;
    return true;
}
bool ppa_project_save(const char *name,const char *midi_path,
                      const PpaMidiTrack *tracks,const PpaMidiPart *parts,
                      int count,bool optimize,int clock_hz,int tick_ms,
                      char *saved_path,size_t cap,char *err,size_t errcap) {
    if(!name||!midi_path||!tracks||!parts||count<1||count>PPA_MIDI_MAX_TRACKS)
        {problem(err,errcap,"Invalid project state");return false;}
    char folder[4096];
    if(!ppa_project_folder(folder,sizeof(folder),err,errcap))return false;
    char safe[160];size_t n=0;
    for(const unsigned char *p=(const unsigned char*)name;*p&&n<sizeof(safe)-1;p++) {
        unsigned char c=*p;
        safe[n++]=(char)(isalnum(c)||c=='_'||c=='-'?c:'_');
    }
    safe[n]=0;
    if(!n){problem(err,errcap,"Project name is empty");return false;}
    char dest[4096];
    int m=snprintf(dest,sizeof(dest),"%s/%s.ppasp",folder,safe);
    if(m<0||(size_t)m>=sizeof(dest)||strlen(dest)>=cap)
        {problem(err,errcap,"Project filename too long");return false;}
    FILE *midi=fopen(midi_path,"rb");
    if(!midi){problem(err,errcap,"Cannot open original MIDI");return false;}
    if(fseek(midi,0,SEEK_END)||ftell(midi)<0){fclose(midi);problem(err,errcap,"MIDI seek failed");return false;}
    long len=ftell(midi);
    if(len<14||(unsigned long)len>MAX_MIDI){fclose(midi);problem(err,errcap,"Invalid MIDI size");return false;}
    rewind(midi);
    char tmp[4096];
    if(snprintf(tmp,sizeof(tmp),"%s/.%s-XXXXXX",folder,safe)>=(int)sizeof(tmp))
        {fclose(midi);problem(err,errcap,"Temporary path too long");return false;}
    int fd=mkstemp(tmp);
    if(fd<0){fclose(midi);problem(err,errcap,"Cannot create PPASP temporary file");return false;}
    FILE *out=fdopen(fd,"wb");if(!out){close(fd);unlink(tmp);fclose(midi);return false;}
    bool ok=fwrite(PROJECT_MAGIC,1,8,out)==8 &&
        w32(out,1)&&w32(out,(uint32_t)len)&&w32(out,(uint32_t)count)&&
        w32(out,optimize?1u:0u)&&w32(out,(uint32_t)clock_hz)&&
        w32(out,(uint32_t)tick_ms);
    unsigned char buffer[8192];long remaining=len;
    while(ok&&remaining>0) {
        size_t chunk=remaining>(long)sizeof(buffer)?sizeof(buffer):(size_t)remaining;
        if(fread(buffer,1,chunk,midi)!=chunk||fwrite(buffer,1,chunk,out)!=chunk)ok=false;
        remaining-=(long)chunk;
    }
    fclose(midi);
    for(int i=0;i<count&&ok;i++)ok=write_track(out,&tracks[i],parts+i*4);
    if(fclose(out))ok=false;
    if(ok&&rename(tmp,dest))ok=false;
    if(!ok){unlink(tmp);problem(err,errcap,"Cannot write PPASP project");return false;}
    snprintf(saved_path,cap,"%s",dest);
    return true;
}
bool ppa_project_load(const char *path,char *temp_midi,size_t midi_cap,
                      PpaMidiTrack *tracks,PpaMidiPart *parts,int cap,
                      int *count,bool *optimize,int *clock_hz,int *tick_ms,
                      char *err,size_t errcap) {
    if(!path||!temp_midi||!tracks||!parts||!count||!optimize||!clock_hz||!tick_ms)
        {problem(err,errcap,"Invalid project load arguments");return false;}
    FILE *in=fopen(path,"rb");
    if(!in){problem(err,errcap,"Cannot open PPASP project");return false;}
    unsigned char magic[8];uint32_t version,size,n,optim,clock,tick;
    bool ok=fread(magic,1,8,in)==8&&memcmp(magic,PROJECT_MAGIC,8)==0&&
        r32(in,&version)&&r32(in,&size)&&r32(in,&n)&&r32(in,&optim)&&
        r32(in,&clock)&&r32(in,&tick);
    if(!ok||version!=1||size<14||size>MAX_MIDI||n==0||n>(unsigned)cap||
       optim>1||clock<100||clock>10000000||tick<1||tick>250)
        {fclose(in);problem(err,errcap,"Unsupported or damaged PPASP header");return false;}
    char filename[]="/tmp/ppa-ppasp-midi-XXXXXX";
    int fd=mkstemp(filename);
    if(fd<0){fclose(in);problem(err,errcap,"Cannot extract embedded MIDI");return false;}
    FILE *out=fdopen(fd,"wb");
    if(!out){close(fd);unlink(filename);fclose(in);return false;}
    unsigned char buf[8192];uint32_t left=size;
    while(left&&ok) {
        size_t chunk=left>sizeof(buf)?sizeof(buf):left;
        ok=fread(buf,1,chunk,in)==chunk&&fwrite(buf,1,chunk,out)==chunk;
        left-=(uint32_t)chunk;
    }
    if(fclose(out))ok=false;
    PpaMidiTrack *original=calloc(n,sizeof(*original));
    if(!original)ok=false;
    int loaded=0;
    if(ok&&ppa_midi_open(filename,original,(int)n,&loaded,err,errcap))
        ok=(loaded==(int)n);
    else ok=false;
    for(uint32_t i=0;i<n&&ok;i++) {
        uint32_t length,values[6];
        ok=r32(in,&length)&&length<=31;
        char id[32]={0};
        if(ok)ok=fread(id,1,length,in)==length;
        for(int j=0;j<6&&ok;j++)ok=r32(in,&values[j]);
        if(!ok||strcmp(id,original[i].id)) {ok=false;break;}
        tracks[i]=original[i];
        tracks[i].enabled=values[0]==1;
        tracks[i].voice=(int)values[1];
        tracks[i].wave=(int)values[2];
        tracks[i].poly=(int)values[3];
        tracks[i].volume=(int)values[4];
        tracks[i].transpose=(int)(int32_t)values[5];
        if(tracks[i].voice<0||tracks[i].voice>4||tracks[i].wave<0||tracks[i].wave>4||
           tracks[i].poly<0||tracks[i].poly>2||tracks[i].volume<0||
           tracks[i].volume>255||tracks[i].transpose< -48||tracks[i].transpose>48)
           {ok=false;break;}
        for(int lane=0;lane<4&&ok;lane++) {
            uint32_t v[4];
            for(int j=0;j<4;j++)if(!r32(in,&v[j]))ok=false;
            if(!ok||v[0]>1||v[1]>4||v[2]>255||(int32_t)v[3]< -48||
               (int32_t)v[3]>48){ok=false;break;}
            PpaMidiPart *p=&parts[i*4+lane];
            *p=(PpaMidiPart){v[0]==1,(int)v[1],(int)v[2],(int)(int32_t)v[3]};
        }
    }
    free(original);fclose(in);
    if(!ok||strlen(filename)>=midi_cap) {
        unlink(filename);problem(err,errcap,"PPASP damaged or incompatible with MIDI");return false;
    }
    snprintf(temp_midi,midi_cap,"%s",filename);
    *count=(int)n;*optimize=optim==1;*clock_hz=(int)clock;*tick_ms=(int)tick;
    return true;
}
