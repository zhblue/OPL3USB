#include <stdio.h>
#include "serial.h"
#include <windows.h>
#include <conio.h> 
#define KEY_UP     72  //? 
#define KEY_DOWN    80  //? 
#define ESC       0x1B 
 
#define VGM_HEADER_LEN (0x80)
#define SAMPLE_RATE (44100)
PORT COM1;
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
    //OPL3off();
}
void nop(){
	for(int i=0;i<1000;i++);
}
int main(int n,char ** argv)
{
	int com_num=4;
    long long wait=0;
	
	float speed = SAMPLE_RATE/900.0f;
	char vgm_header[VGM_HEADER_LEN]={0};
	int loop=8;
	const char * good[]={"jy.vgm","pal.vgm","doom.vgm","ultima.vgm","sandy.vgm","queen.vgm","axe.vgm","rain.vgm","zanac.vgm"};
	const char * vgmfile="jy.vgm";
	if (n>=2) vgmfile=argv[1]; else printf("Example:%s %s com%d\n",argv[0],vgmfile,com_num);
	if (n>=3) sscanf(argv[2],"com%d",&com_num);
	if (n>=4) sscanf(argv[3],"%f",&speed);
	
	printf("open com%d \n",com_num);
	COM1 = serial_init(com_num, 115200, 8, 1 , 0);
    
	char op[3]={0};
	char key=0;
	reset();
    while(loop){
    	if(n==1)vgmfile=good[loop];
    	printf("playing: %s com%d\n",vgmfile,com_num);
		printf("loop:%d\n",loop);
	    printf("speed:%f\n",speed);
	    FILE * vgm=fopen(vgmfile,"rb");
		wait=0;
	    if(vgm!=NULL){
	    	fread(vgm_header,VGM_HEADER_LEN,1,vgm);
	    	printf("data offset:%02X\n",(vgm_header[0x34]&0xff)+0x34);
	    	//printf("OPL2:%02X %02X %02X \n",vgm_header[0x50]&0xff,vgm_header[0x51]&0xff,vgm_header[0x52]&0xff);
	    	printf("OPL2:%d Hz \n",ld_int(&vgm_header[0x50]));
	    	if(ld_int(&vgm_header[0x50]))  OPL3off();
	    	//printf("OPL3:%02X %02X %02X \n",vgm_header[0x5c],vgm_header[0x5d],vgm_header[0x5e]&0xff);
	    	printf("OPL3:%d Hz \n",vgm_header[0x5c]+(vgm_header[0x5d]<<8)+((vgm_header[0x5e]&0xff)<<16));
	    	fseek(vgm,(vgm_header[0x34]&0xff)+0x34,SEEK_SET);
	    	int count=0;
	    	printf("UP/Down Arrow control speed ...\n");
	    	while(fread(op,3,1,vgm)==1){
	    	if(_kbhit()){
	    		key = getch();
	    		if(key==KEY_UP) speed+=1;
	    		if(key==KEY_DOWN) speed-=1;
	    		if(key==ESC) wait=-1;
	    		key=0;
	    		//delline();
	    		 printf("speed:%f\n",speed);
			}	
	    		if(op[0]==0x5e || op[0]==0x5f || op[0]==0x5A ){
	    		   OPL3write(op);
	    		   count++;
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
	    		   			fseek(vgm,-2,SEEK_CUR);
	    		   			break;
	    		   		case 0x63:
	    		   			wait=882;
	    		   		    fseek(vgm,-2,SEEK_CUR);
	    		   			break;
	    		   		case 0x66:
						    wait=-1;
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
	    		   			fseek(vgm,-2,SEEK_CUR);
	    		   			break;
	    		   		   	
	    		   		default: wait=0; 
				   }
				   if (wait>=speed) {
					   	//printf(" %d",wait/44);
					   	Sleep(wait/speed);
					   //	wait=wait % (int)speed;
				   }
				   	
				}
	    		if(wait<0) break;
				   	
			}
	    	
		}
		fclose(vgm);
		loop--;
}
reset();
    printf("OPL3 usb serial drive test. \n");
  
    return 0;
}

