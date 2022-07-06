#include <windows.h>
#include <cstring>
#include <thread>
#include <string>
#include <stdio.h>
#include <shlobj.h>
#include "serial.h"
#include <dirent.h>
#include <windows.h>
#include <conio.h> 
#include <winuser.h>
#define IDI_ICON1 101
#define KEY_UP     72  //? 
#define KEY_DOWN    80  //? 
#define ESC       0x1B 
using namespace std;
static HWND 	hwndEditbox; 
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

#define VGM_HEADER_LEN (0x80)
#define SAMPLE_RATE (44100)
PORT COM1;
float speed = SAMPLE_RATE/990.0f;
static thread * thPlay=NULL;
  

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
    int wait=0;
void playFile(char * vgmfile){

    char op[3]={0};
	char key=0;
	char started=0;
	int gd3offset=0;
	char gd3info[4096];
	int gd3len=0;
	gotoxy(1,6);
		
	char vgm_header[VGM_HEADER_LEN]={0};
	

		printf("playing: %s                       \n",vgmfile);
	
	    printf("speed:%f\n",speed);
	    FILE * vgm=fopen(vgmfile,"rb");
		wait=0;
	    if(vgm!=NULL){
	    	fread(vgm_header,VGM_HEADER_LEN,1,vgm);
	    	printf("data offset:%02X\n",(vgm_header[0x34]&0xff)+0x34);
	    	//printf("OPL2:%02X %02X %02X \n",vgm_header[0x50]&0xff,vgm_header[0x51]&0xff,vgm_header[0x52]&0xff);
	    	printf("OPL2:%d Hz                \n",le_int(&vgm_header[0x50]));
	    	gd3offset=le_int(&vgm_header[0x14]);
	    	if(gd3offset>0){	
				fseek(vgm,gd3offset+0x14,SEEK_SET);
	    		gd3len=fread(gd3info,1,4096,vgm);
	    		int base=12;
	    		int i=0,j=0;
	    		for(i=base;i<gd3len;i++){
					if(gd3info[i]!=0){
						gd3info[j++]=gd3info[i];
					}else if(gd3info[i+1]==0&&gd3info[j-1]!='\n'){
						gd3info[j++]='\r';
						gd3info[j++]='\n';
					}
				}
				gd3info[i]=0;
	    		SetWindowText(hwndEditbox,gd3info);
			}
	    
	    	reset();
	    	if(le_int(&vgm_header[0x50])) OPL3off();
	    	//printf("OPL3:%02X %02X %02X \n",vgm_header[0x5c],vgm_header[0x5d],vgm_header[0x5e]&0xff);
	    	printf("OPL3:%d Hz                \n",vgm_header[0x5c]+(vgm_header[0x5d]<<8)+((vgm_header[0x5e]&0xff)<<16));
	    	fseek(vgm,(vgm_header[0x34]&0xff)+0x34,SEEK_SET);
	    	int count=0;
	    	printf("Up/Down : speed ... Esc: next ... Ctrl+C: Stop \n");
	    	started=0;
	    	while(fread(op,3,1,vgm)==1){
	    	
	    		if(op[0]==0x5e || op[0]==0x5f || op[0]==0x5A ){
	    		   OPL3write(op);
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
				   if (started && wait>=speed) {
					   	//printf(" %d",wait/44);
					   	Sleep(wait/speed);
					   //	wait=wait % (int)speed;
				   }
				   	
				}
	    		if(wait<0) break;
				   	
			}
	    	fclose(vgm);
	    	reset();
		}else{
			SetWindowText(hwndEditbox, "Open failed!");
								
		}
	
}
int isVgmFile(const char fname[])
{
	int l = strlen(fname);
	if (l <= 4 || stricmp(fname + l - 4, ".vgm") != 0)
		return 0;
	else
		return l - 3;
}

