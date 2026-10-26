#include <windows.h>
#include <cstring>
#include <thread>
#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <shlobj.h>
#include <shellapi.h>
#include <commdlg.h>
#include "serial.h"
#include "vgmsrc.h"
#include "vgmchip.h"
#include "adlmidi.h"
#include <dirent.h>
#include <mmsystem.h>
#include <sysinfoapi.h>
#include <conio.h> 
#include <winuser.h>
#include "opl_emu.h"
#include "audio_out.h"
#include <atomic>
#include <vector>
#define IDI_ICON1 101
#define KEY_UP     72  //? 
#define KEY_DOWN    80  //? 
#define ESC       0x1B 

using namespace std;
static HWND 	hwndEditbox; 
static bool     g_useEmu = false;   // no serial port: local OPL3 emulator
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
#define WM_APP_SET_EDIT_TEXT (WM_APP + 1)

// The player runs in a worker thread.  Post text to the UI thread instead of
// calling SetWindowText() directly: a synchronous cross-thread message can
// deadlock when the UI thread is joining the worker (for example on Play).
static void postEditText(const char *text)
{
    size_t size = strlen(text) + 1;
    char *copy = (char *)malloc(size);
    if (!copy) return;
    memcpy(copy, text, size);
    if (!PostMessage(GetParent(hwndEditbox), WM_APP_SET_EDIT_TEXT, 0, (LPARAM)copy))
        free(copy);
}

// Keep the current file and its chip visible while a tune is playing.  The
// edit box is also used for the optional GD3 information.
static void showPlaybackInfo(const char *file, const char *chips, const char *gd3)
{
    char text[4600];
    const char *name = file;
    const char *slash = strrchr(file, '\\');
    if (!slash) slash = strrchr(file, '/');
    if (slash) name = slash + 1;

    snprintf(text, sizeof(text), "Now playing: %s\r\nChip: %s", name,
             (chips && *chips) ? chips : "unknown");
    if (gd3 && *gd3) {
        size_t used = strlen(text);
        if (used < sizeof(text) - 1)
            snprintf(text + used, sizeof(text) - used, "\r\n\r\n%s", gd3);
    }
    if (g_useEmu) {
        size_t used = strlen(text);
        if (used < sizeof(text) - 1)
            snprintf(text + used, sizeof(text) - used,
                     "\r\nOutput: OPL3 emulator -> Windows audio");
    }
    postEditText(text);
}

// control ids of the child windows (also used to start the player)
#define IDcmdButton    1
#define IDeditBox      2
#define IDPlayButton   3
#define IDNextButton   4
#define IDLoadButton   5
#define IDBankCombo    6
#define IDOpenButton   7
#define IDPrevButton   8
#define IDLoopCheck    9

#define VGM_HEADER_LEN (0x80)
#define SAMPLE_RATE (44100)
#define EMU_RATE (48000)
#define RGBVALUE(hex) ((hex & 0xff) << 16 | (hex & 0xff00) | hex >> 16)
PORT COM1;

static Opl3Emu  g_emu;
static AudioOut g_audio;
static ADL_MIDIPlayer *g_midiDev = NULL;      // called by the audio thread only
static std::atomic<bool> g_midiDone(false);

// libADLMIDI writes to the COM port handle this application already owns
// (see the "@host" support in its ChipSerialPort); opening COM3 a second
// time would fail because serial.c opens it without sharing.
extern "C" {
HANDLE adl_host_serial_handle = NULL;
unsigned long adl_host_serial_bytes = 0;
}

int com_num=3;
int midiBank=0;
int autoplay=1;
int loopone=0;            // repeat the current file instead of advancing
int octave=0;             // whole octaves the OPL2 pitch is shifted by
int debuglog=0;   // write opl3usb.log when enabled in the ini

#define VGM_SAMPLE_RATE    44100.0                    // VGM sample rate (Hz)
#define VGM_SPEED_DEFAULT  (VGM_SAMPLE_RATE/1000.0)   // samples per millisecond
float speed = (float)VGM_SPEED_DEFAULT;
static thread * thPlay=NULL;

static void saveConfig()
{
    FILE *conf=fopen("opl3usb.ini","w");
    if(!conf) return;
    fprintf(conf,"COM=%d\n",com_num);
    fprintf(conf,"speed=%g\n",speed);
    fprintf(conf,"autoplay=%d\n",autoplay);
    fprintf(conf,"debug=%d\n",debuglog);
    fprintf(conf,"octave=%d\n",octave);
    fprintf(conf,"midibank=%d\n",midiBank);
    fprintf(conf,"loopone=%d\n",loopone);
    fclose(conf);
}

// libADLMIDI returns UTF-8 text (bank names, MIDI titles), the Win32
// edit box of this ANSI application expects the system code page.
static void utf8ToAnsi(const char *src, char *dst, int dstlen)
{
    if(!src || !*src || dstlen<=0){ if(dstlen>0) dst[0]=0; return; }
    wchar_t wbuf[512];
    int n=MultiByteToWideChar(CP_UTF8,0,src,-1,wbuf,512);
    if(n<=0){ snprintf(dst,dstlen,"%s",src); return; }
    WideCharToMultiByte(CP_ACP,0,wbuf,-1,dst,dstlen,NULL,NULL);
}
  

int le_int(char * p){
   int ret=0;
   int i=3;
   for(i=3;i>0;i--){
   	   ret+=p[i]&0xff;
   	   ret<<=8;
   }
   ret+=p[i]&0xff;
   
   return ret;
}

