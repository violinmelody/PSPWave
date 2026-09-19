#include "themes.h"
#include "wavegen.h"
#include <pspiofilemgr.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

static void make_path(const char *name,char *path,int size)
{
    snprintf(path,size,"%s/%s.txt",PSPWAVE_THEMES_DIR,name);
}

int themes_sanitize_name(const char *input,char *output,int size)
{
    int n=0;
    for(;*input&&n<size-1;++input)
    {
        unsigned char c=(unsigned char)*input;
        if(isalnum(c)||c==' '||strchr(".,*\"':;!?@#$_&-+()",c)) output[n++]=(char)c;
    }
    while(n>0&&output[n-1]==' ') --n;
    output[n]=0;
    
    return n>0?0:-1;
}

int themes_init(void)
{
    sceIoMkdir("ms0:/SEPLUGINS",0777);
    sceIoMkdir(PSPWAVE_PLUGIN_DIR,0777);
    sceIoMkdir(PSPWAVE_THEMES_DIR,0777);
    
    return 0;
}

int themes_scan(ThemeList *list)
{
    SceUID d;
    SceIoDirent ent;
    memset(list,0,sizeof(*list));
    d=sceIoDopen(PSPWAVE_THEMES_DIR);
    if(d<0) return d;
    
    memset(&ent,0,sizeof(ent));
    while(list->count<PSPWAVE_MAX_THEMES&&sceIoDread(d,&ent)>0)
    {
        int len=strlen(ent.d_name);
        if(len>4&&strcmp(ent.d_name+len-4,".txt")==0)
        {
            ThemeEntry *e=&list->item[list->count++];
            int n=len-4;
            if(n>=PSPWAVE_THEME_NAME) n=PSPWAVE_THEME_NAME-1;
            memcpy(e->name,ent.d_name,n);
            e->name[n]=0;
            snprintf(e->path,sizeof(e->path),"%s/%s",PSPWAVE_THEMES_DIR,ent.d_name);
        }
        
        memset(&ent,0,sizeof(ent));
    }
    
    sceIoDclose(d);
    return 0;
}

int themes_load(const ThemeEntry *entry,WaveConfig *cfg)
{
    config_defaults(cfg);
    return config_load(cfg,entry->path);
}

int themes_save(const char *name,const WaveConfig *cfg)
{
    char safe[PSPWAVE_THEME_NAME],path[160];
    if(themes_sanitize_name(name,safe,sizeof(safe))<0) return-1;
    
    make_path(safe,path,sizeof(path));
    return config_save(cfg,path);
}

int themes_delete(const ThemeEntry *entry)
{
    return sceIoRemove(entry->path);
}

int themes_activate(const char *name,const WaveConfig *cfg)
{
    char tmp[160], theme_path[160], safe[PSPWAVE_THEME_NAME], buf[1024], head[64];
    SceUID in, out;
    int n, r, hl;
    if(themes_sanitize_name(name,safe,sizeof(safe))<0) return-1;
    
    make_path(safe,theme_path,sizeof(theme_path));
    if(config_save(cfg,theme_path)<0) return-1;
    if(wavegen_generate(cfg,NULL)<0) return-1;
    snprintf(tmp,sizeof(tmp),"%s.tmp",PSPWAVE_CONFIG_PATH);
    
    in=sceIoOpen(theme_path,PSP_O_RDONLY,0);
    if(in<0) return in;
    
    out=sceIoOpen(tmp,PSP_O_WRONLY|PSP_O_CREAT|PSP_O_TRUNC,0777);
    if(out<0)
    {
        sceIoClose(in);
        return out;
    }
    
    hl=snprintf(head,sizeof(head),"NAME=%s\n",safe);
    if(sceIoWrite(out,head,hl)!=hl)
    {
        sceIoClose(in);
        sceIoClose(out);
        sceIoRemove(tmp);
        return-2;
    }
    while((n=sceIoRead(in,buf,sizeof(buf)))>0)
        if(sceIoWrite(out,buf,n)!=n)
        {
            sceIoClose(in);
            sceIoClose(out);
            sceIoRemove(tmp);
            return-2;
        }
    sceIoClose(in);
    sceIoClose(out);
    sceIoRemove(PSPWAVE_CONFIG_PATH);
    r=sceIoRename(tmp,PSPWAVE_CONFIG_PATH);
    return r;
}

int themes_active_name(char *name,int size)
{
    SceUID f=sceIoOpen(PSPWAVE_CONFIG_PATH,PSP_O_RDONLY,0);
    char line[64], *e;
    int n;
    if(f<0)
    {
        if(size)name[0]=0;
        return f;
    }
    
    n=sceIoRead(f,line,sizeof(line)-1);
    sceIoClose(f);
    if(n<=0) return-1;
    
    line[n]=0;
    if(strncmp(line,"NAME=",5)!=0)
    {
        if(size) name[0]=0;
        return-1;
    }
    e=strchr(line,'\n');
    if(e) *e=0;
    strncpy(name,line+5,size-1);
    name[size-1]=0;
    return 0;
}
