#include <stdio.h>
#include "serial.h"
#include "vgmsrc.h"
#include "vgmchip.h"
#include <dirent.h>
#include <windows.h>
#include <conio.h> 
#include <winuser.h>
#include <mmsystem.h>

#define KEY_UP     72  
#define KEY_DOWN    80 
#define ESC       0x1B 
 
#define VGM_HEADER_LEN (0x80)
#define SAMPLE_RATE (44100)
#define VGM_SAMPLE_RATE    44100.0
#define VGM_SPEED_DEFAULT  (VGM_SAMPLE_RATE/1000.0)
PORT COM1;
float speed = (float)VGM_SPEED_DEFAULT;   // VGM samples per millisecond (44.1 = exact)


// --------------------------------------------------------------- timing ---
// The VGM file format counts every wait in "samples" of 1/44100 second, so a
// VGM file that is played at its original tempo consumes exactly 44100
// samples per second (VGM File Format specification).  Instead of guessing
// how long to sleep we keep the number of samples that have been sent and
// compute the *absolute* deadline of the next event.  That way the timing
// never drifts, no matter how long the file is.
//
// The waiting itself is done with the Windows high resolution waitable timer
// (CreateWaitableTimerEx with CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, Windows
// 10 1803 and newer).  It wakes up within ~100 us without burning CPU, so the
// timing is precise and cheap.  On older systems we fall back to
// timeBeginPeriod(1) plus a very short spin for the last milliseconds.


static volatile int timing_stop = 0;
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
		if(timing_stop) return;
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

