#include <pspkernel.h>
#include <pspctrl.h>
#include <pspiofilemgr.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "config.h"
#include "themes.h"
#include "wavegen.h"
#include "render.h"
#include "image.h"

PSP_MODULE_INFO("PSPWave",0,1,0); PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER|THREAD_ATTR_VFPU);

#define NAV_MASK (PSP_CTRL_UP|PSP_CTRL_DOWN|PSP_CTRL_LEFT|PSP_CTRL_RIGHT)
#define REPEAT_DELAY 18
#define REPEAT_RATE 4
#define ANALOG_LOW 72
#define ANALOG_HIGH 184

typedef struct
{
	unsigned int held, pressed, repeated, previous;
	int ru, rd, rl, rr;
} InputState;

typedef enum
{
	KEY_CHAR,
	KEY_CAPS,
	KEY_BACKSPACE,
	KEY_CANCEL,
	KEY_OK
} KeyKind;

typedef struct
{
	KeyKind kind;
	char ch;
} KeyAction;

static const char*keyboard_rows[] = {"qwertyuiop", "asdfghjkl", "zxcvbnm", "1234567890",".,*\"\':;!?", "@#$_&-+()/"};
static const int keyboard_widths[] = {10, 9, 7, 10, 10, 10, 4};
#define KEYBOARD_ROWS 7

static void keyboard_move(int*row,int*col,int dr,int dc)
{
	int nr=*row+dr;
	if(nr<0) nr=KEYBOARD_ROWS-1;
	if(nr>=KEYBOARD_ROWS) nr=0;

	*row=nr;
	if(*col>=keyboard_widths[*row]) *col=keyboard_widths[*row]-1;
	
	if(dc)
	{
		*col+=dc;
		if(*col<0) *col=keyboard_widths[*row]-1;
		if(*col>=keyboard_widths[*row]) *col=0;
	}
}

static KeyAction keyboard_action(int row,int col,int caps)
{
	KeyAction a = {KEY_CHAR, 0};
	if(row<6)
	{
		a.ch=keyboard_rows[row][col];
		if(caps && a.ch>='a' && a.ch<='z') a.ch=(char)(a.ch-'a'+'A');
		return a;
	}

	switch(col)
	{
		case 0:
			a.kind=KEY_CAPS;
			break;
		case 1:
			a.kind=KEY_BACKSPACE;
			break;
		case 2:
			a.kind=KEY_CANCEL;
			break;
		default:
			a.kind=KEY_OK;
			break;
	}

	return a;
}

static void key_box(int x,int y,int w,const char*label,int selected,uint32_t accent)
{
	render_rect(x,y,w,15,selected?accent:render_rgb(37,41,53));
	render_text(x+4,y+4,1,selected?render_rgb(16,18,26):0xffffffff,label);
}

static void render_keyboard(int row,int col,int caps)
{
	static const int starts[6]={72,86,114,72,72,72};
	char one[2]={0};
	uint32_t accent=render_rgb(75,110,180);
	for(int r=0;r<6;r++)
		for(int c=0;c<keyboard_widths[r];c++)
		{
			int x=starts[r]+c*34, y=102+r*19;
			char ch=keyboard_rows[r][c];
			if(caps && ch>='a' && ch<='z') ch=(char)(ch-'a'+'A');
			render_rect(x,y,28,15,(r==row && c==col)?accent:render_rgb(37,41,53));
			one[0]=ch;
			render_text(x+11,y+4,1,(r==row && c==col)?render_rgb(16,18,26):0xffffffff,one);
		}

	int y=220;
	key_box(72,y,46,caps?"v":"^",row==6 && col==0,accent);
	key_box(124,y,76,"BACKSPACE",row==6 && col==1,accent);
	key_box(206,y,62,"CANCEL",row==6 && col==2,accent);
	key_box(274,y,46,"OK",row==6 && col==3,accent);
}


typedef struct
{
	char name[128];
	int directory;
} PickerEntry;

#define PICKER_MAX 128
#define IMAGE_PREVIEW_WIDTH 108
#define IMAGE_PREVIEW_HEIGHT 33

static PickerEntry picker[PICKER_MAX];
static int picker_count=0, picker_sel=0;
static char picker_path[256]="ms0:/PICTURE";
static unsigned char image_preview[IMAGE_PREVIEW_WIDTH*IMAGE_PREVIEW_HEIGHT*3];
static int image_preview_ready=0;