void OPL3write(unsigned char * raw){
	int show=0;
	char nuke[3]={0};
 unsigned char bank = (raw[0] > 0x50) ? (raw[0] & 0x01) : 0;
 unsigned char reg  = raw[1];
 unsigned char data = raw[2];
 if (show)	printf("%02X %02X %02X =>",bank&0xff,reg&0xff,data&0xff);
    if (g_useEmu) {
        g_emu.write(((unsigned)bank << 8) | reg, data);
        return;
    }
	nuke[0] = ((bank << 2)&0x04) | 0x80 | ((reg >> 6) & 0x03 ) & 0xff;      // high 1xxx xxxx for nuke protocol
	nuke[1] = 0x7f & (( reg << 1 )&0x7f) | ((data >> 7 ) & 0x01);
	nuke[2] = 0x7f & data;
  if (show) printf("%02X %02X %02X =>",nuke[0]&0xff,nuke[1],nuke[2]);
	Serial_SendData(COM1, nuke, 3);
	// no flush per packet: waiting for the port to drain after every
	// 3 byte packet costs ~260 us and was the biggest source of jitter.
	// The data is flushed before the player goes to sleep instead.
}
void OPL3off()
{

     unsigned char reset[]={0x5e,0x05,0x00};
     OPL3write(reset);
     reset[0]=0x5f;
     reset[1]=0x01;
     OPL3write(reset);
     reset[1]=0x08;
     OPL3write(reset);
     
     Sleep(150);
  
}
void reset()
{

     unsigned char reset[]={0x5f,0x05,0x01};
     OPL3write(reset);
     reset[1]=0x04;
     reset[2]=0x00;
     OPL3write(reset);
     
     reset[2]=0x00;
     for(int add=0;add<=0xff;add++){
     	reset[1]=add;
     	reset[0]=0x5e;
     	OPL3write(reset);
     	//Sleep(1);
     	reset[0]=0x5f;
     	OPL3write(reset);
     	//Sleep(1);
	 }
	 
	Sleep(100);
    OPL3off();
}
void nop(){
	for(int i=0;i<1000;i++);
}

void locateCursor(const int row, const int col){
    printf("%c[%d;%dH",27,row,col);
}
void   gotoxy(int   x,   int   y){   
  COORD   c;   
  c.X   =   x   -   1;   
  c.Y   =   y   -   1;   
  SetConsoleCursorPosition   (GetStdHandle(STD_OUTPUT_HANDLE),   c);   
}   
    int wait=0;
    int stop=0;
    volatile int skipfile=0;   // set by the Next button, skips the current file
    volatile int prevtrack=0;  // set by the Previous button, steps back one file

// Audio-thread callbacks.  emuFill renders the local Nuked OPL3 that receives
// the VGM register stream; midiFill pulls PCM straight out of libADLMIDI, so
// the sound card clock also paces the MIDI sequencer.
static bool emuFill(short *buf, int frames)
{
    g_emu.render(buf, frames);
    return true;
}

static bool midiFill(short *buf, int frames)
{
    if (!g_midiDev)
        return false;
    if (stop || skipfile) {
        g_midiDone = true;
        return false;
    }
    int got = adl_play(g_midiDev, frames * 2, buf);
    if (got <= 0) {
        g_midiDone = true;
        return false;
    }
    if (got < frames * 2)
        memset(buf + got, 0, (size_t)(frames * 2 - got) * sizeof(short));
    if (adl_atEnd(g_midiDev))
        g_midiDone = true;
    return got == frames * 2;
}

static HANDLE        hrTimer     = NULL;
static LARGE_INTEGER qpcFreq;
static int           timingReady = 0;

static void timing_init(void){
	if(timingReady) return;
	timingReady=1;
	QueryPerformanceFrequency(&qpcFreq);
	// high resolution timer (Windows 10 1803+), plain timer as a fallback
	hrTimer=CreateWaitableTimerExW(NULL,NULL,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_ALL_ACCESS);
	if(hrTimer==NULL) hrTimer=CreateWaitableTimerExW(NULL,NULL,0,TIMER_ALL_ACCESS);
	timeBeginPeriod(1);                    // 1 ms timer resolution for Sleep()
}

static void timing_exit(void){
	if(hrTimer){ CloseHandle(hrTimer); hrTimer=NULL; }
	timeEndPeriod(1);
	timingReady=0;
}

// microseconds, high resolution
static inline double now_us(void){
	LARGE_INTEGER c;
	QueryPerformanceCounter(&c);
	return (double)c.QuadPart*1000000.0/(double)qpcFreq.QuadPart;
}

// sleep until the absolute deadline (same unit as now_us), aborts on stop
static void wait_until_us(double deadline_us){
	for(;;){
		if(stop || skipfile) return;
		double remain=deadline_us-now_us();
		if(remain<=0) return;
		if(remain>250.0 && hrTimer){
			// wake up ~120 us before the deadline, then spin the rest
			double slice=remain-120.0;
			if(slice>50000.0) slice=50000.0;      // keep the stop flag responsive
			LARGE_INTEGER due;
			due.QuadPart=-(LONGLONG)(slice*1000.0); // -100 ns units
			if(SetWaitableTimerEx(hrTimer,&due,0,NULL,NULL,NULL,0)){
				WaitForSingleObject(hrTimer,(DWORD)(slice/1000.0)+20);
				continue;
			}
			hrTimer=NULL;
		}
		if(remain>1200.0){ Sleep(1); continue; }
		while(now_us()<deadline_us){ YieldProcessor(); }   // last milliseconds
		return;
	}
}