char vgmfile[1024]=".";	
void play(){
	   DIR *pdir;
				struct dirent *pent;
				if(strlen(vgmfile)>0 && isVgmFile(vgmfile)) playFile(vgmfile);
				else{
					if(strlen(vgmfile)==0){
						strcpy(vgmfile,".");
					}
					SetWindowText(hwndEditbox,(vgmfile));
					pdir=opendir(vgmfile); //opendir for playlist
					if (!pdir){
						printf ("opendir() failure; terminating");
						exit(1);
					}
					errno=0;
						wait=0;
						while ((pent=readdir(pdir))){
							
							if(isVgmFile(pent->d_name)){
								gotoxy(1,3);
								SetWindowText(hwndEditbox, pent->d_name);
								char filepath[1024];
								sprintf(filepath,"%s\\%s",vgmfile,pent->d_name);
								playFile(filepath);
							}
							if(wait==-2) break;
							
						}
					if (errno){
					printf ("readdir() failure; terminating");
					exit(1);
					}
					closedir(pdir);
				
				}
} 
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
	WNDCLASSEX wc;
	HWND hwnd, hwndDT;
	MSG msg;
	RECT rect;
	int dtWidth,dtHeight;
 
	memset(&wc,0,sizeof(wc));
	wc.cbSize		 = sizeof(WNDCLASSEX);
	wc.lpfnWndProc	 = WndProc;
	wc.hInstance	 = hInstance;
	wc.hCursor		 = LoadCursor(NULL, IDC_ARROW);
	
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
	wc.lpszClassName = "WindowClass";
	wc.hIcon		 = LoadIcon(hInstance,IDI_APPLICATION);
	wc.hIconSm		 = LoadIcon(NULL, IDI_APPLICATION);
	int com_num=4;
    printf("open com%d \n",com_num);
	while((COM1 = serial_init(com_num, 115200, 8, 1 , 0))==NULL   && com_num<12)  {
		com_num++;
	}
	if(!RegisterClassEx(&wc)) {
		MessageBox(NULL, "Window Registration Failed!","Error!",MB_ICONEXCLAMATION|MB_OK);
		return 0;
	}
 
	hwndDT=GetDesktopWindow(); //取桌面句柄 
	GetWindowRect(hwndDT,&rect); //取桌面范围 
	dtWidth=rect.right-rect.left; //桌面宽度 
	dtHeight=rect.bottom-rect.top; //桌面高度 
	
	hwnd = CreateWindowEx(WS_EX_CLIENTEDGE,wc.lpszClassName,
		"OPL3USB VGM Player",WS_VISIBLE|WS_OVERLAPPEDWINDOW,
		(dtWidth-240)/2,   /*窗体居中*/ 
		(dtHeight-160)/2,
		315,
		200,
		NULL,NULL,hInstance,NULL);
	//MoveWindow(hwnd, (width-640)/2, (height-480)/2, 640, 480, FALSE);
    
	if(hwnd == NULL) {
		MessageBox(NULL, "Window Creation Failed!","Error!", MB_ICONEXCLAMATION|MB_OK);
		return 0;
	}
	while(GetMessage(&msg, NULL, 0, 0) > 0) { 
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
 
	return msg.wParam;
}
void start(){
	if(thPlay!=NULL ){
		wait=-2;
		thPlay=NULL;
		//thPlay->join();
		Sleep(1000);
		thPlay=new thread(play);
    	thPlay->detach();
	}else{	//
    	thPlay=new thread(play);
    	thPlay->detach();
    }
}
void load(){
	TCHAR szPathName[4096];
	 BROWSEINFO bInfo={0};
	 bInfo.hwndOwner=GetForegroundWindow();//???
	 bInfo.lpszTitle=TEXT("?????```?????");
	 bInfo.ulFlags=BIF_RETURNONLYFSDIRS |BIF_USENEWUI/*??????? ?????????? ????????????..*/|
					BIF_UAHINT/*?TIPS??*/ |BIF_NONEWFOLDERBUTTON /*?????????*/;
					
		//????? ulFlags ?? http://msdn.microsoft.com/en-us/library/bb773205(v=vs.85).aspx
	 LPITEMIDLIST lpDlist;
	 lpDlist=SHBrowseForFolder(&bInfo);
	 if (lpDlist!=NULL)//???????
	 {
		SHGetPathFromIDList(lpDlist,szPathName);
		SetWindowText(hwndEditbox,szPathName);
		strcpy(vgmfile,szPathName);
		start();
	 }
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

	string			strXy;
	char			x[5], y[5];
	char			buff[4096] = {0};
	const int		IDcmdButton = 1;
	const int		IDeditBox = 2;
	const int		IDPlayButton = 3;
	const int		IDNextButton = 4;
	const int		IDLoadButton = 5;
  
    
	cxChar = LOWORD(GetDialogBaseUnits());
	cyChar = HIWORD(GetDialogBaseUnits());
    int height=8;
	switch (message) {
		case WM_CREATE:
			hwndEditbox = CreateWindow( TEXT("edit"),NULL,
										WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE,
                                   		cxChar * 1, cyChar * 0.2,
                                   		35 * cxChar, (-0.5+height) * cyChar,
										hwnd,(HMENU)IDeditBox,NULL,NULL);
			hwndPlay = CreateWindow ( TEXT("button"), TEXT("Play"),
                            			WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                   		cxChar * 2, cyChar * height,
                                   		5 * cxChar, 1.5 * cyChar,
                                   		hwnd, (HMENU)IDPlayButton, ((LPCREATESTRUCT) lParam)->hInstance, NULL);
			hwndNext = CreateWindow ( TEXT("button"), TEXT("Next"),
                            			WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                   		cxChar * 12, cyChar * height,
                                   		5 * cxChar, 1.5 * cyChar,
                                   		hwnd, (HMENU)IDNextButton, ((LPCREATESTRUCT) lParam)->hInstance, NULL);
			hwndLoad = CreateWindow ( TEXT("button"), TEXT("Load"),
                            			WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                   		cxChar * 22, cyChar * height,
                                   		5 * cxChar, 1.5 * cyChar,
                                   		hwnd, (HMENU)IDLoadButton, ((LPCREATESTRUCT) lParam)->hInstance, NULL);
			if (!hwndPlay) MessageBox(NULL,"创建按钮失败","Message",MB_OK|MB_ICONERROR);
			ShowWindow(hwndPlay,SW_SHOW);
            UpdateWindow(hwndPlay);
			ShowWindow(hwndNext,SW_SHOW);
            UpdateWindow(hwndNext);
				
            
										
			if (!hwndEditbox) MessageBox(NULL,"创建文本框失败","Message",MB_OK|MB_ICONERROR);
			ShowWindow(hwndEditbox,SW_SHOW);
            UpdateWindow(hwndEditbox);
            
        	return 0 ;
	          
	    case WM_PAINT:
			
			
			hdc = BeginPaint (hwnd, &ps);
	    	//SetRect(&rect, 10, 10, 300, 200);
    	   // FrameRect(hdc, &rect, (HBRUSH)GetStockObject(BLACK_BRUSH));
			//GetClientRect (hwnd, &rect);
			//rect.left += 20;
            //rect.top += 2;
            //SetTextColor(hdc, RGB(255,0,0)); //可以用RGB三原色设置文本颜色
			//DrawText(hdc, TEXT(" Hello, Dev-C++! "), -1, &rect, DT_SINGLELINE | DT_TOP | DT_LEFT);
			EndPaint(hwnd, &ps);
			return 0;
 
	    case WM_SIZE:
			//GetWindowRect(hwnd, &rect);
			//MoveWindow(hwndPlay, (rect.right-rect.left)/2 - 15*cxChar ,\
			//(rect.bottom-rect.top)/2 - 1.25*cxChar, 30*cxChar, 2.5*cyChar, FALSE);
			return 0;
			
		case WM_COMMAND:
			//各控件的_click()事件 
			switch (LOWORD(wParam)) {
			case 0:
				PostQuitMessage(0);
				break;
			case IDPlayButton: 
			    start();
				
				break;
			case IDNextButton: 
			    if(thPlay!=NULL){
			    	
			    	wait=-1;
				}
				
				break;
			case IDLoadButton: 
				load();
			    		
				break;
			case IDeditBox:
				GetWindowText(hwndEditbox,buff,4096);
				break;
			}
			return 0; 
 
		case WM_LBUTTONDOWN: // WM_LBUTTONDOWN是鼠标左键按下的事件
			GetCursorPos(&mouse);
			GetWindowRect(hwnd, &rect);
			mX=mouse.x-rect.left;
			mY=mouse.y-rect.top;
			itoa(mX,x,10);
			itoa(mY,y,10);
			strXy="鼠标点击的窗体内坐标：("+string(x)+","+string(y)+")";
			SetWindowText(hwndEditbox,strXy.c_str());
			//MessageBox(NULL, strXy.c_str(), "", MB_ICONASTERISK);
			return 0;
 
		case WM_CLOSE:
			if (IDYES==MessageBox(hwnd, "是否真的要退出？", "确认", MB_ICONQUESTION | MB_YESNO))
				DestroyWindow(hwnd);  //销毁窗口
			return 0;
		  
		case WM_DESTROY:
			//ShellAbout(hwnd, "第一个窗口程序", "再见，期待您在评论区留言！", NULL);
			thPlay->join();
			PostQuitMessage(0);
			
			return 0;
	}
	
	return DefWindowProc(hwnd, message, wParam, lParam);
}