static int has_bmp_extension(const char*name)
{
	int n=strlen(name);
	if(n<4) return 0;
	const char*e=name+n-4;
	return (e[0]=='.' && (e[1]=='b'||e[1]=='B') && (e[2]=='m'||e[2]=='M') && (e[3]=='p'||e[3]=='P'));
}

static void picker_join(char*out,int size,const char*dir,const char*name)
{
	int n=strlen(dir);
	snprintf(out,size,n>0 && dir[n-1]=='/'?"%s%s":"%s/%s",dir,name);
}

static void picker_parent(void)
{
	char*p;
	int n=strlen(picker_path);
	while(n>4 && picker_path[n-1]=='/') picker_path[--n]=0;
	p=strrchr(picker_path,'/');
	if(p && p>picker_path+3) *p=0;
	else strcpy(picker_path,"ms0:/");
}

static int picker_scan(void)
{
	SceUID d;
	SceIoDirent ent;
	picker_count=0;
	picker_sel=0;
	d=sceIoDopen(picker_path);
	if(d<0)
	{
		strcpy(picker_path,"ms0:/");
		d=sceIoDopen(picker_path);
	}
	if(d<0) return d;
	memset(&ent,0,sizeof(ent));
	while(picker_count<PICKER_MAX && sceIoDread(d,&ent)>0)
	{
		if(strcmp(ent.d_name,".") && strcmp(ent.d_name,".."))
		{
			int directory=FIO_S_ISDIR(ent.d_stat.st_mode);
			if(directory || has_bmp_extension(ent.d_name))
			{
				snprintf(picker[picker_count].name,sizeof(picker[picker_count].name),"%s",ent.d_name);
				picker[picker_count].directory=directory;
				picker_count++;
			}
		}
		memset(&ent,0,sizeof(ent));
	}
	sceIoDclose(d);
	return 0;
}

static int load_image_preview(const char*path)
{
	Image image;
	memset(&image,0,sizeof(image));
	image_preview_ready=0;
	if(image_load(path,&image)<0) return -1;
	if(image_resize_cover(&image,image_preview,IMAGE_PREVIEW_WIDTH,IMAGE_PREVIEW_HEIGHT)<0)
	{
		image_free(&image);
		return -1;
	}
	image_free(&image);
	image_preview_ready=1;
	return 0;
}

static int running = 1;
static int exit_cb(int a,int b,void*c)
{
	(void)a;
	(void)b;
	(void)c;
	running=0;

	return 0;
}

static int callback_thread(SceSize a,void*b)
{
	(void)a;
	(void)b;
	int cb=sceKernelCreateCallback("exit",exit_cb,NULL);
	sceKernelRegisterExitCallback(cb);
	sceKernelSleepThreadCB();

	return 0;
}

static void setup_callbacks(void)
{
	int t=sceKernelCreateThread("callbacks",callback_thread,0x11,0x1000,0,NULL);
	if(t>=0) sceKernelStartThread(t,0,NULL);
}

static unsigned int analog_buttons(const SceCtrlData*p)
{
	unsigned int b=0;
	if(p->Lx<ANALOG_LOW) b|=PSP_CTRL_LEFT;
	else if(p->Lx>ANALOG_HIGH) b|=PSP_CTRL_RIGHT;
	if(p->Ly<ANALOG_LOW) b|=PSP_CTRL_UP;
	else if(p->Ly>ANALOG_HIGH) b|=PSP_CTRL_DOWN;

	return b;
}

static int repeat_button(unsigned int h,unsigned int p,unsigned int b,int*c)
{
	if(!(h&b))
	{
		*c=0;
		return 0;
	}
	if(p&b)
	{
		*c=1;
		return 1;
	}

	(*c)++;
	return *c>REPEAT_DELAY && ((*c-REPEAT_DELAY)%REPEAT_RATE)==0;
}

