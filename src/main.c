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
	int screen=0, sel=0, action=0, slot=0, color_index=0, edit=0, component=0, dirty=0;
	float eh=0, es=0, ev=0;
	char current[PSPWAVE_THEME_NAME]={0}, namebuf[PSPWAVE_THEME_NAME]={0};
	int key_row=0, key_col=0, caps=0, about_return=0;
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
					color_index=0;
					dirty=0;
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
		else if(screen==5)
		{
			if(press&(PSP_CTRL_CIRCLE|PSP_CTRL_CROSS|PSP_CTRL_SELECT)) screen=about_return;
		}
		else if(screen==1)
		{
			if(!edit)
			{
				if(nav&PSP_CTRL_UP) slot=(slot+PSPWAVE_SLOTS-1)%PSPWAVE_SLOTS;
				if(nav&PSP_CTRL_DOWN) slot=(slot+1)%PSPWAVE_SLOTS;
				if(nav&PSP_CTRL_LEFT) color_index=(color_index+2)%3;
				if(nav&PSP_CTRL_RIGHT) color_index=(color_index+1)%3;
				if(press&PSP_CTRL_CROSS)
				{
					if(color_index>=cfg.slot[slot].count) cfg.slot[slot].count=color_index+1;
					edit=1;
					component=0;
					to_hsv(cfg.slot[slot].color[color_index],&eh,&es,&ev);
				}
				if(press&PSP_CTRL_SQUARE)
				{
					cfg.slot[slot].count=cfg.slot[slot].count%3+1;
					if(color_index>=cfg.slot[slot].count) color_index=cfg.slot[slot].count-1;
					dirty=1;
				}
				if(press&PSP_CTRL_TRIANGLE)
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
					cfg.slot[slot].color[color_index]=from_hsv(eh,es,ev);
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
		else if(screen==5)
		{
			render_text(18,58,2,0xffffffff,"ABOUT");
			render_text(18,92,3,0xffffffff,"PSPWAVE");
			render_text(18,124,1,0xffd8dbe3,"VERSION: 1.0.0");
			render_text(18,143,1,0xffd8dbe3,"AUTHOR:  MISS VIOLIN MELODY");
			render_text(18,162,1,0xffd8dbe3,"HTTPS://VIOLINMELODY.NET");
			render_text(18,190,1,0xffc7c9d0,"BUILT USING PSPDEV / PSPSDK");
			render_text(18,209,1,0xffc7c9d0,"NOT AFFILIATED WITH SONY");
			render_text(18,246,1,0xff9297a2,"SELECT / CIRCLE  BACK");
		}
		else
		{
			Rgb ac=cfg.slot[slot].color[color_index];
			uint32_t accent=render_rgb(ac.r,ac.g,ac.b);
			render_rect(0,40,480,2,accent);
			sprintf(b,"%s  %s",current,is_active(current,active)?"ACTIVE":"NOT ACTIVE");
			render_text(18,53,1,0xffd8dbe3,b);
			sprintf(b,"WAVE %02d / %02d",slot+1,PSPWAVE_SLOTS);
			render_text(18,70,2,0xffffffff,b);
			/* live preview uses the exact same gradient sampler as resource generation */
			render_rect(18,90,444,72,render_rgb(37,41,53));
			for(int py=0;py<33;py++)
				for(int px=0;px<108;px++)
				{
					Rgb pc=wavegen_preview_sample(&cfg.slot[slot],px/107.0f,py/32.0f);
					render_rect(24+px*4,93+py*2,4,2,render_rgb(pc.r,pc.g,pc.b));
				}
			
			for(int k=0;k<3;k++)
			{
				int x=18+k*146;
				Rgb c=cfg.slot[slot].color[k];
				render_rect(x,166,132,17,render_rgb(37,41,53));
				render_rect(x+4,169,22,11,render_rgb(c.r,c.g,c.b));
				if(k==color_index) render_rect(x,163,132,2,accent);
				sprintf(b,"C%d%s",k+1,k<cfg.slot[slot].count?"":" OFF");
				render_text(x+31,171,1,0xffffffff,b);
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
				sprintf(b,"GRADIENT  %s",config_gradient_name(cfg.slot[slot].gradient));
				render_text(18,190,1,0xffffffff,b);
				render_text(18,224,1,0xffc7c9d0,"UP/DOWN WAVE  LEFT/RIGHT COLOR  X EDIT  SQUARE COLORS");
				render_text(18,241,1,0xffc7c9d0,"TRIANGLE GRADIENT  START SAVE  CIRCLE THEMES");
				render_text(18,258,1,dirty?accent:0xff9297a2,dirty?"UNSAVED CHANGES":"SAVED");
			}
		}
		
		render_end();
	}
	
	sceKernelExitGame();
	return 0;
}