void playFile(char * vgmfile){

    VgmChip chip;
    vgmchip_init(&chip,octave);
    unsigned char op[3]={0};
//	char key=0;
	char started=0;
	int gd3offset=0;
	char gd3info[4096]={0};
	char chipdesc[128]="unknown";
	int chipKnown=0;
	int gd3len=0;
	gotoxy(1,6);
	DWORD tick,lasttick;	
	char vgm_header[VGM_HEADER_LEN]={0};
	// timer resolution is managed by timing_init()/timing_exit()
		printf("playing: %s                       \n",vgmfile);
		showPlaybackInfo(vgmfile, "detecting...", "");
	
	    printf("speed:%f\n",speed);
	    VgmSource vgm;
    int srcok = vgm_src_is_vgz(vgmfile) ? vgm_src_open_vgz(&vgm,vgmfile)
                                         : vgm_src_open_file(&vgm,vgmfile);
		wait=0;
	    if(srcok==0){
	    	vgm_src_read(&vgm,vgm_header,VGM_HEADER_LEN);
	    	vgmchip_detect_header(&chip,(const unsigned char*)vgm_header,VGM_HEADER_LEN);
	    	vgmchip_desc(&chip,chipdesc,sizeof(chipdesc));
	    	chipKnown=vgmchip_mask(&chip)!=0;
	    	showPlaybackInfo(vgmfile,chipdesc,"");
	    	printf("data offset:%02X\n",(vgm_header[0x34]&0xff)+0x34);
	    	//printf("OPL2:%02X %02X %02X \n",vgm_header[0x50]&0xff,vgm_header[0x51]&0xff,vgm_header[0x52]&0xff);
	    	printf("OPL2:%d Hz                \n",le_int(&vgm_header[0x50]));
	    	gd3offset=le_int(&vgm_header[0x14]);
	    	if(gd3offset>0){	
				vgm_src_seek(&vgm,gd3offset+0x14,SEEK_SET);
	    		gd3len=(int)vgm_src_read(&vgm,gd3info,4096);
	    		int base=12;
	    		int i=0,j=0;
	    		int n=0;
	    		for(i=base;i<gd3len;i+=2){
					if(gd3info[i]>31&&gd3info[i]<127){
						gd3info[j++]=gd3info[i];
					}else if(gd3info[i+1]==0&&gd3info[i]==0&&gd3info[j-1]!='\n'){
						gd3info[j++]='\r';
						gd3info[j++]='\n';
						i+=2;
						n++;
						if (n==7) break;
						while(i<gd3len&&(gd3info[i]!=0||gd3info[i+1]!=0)) i+=2;
					}else if (gd3info[i]>127){
						gd3info[j++]=' ';
					}
				}
				//i-=2;
				while(j<gd3len){
					gd3info[j]=0;j++;
				}
				
	    		showPlaybackInfo(vgmfile,chipdesc,gd3info);
			}
	    
	    	reset();
	    	if(le_int(&vgm_header[0x50])) OPL3off();
	    	//printf("OPL3:%02X %02X %02X \n",vgm_header[0x5c],vgm_header[0x5d],vgm_header[0x5e]&0xff);
	    	printf("OPL3:%d Hz                \n",vgm_header[0x5c]+(vgm_header[0x5d]<<8)+((vgm_header[0x5e]&0xff)<<16));
	    	vgm_src_seek(&vgm,(vgm_header[0x34]&0xff)+0x34,SEEK_SET);
	    	int count=0;
	    	printf("Up/Down : speed ... Esc: next ... Ctrl+C: Stop \n");
	    	started=0;
	    	// --- precise timeline -------------------------------------------
	    	// A VGM sample is 1/44100 s, so the position inside the file is
	    	// simply the number of samples that have been sent.  The deadline
	    	// of the next event is calculated from that counter, therefore the
	    	// timing never drifts, no matter how long the file is.
	    	long long samples=0;
	    	double   t0=now_us();
	    	double   rate=(double)speed*1000.0;      // samples per second
	    	int      endofdata=0;
	    	while(vgm_src_read(&vgm,op,3)==3 && !endofdata && !skipfile){
	    	
	    		int writelen=vgmchip_write_len(op[0]);
	    		if(writelen>0){
 	    		   unsigned char chipout[VGMCHIP_MAXOUT][3];
 	    		   int chipn = vgmchip_translate(&chip,op[0],op[1],op[2],chipout);
 	    		   vgmchip_note(&chip,op[0]);
 	    		   if(!chipKnown && vgmchip_mask(&chip)){
 	    			   vgmchip_desc(&chip,chipdesc,sizeof(chipdesc));
 	    			   showPlaybackInfo(vgmfile,chipdesc,gd3info);
 	    			   chipKnown=1;
 	    		   }
 	    		   for(int chipk=0; chipk<chipn; chipk++) OPL3write(chipout[chipk]);
	    			if(writelen==2) vgm_src_seek(&vgm,-1,SEEK_CUR);
	    		   count++;
	    		   started=1;
	    		   //if(count%44==0) Sleep(1);
	    	//	if(op[2]&(0x01<<sleep))Sleep(10);
	    		}else{
	    		   //if(op[0]!=0x61) printf("shortcut %02X %02X %02X\n",op[0]&0xff,op[1]&0xff,op[2]&0xff);
	    		   
	    		   switch (op[0]){
	    		   		case 0x61:
	    		   			wait=(op[1]&0xff)+((op[2]&0xff)*256);
	    		   			break;
	    		   		case 0x62:
	    		   			wait=735;
	    		   			vgm_src_seek(&vgm,-2,SEEK_CUR);
	    		   			break;
	    		   		case 0x63:
	    		   			wait=882;
	    		   		    vgm_src_seek(&vgm,-2,SEEK_CUR);
	    		   			break;
	    		   		case 0x66:                     // end of sound data
						    endofdata=1;
						    break;
						case 0x70:
						case 0x71:
						case 0x72:
						case 0x73:
						case 0x74:
						case 0x75:
						case 0x76:
						case 0x77:
						case 0x78:
						case 0x79:
						case 0x7a:
						case 0x7b:
						case 0x7c:
						case 0x7d:
						case 0x7e:
						case 0x7f:
	    		   			wait=1+(0xff&op[0]-0x70);
	    		   			vgm_src_seek(&vgm,-2,SEEK_CUR);
	    		   			break;
	    		   		case 0xD2:
	    		   			wait++;
	    		   			vgm_src_seek(&vgm,1,SEEK_CUR);
	    		   			break;
	    		   		case 0x00:                                  // one-byte no-op
							vgm_src_seek(&vgm,-2,SEEK_CUR);
							break;
						case 0x30: case 0x31: case 0x3F:
						case 0x4F: case 0x50:               // one-byte operand
							vgm_src_seek(&vgm,-1,SEEK_CUR);
							break;
						case 0x67: {                        // data block
							unsigned char blockinfo[4]={0};
							if(vgm_src_read(&vgm,blockinfo,4)==4){
								long size=(long)blockinfo[0]|((long)blockinfo[1]<<8)|
								          ((long)blockinfo[2]<<16)|((long)blockinfo[3]<<24);
								vgm_src_seek(&vgm,size,SEEK_CUR);
							}
							break;
						}
						case 0x68:                          // PCM RAM write, 12 bytes
							vgm_src_seek(&vgm,9,SEEK_CUR);
							break;
						case 0x90: case 0x91: case 0x92:
						case 0x93: case 0x94: case 0x95:   // DAC stream control
							vgm_src_seek(&vgm,1,SEEK_CUR);
							break;
	    		   		default:
	    		   			if(op[0]>=0xC0 && op[0]<=0xDF) vgm_src_seek(&vgm,1,SEEK_CUR);
	    		   			else if(op[0]>=0xE0) vgm_src_seek(&vgm,2,SEEK_CUR);
	    		   			wait=0;
				   }
				   				   if(endofdata) break;
				   if(wait>0){
				   	samples+=wait;
				   	if(COM1) FlushFileBuffers(COM1);          // the chip gets it now
				   	wait_until_us(t0+(double)samples*1000000.0/rate);
				   }
				   wait=0;
				   if(stop) break;
				}
				//	if(wait<0) break;
				if(stop) break;   	
			}
													
	    	if(debuglog){
	    		double elapsed=(now_us()-t0)/1000000.0;
	    		double expected=(double)samples/rate;
	    		FILE *log=fopen("opl3usb.log","a");
	    		if(log){
	    			char chipdesc[64];
	    			fprintf(log,"%s\tchips=%s\tsamples=%lld\texpected=%.3fs\tactual=%.3fs\terror=%+.3f%%\n",
	    				vgmfile,vgmchip_desc(&chip,chipdesc,sizeof(chipdesc)),samples,expected,elapsed,
	    				expected>0.0?(elapsed-expected)*100.0/expected:0.0);
	    			fclose(log);
	    		}
	    	}
	    	vgm_src_close(&vgm);
	    	reset();
	    	OPL3off();
		}else{
			postEditText("Open failed!");
								
		}
}
// libADLMIDI turns the MIDI score into YMF262 register writes and sends them
// through the serial port.  The sequencer paces itself: adl_tickEvents()
// returns the time until the next MIDI event, the loop sleeps until then and
// feeds the real elapsed time back in on the next call.  That keeps the score
// in real time even when the serial link has to swallow a burst of writes.
void playMidiFile(const char *midfile)
{
	char text[1024];
	char bankNameAnsi[256]="unknown";
	char titleAnsi[256]="";
	char info[1024]={0};
	const char *const *bankList=adl_getBankNames();
	const char *bankName=(bankList && midiBank>=0 && midiBank<adl_getBanksCount())
	                     ? bankList[midiBank] : NULL;
	ADL_MIDIPlayer *dev=NULL;
	// No serial link: let libADLMIDI render into the waveOut stream instead.
	bool emu=g_useEmu;

	if(!emu && !COM1){
		postEditText("Serial port is not open!");
		return;
	}
	if(bankName) utf8ToAnsi(bankName,bankNameAnsi,sizeof(bankNameAnsi));

	dev=adl_init(emu?EMU_RATE:SAMPLE_RATE);
	if(!dev){
		snprintf(text,sizeof(text),"MIDI init failed: %s",
		         adl_errorInfo(NULL)?adl_errorInfo(NULL):"unknown error");
		postEditText(text);
		return;
	}

	adl_setNumChips(dev,1);
	adl_setLoopEnabled(dev,0);
	if(emu){
		adl_switchEmulator(dev,ADLMIDI_EMU_DOSBOX);
	}else if(adl_switchSerialHW(dev,"@host",115200,ADLMIDI_SerialProtocol_NukeYktOPL3)<0){
		snprintf(text,sizeof(text),"MIDI hardware mode failed: %s",
		         adl_errorInfo(dev)?adl_errorInfo(dev):"unknown error");
		postEditText(text);
		adl_close(dev);
		return;
	}
	if(adl_setBank(dev,midiBank)<0){
		snprintf(text,sizeof(text),"Bank %d failed: %s",midiBank,
		         adl_errorInfo(dev)?adl_errorInfo(dev):"unknown error");
		postEditText(text);
		adl_close(dev);
		return;
	}
	if(adl_openFile(dev,midfile)<0){
		snprintf(text,sizeof(text),"Open failed: %s",
		         adl_errorInfo(dev)?adl_errorInfo(dev):"unknown error");
		postEditText(text);
		adl_close(dev);
		return;
	}

	double length=adl_totalTimeLength(dev);
	const char *title=adl_metaMusicTitle(dev);
	if(title && *title) utf8ToAnsi(title,titleAnsi,sizeof(titleAnsi));
	snprintf(info,sizeof(info),"Bank: %s\r\nLength: %d:%02d   Tracks: %u",
	         bankNameAnsi,(int)(length/60.0),(int)length%60,(unsigned)adl_trackCount(dev));
	if(titleAnsi[0]){
		size_t used=strlen(info);
		if(used<sizeof(info)-1)
			snprintf(info+used,sizeof(info)-used,"\r\nTitle: %s",titleAnsi);
	}
	showPlaybackInfo(midfile,emu?"MIDI -> OPL3 emulator":"MIDI -> YMF262",info);
	printf("playing midi: %s (bank %d, %s)\n",midfile,midiBank,emu?"emulator":"serial");

	double t0=now_us();
	if(emu){
		// The sound card clocks the score: every refilled waveOut buffer asks
		// adl_play() for exactly that many frames, so no extra pacing loop.
		g_midiDone=false;
		g_midiDev=dev;
		g_emu.reset();                 // drop whatever the previous VGM left behind
		g_audio.setFill(midiFill);
		while(!stop && !skipfile && !g_midiDone) Sleep(10);
		// Wait for a fill that may still be inside adl_play(), then detach the
		// sequencer before adl_panic()/adl_close() touch it.
		g_audio.setFill(emuFill);
		g_midiDev=NULL;
		g_emu.reset();
		adl_panic(dev);
	}else{
		adl_host_serial_bytes=0;
		OPL3off();
		const double min_delay=0.020;      // service the sequencer at least every 20 ms
		const double granularity=0.002;    // <= 1 ms of event timing error
		double last=t0;
		double last_flush=t0;
		double advance=1e-9;               // first call only processes events at time 0
		double tick_delay=0.0;

		while(!stop && !skipfile){
			tick_delay=adl_tickEvents(dev,advance,granularity);
			double now=now_us();

			// The write buffer is not drained per packet; flushing a few times
			// per second bounds the backlog without stalling on every event.
			if(now-last_flush>250000.0){
				FlushFileBuffers(COM1);
				now=now_us();
				last_flush=now;
			}
			if(tick_delay<0.0) break;      // sequencer error

			double next=tick_delay<min_delay?tick_delay:min_delay;
			if(next>0.0) wait_until_us(now+next*1000000.0);

			double after=now_us();
			advance=(after-last)/1000000.0;
			if(advance>0.25) advance=0.25; // after a stall: catch up, don't skip
			last=after;
			if(adl_atEnd(dev) && tick_delay<=0.0) break;
		}

		adl_panic(dev);
		FlushFileBuffers(COM1);
		OPL3off();
	}

	if(debuglog){
		double actual=(now_us()-t0)/1000000.0;
		double played=adl_positionTell(dev);
		FILE *log=fopen("opl3usb.log","a");
		if(log){
			fprintf(log,"%s\tmidi\tbank=%s\tout=%s\tlength=%.3fs\tplayed=%.3fs\tactual=%.3fs\terror=%+.3f%%\tbytes=%lu\n",
			        midfile,bankNameAnsi,emu?"emu":"serial",length,played,actual,
			        length>0.0?(actual-length)*100.0/length:0.0,adl_host_serial_bytes);
			fclose(log);
		}
	}
	adl_close(dev);
}