static void input_update(InputState*i)
{
	SceCtrlData p;
	sceCtrlPeekBufferPositive(&p,1);
	unsigned int h=p.Buttons|analog_buttons(&p),pr=h&~i->previous;
	i->held=h;
	i->pressed=pr;
	i->repeated=pr&~NAV_MASK;
	if(repeat_button(h,pr,PSP_CTRL_UP,&i->ru)) i->repeated|=PSP_CTRL_UP;
	if(repeat_button(h,pr,PSP_CTRL_DOWN,&i->rd)) i->repeated|=PSP_CTRL_DOWN;
	if(repeat_button(h,pr,PSP_CTRL_LEFT,&i->rl)) i->repeated|=PSP_CTRL_LEFT;
	if(repeat_button(h,pr,PSP_CTRL_RIGHT,&i->rr)) i->repeated|=PSP_CTRL_RIGHT;
	
	i->previous=h;
}

static void to_hsv(Rgb c,float*h,float*s,float*v)
{
	float r=c.r/255.f, g=c.g/255.f, b=c.b/255.f, mx=fmaxf(r,fmaxf(g,b)), mn=fminf(r,fminf(g,b)), d=mx-mn;
	*v=mx;
	*s=mx>0?d/mx:0;
	*h=0;
	if(d>0)
	{
		if(mx==r) *h=60*fmodf((g-b)/d,6);
		else if(mx==g) *h=60*((b-r)/d+2);
		else *h=60*((r-g)/d+4);
	}
	if(*h<0) *h+=360;
}

static Rgb from_hsv(float h,float s,float v)
{
	uint32_t c=render_hsv(h,s,v);
	Rgb r={c&255, (c>>8)&255, (c>>16)&255};
	
	return r;
}

static int is_active(const char*n,const char*a)
{
	return a[0] && strcmp(n,a)==0;
}

static void refresh(ThemeList*l,char*a)
{
	themes_scan(l);
	themes_active_name(a,PSPWAVE_THEME_NAME);
}

static void unique_name(const ThemeList*l,const char*base,char*out)
{
	int k=1;
	strncpy(out,base,PSPWAVE_THEME_NAME-1);
	out[PSPWAVE_THEME_NAME-1]=0;
	for(;;)
	{
		int hit=0;
		for(int i=0;i<l->count;i++)
			if(!strcmp(l->item[i].name,out)) hit=1;
		if(!hit) return;
		snprintf(out,PSPWAVE_THEME_NAME,"%s %d",base,++k);
	}
}