int ld_int(char * p){
   return (p[0]&0xff+(p[1]&0xff)*256)+((p[2]&0xff)*256*256);
}
void OPL3write(char * raw){
	int show=0;
	char nuke[3]={0};
 if (show)	printf("%02X %02X %02X =>",raw[0]&0xff,raw[1]&0xff,raw[2]&0xff);
    if (raw[0]>0x50) raw [0] = raw[0] & 0x01; 
    else raw[0]=0;
	nuke[0] = ((raw[0] << 2)&0x04) | 0x80 | ((raw[1] >> 6) & 0x03 ) & 0xff;      // high 1xxx xxxx for nuke protocol
	nuke[1] = 0x7f & (( raw[1] << 1 )&0x7f) | ((raw[2] >> 7 ) & 0x01);
	nuke[2] = 0x7f & raw[2];

	
  if (show) printf("%02X %02X %02X =>",nuke[0]&0xff,nuke[1],nuke[2]);
	Serial_SendData(COM1, nuke, 3);
	char x1=0xff&nuke[0];
	char x2=0xff&nuke[1];
	char x3=0xff&nuke[2];
	
   //5E 75 FB =>81 7F 7B =>00 7F FB
	
    char bank = (x1>>2);
	char reg = ((x1<<6)&0x0c0)|((x2>>1)&0x3f);
	char data = ((x2<<7)&0x80)|(x3&0x7f);
  if (show) printf("%02X %02X %02X \n",bank&0x03,reg&0xff,data&0xff);
  
}
void OPL3off()
{

     char reset[]={0x5f,0x05,0x00};
     OPL3write(reset);
     reset[0]=0x5e;
     reset[1]=0x01;
     OPL3write(reset);
     reset[1]=0x08;
     OPL3write(reset);
     
     Sleep(150);
  
}
void reset()
{

     char reset[]={0x5f,0x05,0x01};
     OPL3write(reset);
     reset[1]=0x04;
     reset[2]=0x00;
     OPL3write(reset);
     
     reset[2]=0x00;
     for(int add=0;add<=0xff;add++){
     	reset[1]=add;
     	reset[0]=0x5e;
     	OPL3write(reset);
     //	Sleep(5);
     	reset[0]=0x5f;
     	OPL3write(reset);
     //	Sleep(5);
	 }
	 
	Sleep(500);
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

void playFile(char * vgmfile){
    VgmChip chip;
    vgmchip_init(&chip,0);
    int wait=0;
    unsigned char op[3]={0};
	char key=0;
	char started=0;
	char chipdesc[128]="unknown";
	int chipKnown=0;
		gotoxy(1,6);
		char vgm_header[VGM_HEADER_LEN]={0};

		printf("playing: %s                       \n",vgmfile);
	
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
	    	printf("chip: %s                       \n",chipdesc);
	    	printf("data offset:%02X\n",(vgm_header[0x34]&0xff)+0x34);
	    	//printf("OPL2:%02X %02X %02X \n",vgm_header[0x50]&0xff,vgm_header[0x51]&0xff,vgm_header[0x52]&0xff);
	    	printf("OPL2:%d Hz                \n",ld_int(&vgm_header[0x50]));
	    	reset();
	    	if(ld_int(&vgm_header[0x50])) OPL3off();
	    	//printf("OPL3:%02X %02X %02X \n",vgm_header[0x5c],vgm_header[0x5d],vgm_header[0x5e]&0xff);
	    	printf("OPL3:%d Hz                \n",vgm_header[0x5c]+(vgm_header[0x5d]<<8)+((vgm_header[0x5e]&0xff)<<16));
	    	vgm_src_seek(&vgm,(vgm_header[0x34]&0xff)+0x34,SEEK_SET);
	    	int count=0;
	    	printf("Up/Down : speed ... Esc: next ... Ctrl+C: Stop \n");
	    	started=0;
	    	long long samples=0;
	    	int      endofdata=0;
	    	double   t0=now_us();
	    	double   rate=(double)speed*1000.0;
	    	while(vgm_src_read(&vgm,op,3)==3 && !endofdata){
	    	if(_kbhit()){
	    		key = getch();
	    		if(key==KEY_UP) speed+=1;
	    		if(key==KEY_DOWN) speed-=1;
	    		if(key==ESC) break;
	    		key=0;
	    		//delline();
	    		
	    		 printf("\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b speed:%f",speed);
			}	
	    		int writelen=vgmchip_write_len(op[0]);
	    		if(writelen>0){
	    		   unsigned char chipout[VGMCHIP_MAXOUT][3];
	    		   int chipn = vgmchip_translate(&chip,op[0],op[1],op[2],chipout);
	    		   vgmchip_note(&chip,op[0]);
	    		   if(!chipKnown && vgmchip_mask(&chip)){
	    			   vgmchip_desc(&chip,chipdesc,sizeof(chipdesc));
	    			   printf("chip: %s                       \n",chipdesc);
	    			   chipKnown=1;
	    		   }
	    		   for(int chipk=0; chipk<chipn; chipk++) OPL3write((char*)chipout[chipk]);
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
				   if(wait>0){
				   	samples+=wait;
				   	FlushFileBuffers(COM1);
				   	wait_until_us(t0+(double)samples*1000000.0/rate);
				   }
				   wait=0;
				   	
				}
	    		if(wait<0) break;
				   	
			}
	    	
		}
		vgm_src_close(&vgm);
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

	
int main(int n,char ** argv)
{
	int com_num=4;
    int loop=8;
	const char * good[]={"jy.vgm","pal.vgm","doom.vgm","ultima.vgm","sandy.vgm","queen.vgm","axe.vgm","rain.vgm","zanac.vgm"};
	char vgmfile[1024]="";
	if (n>=2) strcpy(vgmfile,argv[1]); else printf("Example:%s sandy.vgm com%d\n",argv[0],com_num);
	if (n>=3) sscanf(argv[2],"com%d",&com_num);
	if (n>=4) sscanf(argv[3],"%f",&speed);
	
	timing_init();
	printf("open com%d \n",com_num);
	while((COM1 = serial_init(com_num, 115200, 8, 1 , 0))==NULL   && com_num<12)  {
		com_num++;
	}
    

    while(loop){
    	
    //	if(n==1) strcpy(vgmfile,good[loop]);
    	gotoxy(1,5);
    		    printf("loop:%d\n",loop);
    	
			    DIR *pdir;
				struct dirent *pent;
				if(strlen(vgmfile)>0 && isVgmFile(vgmfile)) playFile(vgmfile);
				else{
					if(strlen(vgmfile)==0){
						strcpy(vgmfile,".");
					}
					printf ("open %s ... \n",vgmfile);
					pdir=opendir(vgmfile); //opendir for playlist
					if (!pdir){
						printf ("opendir() failure; terminating");
						exit(1);
					}
					errno=0;
				
						while ((pent=readdir(pdir))){
							
							if(isVgmFile(pent->d_name)){
								gotoxy(1,3);
								printf("find ... %s                           \n", pent->d_name);
								char filepath[1024];
								sprintf(filepath,"%s\\%s",vgmfile,pent->d_name);
								playFile(filepath);
							}
							
						}
					if (errno){
					printf ("readdir() failure; terminating");
					exit(1);
					}
					closedir(pdir);
				
				}
    
		loop--;
   }
reset();
    printf("OPL3 usb serial drive test. \n");
  
    timing_exit();
    return 0;
}