int isVgmFile(const char fname[])
{
	int l = strlen(fname);
	if (l <= 4) return 0;
	// .vgm is a plain file, .vgz is the same file compressed with gzip
	if (stricmp(fname + l - 4, ".vgm") == 0) return l - 3;
	if (stricmp(fname + l - 4, ".vgz") == 0) return l - 3;
	return 0;
}

int isMidiFile(const char fname[])
{
	int l = strlen(fname);
	if (l <= 4) return 0;
	if (stricmp(fname + l - 4, ".mid") == 0) return 1;
	if (l > 5 && stricmp(fname + l - 5, ".midi") == 0) return 1;
	return 0;
}

int isPlayableFile(const char fname[])
{
	return isVgmFile(fname) || isMidiFile(fname);
}

char vgmfile[1024]=".";	
void play(){
	// Previous and Loop one need the playlist up front, so a directory is
	// scanned once per pass and played by index.
	if(strlen(vgmfile)>0 && isPlayableFile(vgmfile)){
		for(;;){
			skipfile=0;      // a stale request must not eat this file
			prevtrack=0;
			if(isMidiFile(vgmfile)) playMidiFile(vgmfile);
			else playFile(vgmfile);
			if(stop) break;
			if(prevtrack){   // only one file here: Previous restarts it
				prevtrack=0;
				skipfile=0;
				continue;
			}
			if(skipfile){    // Next: nothing follows, so stop
				skipfile=0;
				break;
			}
			if(!loopone) break;
		}
		stop=0;
		return;
	}

	if(strlen(vgmfile)==0) strcpy(vgmfile,".");
	postEditText(vgmfile);
	while(!stop){
		std::vector<std::string> files;
		DIR *pdir=opendir(vgmfile); //opendir for playlist
		if (!pdir){
			printf ("opendir() failure; terminating");
			exit(1);
		}
		errno=0;
		struct dirent *pent;
		while ((pent=readdir(pdir))){
			if(isPlayableFile(pent->d_name)) files.push_back(pent->d_name);
		}
		if (errno){
			printf ("readdir() failure; terminating");
			exit(1);
		}
		closedir(pdir);
		if(files.empty()){
			Sleep(200);   // nothing playable here, do not spin
			continue;
		}

		wait=0;
		size_t idx=0;
		while(!stop && idx<files.size()){
			gotoxy(1,3);
			char filepath[1024];
			snprintf(filepath,sizeof(filepath),"%s\\%s",vgmfile,files[idx].c_str());
			showPlaybackInfo(filepath,"detecting...","");

			OPL3off();
			reset();
			// Between tracks the worker is busy for ~0.6 s; a Next or Previous
			// press during that gap belongs to the track that just ended, not
			// to the one that is about to start.
			skipfile=0;
			prevtrack=0;

			if(isMidiFile(filepath)) playMidiFile(filepath);
			else playFile(filepath);
			if(stop) break;

			if(prevtrack){
				prevtrack=0;
				skipfile=0;
				idx = (idx==0) ? files.size()-1 : idx-1;
				continue;
			}
			if(skipfile){    // Next: advance even when Loop one is set
				skipfile=0;
				++idx;
				continue;
			}
			if(!loopone) ++idx;
		}
	}
	stop =0;
} 

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
	WNDCLASSEX wc;
	HWND hwnd, hwndDT;
	MSG msg;
	RECT rect;
	int dtWidth,dtHeight;
    HDC         	hdc;
	memset(&wc,0,sizeof(wc));
	wc.cbSize		 = sizeof(WNDCLASSEX);
	wc.lpfnWndProc	 = WndProc;
	wc.hInstance	 = hInstance;
	wc.hCursor		 = LoadCursor(NULL, IDC_ARROW);
	
	wc.hbrBackground = (HBRUSH)(CreateSolidBrush(RGB(0xfc,0x55,0x31)));
	wc.lpszClassName = "WindowClass";
	wc.hIcon		 = LoadIcon(hInstance,IDI_APPLICATION);
	wc.hIconSm		 = LoadIcon(NULL, IDI_APPLICATION);
	
	const char * config="opl3usb.ini";
	if (access(config, R_OK ) != -1){
		FILE *conf=fopen(config,"r");
		fscanf(conf,"COM=%d\n",&com_num);
		fscanf(conf,"speed=%f\n",&speed);
		fscanf(conf,"autoplay=%d\n",&autoplay);
		fscanf(conf,"debug=%d\n",&debuglog);
		fscanf(conf,"octave=%d\n",&octave);
		fscanf(conf,"midibank=%d\n",&midiBank);
		fscanf(conf,"loopone=%d\n",&loopone);
		fclose(conf);
	}
	if(midiBank<0 || midiBank>=adl_getBanksCount()) midiBank=0;
    timing_init();
    printf("open com%d \n",com_num);
	int probePort=com_num;
	while((COM1 = serial_init(probePort, 115200, 8, 1 , 0))==NULL && probePort<12)  {
		probePort++;
	}
	if(COM1) com_num=probePort;   // remember the port that actually opened
	adl_host_serial_handle=COM1;
	if(!COM1){
		// No YMF262 on a serial line: play through the built-in OPL3
		// emulator and the default Windows audio device instead.
		printf("no serial port, falling back to the OPL3 emulator\n");
		if(g_audio.start(EMU_RATE) && g_emu.init(EMU_RATE)){
			g_useEmu=true;
			g_audio.setFill(emuFill);
		}else{
			printf("audio fallback unavailable\n");
			g_audio.stop();
		}
	}
	if(!RegisterClassEx(&wc)) {
		MessageBox(NULL, "Window Registration Failed!","Error!",MB_ICONEXCLAMATION|MB_OK);
		return 0;
	}
 
	hwndDT=GetDesktopWindow(); //????? 
	GetWindowRect(hwndDT,&rect); //????? 
	dtWidth=rect.right-rect.left; //???? 
	dtHeight=rect.bottom-rect.top; //???? 
	
	hwnd = CreateWindowEx(WS_EX_CLIENTEDGE,wc.lpszClassName,
		"OPL3USB VGM/MIDI Player",WS_VISIBLE|WS_OVERLAPPEDWINDOW,
		(dtWidth-380)/2,   /*????*/ 
		(dtHeight-235)/2,
		400,
		270,
		NULL,NULL,hInstance,NULL);
	//MoveWindow(hwnd, (width-640)/2, (height-480)/2, 640, 480, FALSE);
  			
	if(hwnd == NULL) {
		MessageBox(NULL, "Window Creation Failed!","Error!", MB_ICONEXCLAMATION|MB_OK);
		return 0;
	}
	if(g_useEmu)
		postEditText("No serial port - OPL3 emulator -> Windows audio");
	// play the supported files of the current directory right away
	if(autoplay){
		PostMessage(hwnd, WM_COMMAND, (WPARAM)IDPlayButton, 0);
	}
	while(GetMessage(&msg, NULL, 0, 0) > 0) { 
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
 
    saveConfig();
	timing_exit();
	return msg.wParam;
}
void start(){
	// the player thread stays joinable, so that it can be stopped and waited
	// for properly (a detached thread cannot be joined and would throw)
	if(thPlay!=NULL && thPlay->joinable()){
		stop=1;                  // let the running player leave its loop
		thPlay->join();          // and wait until it is really gone
	}
	stop=0;
	prevtrack=0;
	thPlay=new thread(play);
}
void load(){
	TCHAR szPathName[4096];
	
	 BROWSEINFO bInfo={0};
	 bInfo.hwndOwner=GetForegroundWindow();//???
	 bInfo.lpszTitle=TEXT("folder with .vgm, .vgz, .mid or .midi files");
	 bInfo.ulFlags=BIF_RETURNONLYFSDIRS |BIF_USENEWUI|
					BIF_UAHINT |BIF_NONEWFOLDERBUTTON ;
					
		//????? ulFlags ?? http://msdn.microsoft.com/en-us/library/bb773205(v=vs.85).aspx
	 
	 LPITEMIDLIST lpDlist;
	 
	 lpDlist=SHBrowseForFolder(&bInfo);
	 if (lpDlist!=NULL)//???????
	 {
		SHGetPathFromIDList(lpDlist,szPathName);
		postEditText(szPathName);
		strcpy(vgmfile,szPathName);
		start();
	 }
	 
	
}
void openFile()
{
	char path[1024]={0};
	OPENFILENAMEA ofn;
	memset(&ofn,0,sizeof(ofn));
	ofn.lStructSize=sizeof(ofn);
	ofn.hwndOwner=GetForegroundWindow();
	ofn.lpstrFilter="Music files (*.vgm;*.vgz;*.mid;*.midi)\0*.vgm;*.vgz;*.mid;*.midi\0"
	                "VGM files (*.vgm;*.vgz)\0*.vgm;*.vgz\0"
	                "MIDI files (*.mid;*.midi)\0*.mid;*.midi\0"
	                "All files (*.*)\0*.*\0";
	ofn.lpstrFile=path;
	ofn.nMaxFile=sizeof(path);
	ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
	if(GetOpenFileNameA(&ofn)){
		strcpy(vgmfile,path);
		start();
	}
}