int main(void)
{
	setup_callbacks();
	sceCtrlSetSamplingCycle(0);
	sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
	render_init();
	themes_init();
	ThemeList list;
	char active[PSPWAVE_THEME_NAME]={0};
	WaveConfig cfg;
	WavegenStatus status;
	refresh(&list,active);
	int have_default=0;
	
	for(int i=0;i<list.count;i++)
		if(!strcmp(list.item[i].name,"PSPWave Default")) have_default=1;
	
	if(!have_default)
	{
		config_defaults(&cfg);
		themes_save("PSPWave Default",&cfg);
		refresh(&list,active);
	}
	
	if(active[0]==0)
	{
		for(int i=0;i<list.count;i++)
			if(!strcmp(list.item[i].name,"PSPWave Default"))
			{
				themes_load(&list.item[i],&cfg);
				themes_activate(list.item[i].name,&cfg);
				break;
			}
		refresh(&list,active);
	}
	
	wavegen_get_status(&status);
	int screen=0, sel=0, action=0, slot=0, colour_index=0, edit=0, component=0, dirty=0;
	float eh=0, es=0, ev=0;
	char current[PSPWAVE_THEME_NAME]={0}, namebuf[PSPWAVE_THEME_NAME]={0};
	int key_row=0, key_col=0, caps=0, about_return=0, picker_return=1;
	InputState in={0};
	while(running)
	{
		input_update(&in);
		unsigned int nav=in.repeated, press=in.pressed;
		int opened_about=0;
		
		if((press&PSP_CTRL_SELECT) && screen!=5)
		{
			about_return=screen;
			screen=5;
			opened_about=1;
		}
		
		if(opened_about)
		{
			/* consume SELECT press that opened About so it cannot close it in the same frame */
		}
		else if(screen==0)
		{
			if(list.count)
			{
				if(nav&PSP_CTRL_UP) sel=(sel+list.count-1)%list.count;
				if(nav&PSP_CTRL_DOWN) sel=(sel+1)%list.count;
			}
			
			if((press&PSP_CTRL_CROSS) && list.count)
			{
				if(themes_load(&list.item[sel],&cfg)==0)
				{
					strcpy(current,list.item[sel].name);
					slot=0;
					colour_index=0;
					dirty=0;
					image_preview_ready=cfg.slot[0].mode==WAVE_MODE_IMAGE && load_image_preview(cfg.slot[0].image_path)==0;
					screen=1;
				}
			}
			
			if((press&PSP_CTRL_START) && list.count)
			{
				if(themes_load(&list.item[sel],&cfg)==0 && themes_activate(list.item[sel].name,&cfg)==0) refresh(&list,active);
			}
			
			if(press&PSP_CTRL_TRIANGLE)
			{
				action=0;
				screen=2;
			}
			
			if(press&PSP_CTRL_CIRCLE) running=0;
		}
		else if(screen==2)
		{
			if(nav&PSP_CTRL_UP) action=(action+4)%5;
			if(nav&PSP_CTRL_DOWN) action=(action+1)%5;
			if(press&PSP_CTRL_CIRCLE) screen=0;
			if(press&PSP_CTRL_CROSS)
			{
				if(action==4) screen=0;
				else if(action==3 && list.count)
				{
					if(!is_active(list.item[sel].name,active))
					{
						themes_delete(&list.item[sel]);
						refresh(&list,active);
						if(sel>=list.count) sel=list.count?list.count-1:0;
					}
					screen=0;
				}
				else if(action==0)
				{
					config_defaults(&cfg);
					unique_name(&list,"New Theme",namebuf);
					key_row=0;
					key_col=0;
					caps=0;
					screen=3;
				}
				else if(action==1 && list.count)
				{
					themes_load(&list.item[sel],&cfg);
					unique_name(&list,"Theme Copy",namebuf);
					key_row=0;
					key_col=0;
					caps=0;
					screen=3;
				}
				else if(action==2 && list.count)
				{
					themes_load(&list.item[sel],&cfg);
					strncpy(namebuf,list.item[sel].name,sizeof(namebuf)-1);
					namebuf[sizeof(namebuf)-1]=0;
					key_row=0;
					key_col=0;
					caps=0;
					screen=4;
				}
			}
		}
		else if(screen==3||screen==4)
		{
			if(nav&PSP_CTRL_UP) keyboard_move(&key_row,&key_col,-1,0);
			if(nav&PSP_CTRL_DOWN) keyboard_move(&key_row,&key_col,1,0);
			if(nav&PSP_CTRL_LEFT) keyboard_move(&key_row,&key_col,0,-1);
			if(nav&PSP_CTRL_RIGHT) keyboard_move(&key_row,&key_col,0,1);
			if(press&PSP_CTRL_CROSS)
			{
				KeyAction a=keyboard_action(key_row,key_col,caps);
				if(a.kind==KEY_CHAR)
				{
					int n=strlen(namebuf);
					if(n<PSPWAVE_THEME_NAME-1 && a.ch!='/')
					{
						namebuf[n]=a.ch;
						namebuf[n+1]=0;
					}
				}
				else if(a.kind==KEY_CAPS) caps=!caps;
				else if(a.kind==KEY_BACKSPACE)
				{
					int n=strlen(namebuf);
					if(n)namebuf[n-1]=0;
				}
				else if(a.kind==KEY_CANCEL) screen=2;
				else if(a.kind==KEY_OK)
				{
					char safe[PSPWAVE_THEME_NAME];
					if(themes_sanitize_name(namebuf,safe,sizeof(safe))==0)
					{
						if(screen==4 && strcmp(safe,list.item[sel].name))
						{
							int was=is_active(list.item[sel].name,active);
							themes_delete(&list.item[sel]);
							themes_save(safe,&cfg);
							if(was) themes_activate(safe,&cfg);
						}
						else themes_save(safe,&cfg);
						refresh(&list,active);
						for(int i=0;i<list.count;i++)
							if(!strcmp(list.item[i].name,safe)) sel=i;
						screen=0;
					}
				}
			}
		}
		else if(screen==6)
		{
			if(picker_count)
			{
				if(nav&PSP_CTRL_UP) picker_sel=(picker_sel+picker_count-1)%picker_count;
				if(nav&PSP_CTRL_DOWN) picker_sel=(picker_sel+1)%picker_count;
			}
			if(press&PSP_CTRL_CIRCLE)
			{
				if(strcmp(picker_path,"ms0:/"))
				{
					picker_parent();
					picker_scan();
				}
				else screen=picker_return;
			}
			if((press&PSP_CTRL_CROSS) && picker_count)
			{
				char selected[256];
				picker_join(selected,sizeof(selected),picker_path,picker[picker_sel].name);
				if(picker[picker_sel].directory)
				{
					strncpy(picker_path,selected,sizeof(picker_path)-1);
					picker_path[sizeof(picker_path)-1]=0;
					picker_scan();
				}
				else if(load_image_preview(selected)==0)
				{
					cfg.slot[slot].mode=WAVE_MODE_IMAGE;
					snprintf(cfg.slot[slot].image_path,sizeof(cfg.slot[slot].image_path),"%s",selected);
					dirty=1;
					screen=picker_return;
				}
			}
		}
		else if(screen==5)
		{
			if(press&(PSP_CTRL_CIRCLE|PSP_CTRL_CROSS|PSP_CTRL_SELECT)) screen=about_return;
		}
		else if(screen==1)
		{
			if(!edit)
			{
				if(nav&PSP_CTRL_UP)
				{
					slot=(slot+PSPWAVE_SLOTS-1)%PSPWAVE_SLOTS;
					image_preview_ready=cfg.slot[slot].mode==WAVE_MODE_IMAGE && load_image_preview(cfg.slot[slot].image_path)==0;
				}
				if(nav&PSP_CTRL_DOWN)
				{
					slot=(slot+1)%PSPWAVE_SLOTS;
					image_preview_ready=cfg.slot[slot].mode==WAVE_MODE_IMAGE && load_image_preview(cfg.slot[slot].image_path)==0;
				}
				if(nav&PSP_CTRL_LEFT) colour_index=(colour_index+3)%4;
				if(nav&PSP_CTRL_RIGHT) colour_index=(colour_index+1)%4;
				if(press&PSP_CTRL_RTRIGGER)
				{
					if(cfg.slot[slot].mode==WAVE_MODE_IMAGE)
					{
						cfg.slot[slot].mode=WAVE_MODE_GRADIENT;
						image_preview_ready=0;
					}
					else
					{
						if(cfg.slot[slot].image_path[0] && load_image_preview(cfg.slot[slot].image_path)==0)
						{
							cfg.slot[slot].mode=WAVE_MODE_IMAGE;
							image_preview_ready=1;
							dirty=1;
						}
						else
						{
							picker_return=1;
							picker_scan();
							screen=6;
						}
					}
				}
				if(press&PSP_CTRL_CROSS)
				{
					if(colour_index==3)
					{
						edit=1;
						component=0;
						to_hsv(cfg.slot[slot].menu_colour,&eh,&es,&ev);
					}
					else if(cfg.slot[slot].mode==WAVE_MODE_IMAGE)
					{
						picker_return=1;
						picker_scan();
						screen=6;
					}
					else
					{
						if(colour_index>=cfg.slot[slot].count) cfg.slot[slot].count=colour_index+1;
						edit=1;
						component=0;
						to_hsv(cfg.slot[slot].colour[colour_index],&eh,&es,&ev);
					}
				}
				if((press&PSP_CTRL_SQUARE) && cfg.slot[slot].mode==WAVE_MODE_GRADIENT)
				{
					cfg.slot[slot].count=cfg.slot[slot].count%3+1;
					if(colour_index<3 && colour_index>=cfg.slot[slot].count) colour_index=cfg.slot[slot].count-1;
					dirty=1;
				}
				if((press&PSP_CTRL_TRIANGLE) && cfg.slot[slot].mode==WAVE_MODE_GRADIENT)
				{
					cfg.slot[slot].gradient=(GradientMode)((cfg.slot[slot].gradient+1)%GRADIENT_MODE_COUNT);
					dirty=1;
				}
				if(press&PSP_CTRL_START)
				{
					if(themes_save(current,&cfg)==0)
					{
						if(is_active(current,active)) themes_activate(current,&cfg);
						dirty=0;
					}
				}
				if(press&PSP_CTRL_CIRCLE)
				{
					screen=0;
					refresh(&list,active);
				}
			}
			else
			{
				if(nav&PSP_CTRL_UP) component=(component+2)%3;
				if(nav&PSP_CTRL_DOWN) component=(component+1)%3;
				if(nav&(PSP_CTRL_LEFT|PSP_CTRL_RIGHT))
				{
					float d=(nav&PSP_CTRL_LEFT)?-1:1;
					if(component==0)
					{
						eh+=d*2;
						if(eh<0) eh+=360;
						if(eh>=360) eh-=360;
					}
					else if(component==1)
					{
						es+=d*.02f;
						if(es<0) es=0;
						if(es>1) es=1;
					}
					else
					{
						ev+=d*.02f;
						if(ev<.08f) ev=.08f;
						if(ev>1)ev=1;
					}
					if(colour_index==3) cfg.slot[slot].menu_colour=from_hsv(eh,es,ev);
					else cfg.slot[slot].colour[colour_index]=from_hsv(eh,es,ev);
					dirty=1;
				}
				if(press&(PSP_CTRL_CROSS|PSP_CTRL_CIRCLE)) edit=0;
			}
		}
		
		/* render */
		render_begin(render_rgb(16,18,26));
		render_rect(0,0,480,42,render_rgb(25,28,39));
		render_text(18,12,3,0xffffffff,"PSPWAVE");char b[96];
		
		if(screen==0)
		{
			render_text(18,55,2,0xffffffff,"THEMES");
			int first=sel>=8?sel-7:0;
			for(int i=0;i<8 && first+i<list.count;i++)
			{
				int idx=first+i;
				int y=84+i*20;
				uint32_t c=idx==sel?render_rgb(75,110,180):render_rgb(34,38,49);
				render_rect(18,y-3,444,18,c);
				render_text(25,y,1,0xffffffff,list.item[idx].name);
				if(is_active(list.item[idx].name,active)) render_text(365,y,1,0xffffffff,"ACTIVE");
			}
			
			render_text(18,250,1,0xffc7c9d0,"X EDIT   START ACTIVATE   TRIANGLE OPTIONS   SELECT ABOUT");
		}
		else if(screen==2)
		{
			const char*ops[]={"NEW THEME","DUPLICATE","RENAME","DELETE","BACK"};
			render_text(18,58,2,0xffffffff,"THEME OPTIONS");
			for(int i=0;i<5;i++)
			{
				render_rect(18,82+i*26,260,21,i==action?render_rgb(75,110,180):render_rgb(37,41,53));
				render_text(28,87+i*26,1,0xffffffff,ops[i]);
			}
		}
		else if(screen==3||screen==4)
		{
			render_text(18,52,2,0xffffffff,screen==3?"NAME THEME":"RENAME THEME");
			render_rect(18,74,444,22,render_rgb(37,41,53));
			render_text(24,80,1,0xffffffff,namebuf);
			render_keyboard(key_row,key_col,caps);
		}
		else if(screen==6)
		{
			render_text(18,52,2,0xffffffff,"SELECT BMP IMAGE");
			render_text(18,73,1,0xffc7c9d0,picker_path);
			int first=picker_sel>=8?picker_sel-7:0;
			for(int i=0;i<8 && first+i<picker_count;i++)
			{
				int idx=first+i;
				int y=96+i*18;
				render_rect(18,y-3,444,16,idx==picker_sel?render_rgb(75,110,180):render_rgb(34,38,49));
				render_text(24,y,1,0xffffffff,picker[idx].directory?"[DIR]":"[BMP]");
				render_text(72,y,1,0xffffffff,picker[idx].name);
			}
			render_text(18,250,1,0xffc7c9d0,"X OPEN/SELECT   CIRCLE BACK");
		}
		else if(screen==5)
		{
			render_text(18,58,2,0xffffffff,"ABOUT");
			render_text(18,92,3,0xffffffff,"PSPWAVE");
			render_text(18,124,1,0xffd8dbe3,"VERSION: 1.1.0");
			render_text(18,143,1,0xffd8dbe3,"AUTHOR:  MISS VIOLIN MELODY");
			render_text(18,162,1,0xffd8dbe3,"HTTPS://VIOLINMELODY.NET");
			render_text(18,190,1,0xffc7c9d0,"BUILT USING PSPDEV / PSPSDK");
			render_text(18,209,1,0xffc7c9d0,"NOT AFFILIATED WITH SONY");
			render_text(18,246,1,0xff9297a2,"SELECT / CIRCLE  BACK");
		}
		else
		{
			Rgb ac=colour_index==3?cfg.slot[slot].menu_colour:cfg.slot[slot].colour[colour_index];
			uint32_t accent=render_rgb(ac.r,ac.g,ac.b);
			render_rect(0,40,480,2,accent);
			sprintf(b,"%s  %s",current,is_active(current,active)?"ACTIVE":"NOT ACTIVE");
			render_text(18,53,1,0xffd8dbe3,b);
			sprintf(b,"WAVE %02d / %02d",slot+1,PSPWAVE_SLOTS);
			render_text(18,70,2,0xffffffff,b);
			render_rect(18,90,444,72,render_rgb(37,41,53));
			if(cfg.slot[slot].mode==WAVE_MODE_IMAGE && image_preview_ready)
			{
				for(int py=0;py<IMAGE_PREVIEW_HEIGHT;py++)
					for(int px=0;px<IMAGE_PREVIEW_WIDTH;px++)
					{
						unsigned char*p=&image_preview[(py*IMAGE_PREVIEW_WIDTH+px)*3];
						render_rect(24+px*4,93+py*2,4,2,render_rgb(p[0],p[1],p[2]));
					}
			}
			else if(cfg.slot[slot].mode==WAVE_MODE_GRADIENT)
			{
				for(int py=0;py<33;py++)
					for(int px=0;px<108;px++)
					{
						Rgb pc=wavegen_preview_sample(&cfg.slot[slot],px/107.0f,py/32.0f);
						render_rect(24+px*4,93+py*2,4,2,render_rgb(pc.r,pc.g,pc.b));
					}
			}
			
			for(int k=0;k<4;k++)
			{
				int x=18+k*111;
				Rgb c=k==3?cfg.slot[slot].menu_colour:cfg.slot[slot].colour[k];
				render_rect(x,166,102,17,render_rgb(37,41,53));
				render_rect(x+4,169,18,11,render_rgb(c.r,c.g,c.b));
				if(k==colour_index) render_rect(x,163,102,2,accent);
				if(k==3) sprintf(b,"MENU COLOUR");
				else sprintf(b,"C%d%s",k+1,k<cfg.slot[slot].count?"":" OFF");
				render_text(x+26,171,1,0xffffffff,b);
			}
			
			if(edit)
			{
				const char*n[]={"HUE","SATURATION","BRIGHTNESS"};
				float v[]={eh/360,es,ev};
				for(int r=0;r<3;r++)
				{
					int y=190+r*18;
					render_text(18,y,1,r==component?0xffffffff:0xffaeb2bd,n[r]);
					render_rect(120,y,330,8,render_rgb(43,47,59));
					if(r==0)
						for(int x=0;x<330;x+=3) render_rect(120+x,y,3,8,render_hsv(x*360.f/330,1,1));
					else render_rect(120,y,(int)(330*v[r]),8,accent);
					int px=120+(int)(330*v[r]);
					render_rect(px-2,y-2,4,12,0xffffffff);
				}
				render_text(18,258,1,0xffc7c9d0,"UP/DOWN PROPERTY   HOLD LEFT/RIGHT   X DONE");
			}
			else
			{
				if(cfg.slot[slot].mode==WAVE_MODE_IMAGE)
				{
					render_text(18,190,1,0xffffffff,"MODE  IMAGE");
					render_text(18,207,1,0xffc7c9d0,cfg.slot[slot].image_path[0]?cfg.slot[slot].image_path:"NO BMP IMAGE SELECTED");
					render_text(18,224,1,0xffc7c9d0,"UP/DOWN WAVE  X SELECT BMP IMAGE  R MODE");
				}
				else
				{
					sprintf(b,"GRADIENT  %s",config_gradient_name(cfg.slot[slot].gradient));
					render_text(18,190,1,0xffffffff,b);
					render_text(18,224,1,0xffc7c9d0,"UP/DOWN WAVE  LEFT/RIGHT COLOUR  X EDIT  SQUARE COLOURS");
				}
				render_text(18,241,1,0xffc7c9d0,cfg.slot[slot].mode==WAVE_MODE_IMAGE?"R GRADIENT MODE  START SAVE  CIRCLE THEMES":"TRIANGLE GRADIENT  R IMAGE MODE  START SAVE");
				render_text(18,258,1,dirty?accent:0xff9297a2,dirty?"UNSAVED CHANGES":"SAVED");
			}
		}
		
		render_end();
	}
	
	sceKernelExitGame();
	return 0;
}