// fill the instrument bank list once, the selection is used by the next
// MIDI file that is started
static void fillBankCombo(HWND combo)
{
	int count=adl_getBanksCount();
	const char *const *names=adl_getBankNames();
	wchar_t wname[256];
	if(!names || count<=0) return;
	for(int i=0;i<count;i++){
		const char *n=names[i]?names[i]:"";
		if(MultiByteToWideChar(CP_UTF8,0,n,-1,wname,256)>0)
			SendMessageW(combo,CB_ADDSTRING,0,(LPARAM)wname);
		else
			SendMessageA(combo,CB_ADDSTRING,0,(LPARAM)n);
	}
	SendMessage(combo,CB_SETDROPPEDWIDTH,620,0);
	SendMessage(combo,CB_SETCURSEL,(WPARAM)midiBank,0);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	HDC         	hdc;
	PAINTSTRUCT 	ps;
	RECT			rect;
	POINT			mouse;
	static int  	cxChar, cyChar;
	static int  	mX, mY;
	static HWND 	hwndPlay;
	static HWND 	hwndNext;
	static HWND 	hwndLoad;
	static HWND 	hwndOpen;
	static HWND 	hwndBank;
	static HWND 	hwndLoop;

	string			strXy;
	char			x[5], y[5];
	char			buff[4096] = {0};
// ??????
	LOGFONT LogFont;
	::memset(&LogFont, 0, sizeof(LOGFONT));
	lstrcpy(LogFont.lfFaceName, "??");
	LogFont.lfWeight = 32;
	LogFont.lfHeight = -14; // ????
	LogFont.lfCharSet = 134;
	LogFont.lfOutPrecision = 3;
	LogFont.lfClipPrecision = 2;
	LogFont.lfOrientation = 45;
	LogFont.lfQuality = 1;
	LogFont.lfPitchAndFamily = 2;
	
	// ????
	HFONT hFont = CreateFontIndirect(&LogFont);
	
    
	cxChar = LOWORD(GetDialogBaseUnits());
	cyChar = HIWORD(GetDialogBaseUnits());


    int height=8;
	switch (message) {
		case WM_APP_SET_EDIT_TEXT:
			if (lParam) {
				SetWindowText(hwndEditbox, (const char *)lParam);
				free((void *)lParam);
			}
			return 0;
		case WM_CREATE:
			{
			RECT client;
			GetClientRect(hwnd,&client);
			int editW=client.right-2*cxChar;
			int comboX=7*cxChar;
			int loopX=client.right-13*cxChar;
			int comboW=loopX-comboX-cxChar;
			if(comboW<4*cxChar) comboW=4*cxChar;
			hwndEditbox = CreateWindow( TEXT("edit"),NULL,
										WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE,
                                   		cxChar * 1, cyChar * 0.2,
                                   		editW, (-0.5+height) * cyChar,
										hwnd,(HMENU)IDeditBox,NULL,NULL);
			hwndPlay = CreateWindow ( TEXT("button"), TEXT("Play"),
                            			WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                   		cxChar * 10, cyChar * height,
                                   		5 * cxChar, 1.5 * cyChar,
                                   		hwnd, (HMENU)IDPlayButton, ((LPCREATESTRUCT) lParam)->hInstance, NULL);
			CreateWindow ( TEXT("button"), TEXT("Prev"),
                            			WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                   		cxChar * 2, cyChar * height,
                                   		5 * cxChar, 1.5 * cyChar,
                                   		hwnd, (HMENU)IDPrevButton, ((LPCREATESTRUCT) lParam)->hInstance, NULL);
			hwndNext = CreateWindow ( TEXT("button"), TEXT("Next"),
                            			WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                   		cxChar * 18, cyChar * height,
                                   		5 * cxChar, 1.5 * cyChar,
                                   		hwnd, (HMENU)IDNextButton, ((LPCREATESTRUCT) lParam)->hInstance, NULL);
			hwndLoad = CreateWindow ( TEXT("button"), TEXT("Load"),
                            			WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                   		cxChar * 26, cyChar * height,
                                   		5 * cxChar, 1.5 * cyChar,
                                   		hwnd, (HMENU)IDLoadButton, ((LPCREATESTRUCT) lParam)->hInstance, NULL);
			hwndOpen = CreateWindow ( TEXT("button"), TEXT("Open"),
                            			WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                   		cxChar * 34, cyChar * height,
                                   		5 * cxChar, 1.5 * cyChar,
                                   		hwnd, (HMENU)IDOpenButton, ((LPCREATESTRUCT) lParam)->hInstance, NULL);
			CreateWindow ( TEXT("static"), TEXT("Bank:"),
                            			WS_CHILD | WS_VISIBLE,
                                   		cxChar * 2, cyChar * 10.5,
                                   		5 * cxChar, 1.5 * cyChar,
                                   		hwnd, NULL, ((LPCREATESTRUCT) lParam)->hInstance, NULL);
			hwndBank = CreateWindow ( TEXT("combobox"), NULL,
                            			WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST,
                                   		comboX, cyChar * 10.2,
                                   		comboW, 200,
                                   		hwnd, (HMENU)IDBankCombo, ((LPCREATESTRUCT) lParam)->hInstance, NULL);
			fillBankCombo(hwndBank);
			hwndLoop = CreateWindow ( TEXT("button"), TEXT("Loop one"),
                            			WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                   		loopX, cyChar * 10.2,
                                   		12 * cxChar, 1.5 * cyChar,
                                   		hwnd, (HMENU)IDLoopCheck, ((LPCREATESTRUCT) lParam)->hInstance, NULL);
			if(hwndLoop && loopone) SendMessage(hwndLoop,BM_SETCHECK,BST_CHECKED,0);
			if (!hwndPlay) MessageBox(NULL,"??????","Message",MB_OK|MB_ICONERROR);
			ShowWindow(hwndPlay,SW_SHOW);
            UpdateWindow(hwndPlay);
			ShowWindow(hwndNext,SW_SHOW);
            UpdateWindow(hwndNext);
		          		
			// ??????
			//HWND hWndStatic = GetDlgItem(hDlg, IDC_STATIC_INFO);
			
			// ??????
			::SendMessage(hwndEditbox, WM_SETFONT, (WPARAM)hFont, 0);
			::SendMessage(hwndBank, WM_SETFONT, (WPARAM)hFont, 0);
			::SendMessage(hwndLoop, WM_SETFONT, (WPARAM)hFont, 0);
										
			if (!hwndEditbox) MessageBox(NULL,"???????","Message",MB_OK|MB_ICONERROR);
			ShowWindow(hwndEditbox,SW_SHOW);
            UpdateWindow(hwndEditbox);
			DragAcceptFiles(hwnd,TRUE);
			}
        	return 0 ;

		case WM_DROPFILES:
			{
			HDROP drop=(HDROP)wParam;
			UINT count=DragQueryFileA(drop,0xFFFFFFFF,NULL,0);
			char path[1024]={0};
			bool found=false;
			// play the first dropped file this player can handle
			for(UINT i=0;i<count;i++){
				if(DragQueryFileA(drop,i,path,sizeof(path))==0) continue;
				if(isPlayableFile(path)){ found=true; break; }
			}
			if(found){
				strcpy(vgmfile,path);
				start();
			}else if(path[0]){
				char text[1200];
				snprintf(text,sizeof(text),
				         "Unsupported file:\r\n%s\r\n\r\nDrop .vgm, .vgz, .mid or .midi files.",
				         path);
				postEditText(text);
			}
			DragFinish(drop);
			}
			return 0;

	    case WM_PAINT:
			
			
			hdc = BeginPaint (hwnd, &ps);
	    	//SetRect(&rect, 10, 10, 300, 200);
    	   // FrameRect(hdc, &rect, (HBRUSH)GetStockObject(BLACK_BRUSH));
			//GetClientRect (hwnd, &rect);
			//rect.left += 20;
            //rect.top += 2;
            //SetBkColor(hdc, RGBVALUE(0x0101ff));
            //SetTextColor(hdc, RGB(255,0,0)); //???RGB?????????
			//DrawText(hdc, TEXT(" Hello, Dev-C++! "), -1, &rect, DT_SINGLELINE | DT_TOP | DT_LEFT);
			EndPaint(hwnd, &ps);
			return 0;
 
	    case WM_SIZE:
			//GetWindowRect(hwnd, &rect);
			//MoveWindow(hwndPlay, (rect.right-rect.left)/2 - 15*cxChar ,\
			//(rect.bottom-rect.top)/2 - 1.25*cxChar, 30*cxChar, 2.5*cyChar, FALSE);
			return 0;
			
		case WM_COMMAND:
			//????_click()?? 
			switch (LOWORD(wParam)) {
			case 0:
				PostQuitMessage(0);
				break;
			case IDPlayButton: 
			    start();
				
				break;
			case IDNextButton: 
			    if(thPlay!=NULL){
			    	
			    	// leave the current file at the next opportunity
			    	skipfile=1;
				}
				
				break;
			case IDPrevButton:
			    if(thPlay!=NULL){
			    	// leave the current file and step back one place
			    	prevtrack=1;
			    	skipfile=1;
				}
				
				break;
			case IDLoopCheck:
				if(HIWORD(wParam)==BN_CLICKED){
					loopone=(SendMessage(hwndLoop,BM_GETCHECK,0,0)==BST_CHECKED);
					saveConfig();
				}
				break;
			case IDLoadButton: 
				load();
			    		
				break;
			case IDOpenButton:
				openFile();
				break;
			case IDBankCombo:
				if(HIWORD(wParam)==CBN_SELCHANGE){
					int sel=(int)SendMessage(hwndBank,CB_GETCURSEL,0,0);
					if(sel>=0){
						midiBank=sel;
						saveConfig();
					}
				}
				break;
			case IDeditBox:
				GetWindowText(hwndEditbox,buff,4096);
				break;
			}
			return 0; 
 
		case WM_LBUTTONDOWN: // WM_LBUTTONDOWN??????????
			GetCursorPos(&mouse);
			GetWindowRect(hwnd, &rect);
			mX=mouse.x-rect.left;
			mY=mouse.y-rect.top;
			itoa(mX,x,10);
			itoa(mY,y,10);
			speed=mX/10;
			//SetWindowText(hwndEditbox,strXy.c_str());
			//MessageBox(NULL, strXy.c_str(), "", MB_ICONASTERISK);
			return 0;
 
		case WM_CLOSE:
			if (IDYES==MessageBox(hwnd, "Quit", "Exit", MB_ICONQUESTION | MB_YESNO)){
					stop=1;
				//	reset();
				//	ClosePort(COM1);
				
					DestroyWindow(hwnd);  //????
			}
			
			return 0;
		  
		case WM_DESTROY:
			stop=1;
			//ShellAbout(hwnd, "???????", "??,?????????!", NULL);
			if(thPlay!=NULL && thPlay->joinable()) thPlay->join();
			PostQuitMessage(0);
			
			return 0;
	}
	
	return DefWindowProc(hwnd, message, wParam, lParam);
}

