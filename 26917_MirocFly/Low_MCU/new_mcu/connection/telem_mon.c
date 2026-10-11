/*
 * telem_mon.c - 只读遥测监控 (LicheeRV, 读 FC 的 ATTITUDE/RAW_IMU, 不发任何控制)
 * 用途: 飞行时实时看 roll/pitch/yaw/acc; 只发 MSP 读请求, 不影响飞行。
 * 用法: ./telem_mon [serial_dev] [baud]
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/select.h>
#include <time.h>

#define MSP_RAW_IMU 102
#define MSP_ATTITUDE 108

static speed_t baud_of(int b){
	switch(b){case 9600:return B9600;case 19200:return B19200;case 38400:return B38400;
	case 57600:return B57600;case 115200:return B115200;case 230400:return B230400;
	case 460800:return B460800;case 921600:return B921600;default:return B230400;}
}
static int ser_open(const char*dev,int baud){
	int fd=open(dev,O_RDWR|O_NOCTTY); if(fd<0)return -1;
	struct termios t; if(tcgetattr(fd,&t)){close(fd);return -1;}
	cfmakeraw(&t); t.c_cflag|=(CLOCAL|CREAD); t.c_cflag&=~CRTSCTS;
	t.c_cc[VMIN]=0; t.c_cc[VTIME]=1;
	speed_t sp=baud_of(baud);
	if(cfsetispeed(&t,sp)||cfsetospeed(&t,sp)){close(fd);return -1;}
	tcsetattr(fd,TCSANOW,&t); tcflush(fd,TCIOFLUSH); return fd;
}
static void req(int fd,uint8_t cmd){
	uint8_t f[6]={'$','M','<',0,cmd,0}; f[5]=0^cmd; write(fd,f,6);
}
/* 解析器 */
static int st=0; static uint8_t dir,siz,cmd,data[256],idx,cs;
static int feed(uint8_t b,uint8_t*oc,uint8_t*op,uint8_t*ol){
	switch(st){case 0:if(b=='$')st=1;break;
	case 1:st=(b=='M')?2:0;break;
	case 2:dir=b;st=3;break;
	case 3:siz=b;cs=b;st=4;break;
	case 4:cmd=b;cs^=b;idx=0;st=(siz>0)?5:6;break;
	case 5:data[idx++]=b;cs^=b;if(idx>=siz)st=6;break;
	case 6:st=0;if(b==cs){*oc=cmd;*ol=siz;memcpy(op,data,siz);return 1;}break;}
	return 0;
}
static double now_s(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
int main(int argc,char**argv){
	const char*dev=argc>1?argv[1]:"/dev/ttyS0"; int baud=argc>2?atoi(argv[2]):230400;
	setvbuf(stdout,NULL,_IONBF,0);
	int fd=ser_open(dev,baud); if(fd<0){fprintf(stderr,"open %s failed\n",dev);return 1;}
	printf("telem_mon %s @ %d (只读)\n",dev,baud);
	double t0=now_s(),tp=t0; int roll=0,pitch=0,yaw=0,ax=0,ay=0,az=0;
	while(1){
		req(fd,MSP_ATTITUDE); req(fd,MSP_RAW_IMU);
		fd_set rf;FD_ZERO(&rf);FD_SET(fd,&rf);struct timeval tv={0,8000};
		if(select(fd+1,&rf,0,0,&tv)>0){
			uint8_t b[512];int n=read(fd,b,sizeof b);
			for(int i=0;i<n;i++){uint8_t c,p[256],l;
				if(!feed(b[i],&c,&p[0],&l))continue;
				if(c==MSP_ATTITUDE&&l>=6){roll=(int16_t)(p[0]|(p[1]<<8));pitch=(int16_t)(p[2]|(p[3]<<8));yaw=(int16_t)(p[4]|(p[5]<<8));}
				else if(c==MSP_RAW_IMU&&l>=6){ax=(int16_t)(p[0]|(p[1]<<8));ay=(int16_t)(p[2]|(p[3]<<8));az=(int16_t)(p[4]|(p[5]<<8));}
			}
		}
		double t=now_s();
		if(t-tp>=0.1){tp=t;double mag=sqrt((double)ax*ax+(double)ay*ay+(double)az*az);
			printf("[%6.1fs] att r=%6.1f p=%6.1f y=%6.1f | acc(%5d,%5d,%5d) |a|=%.2fg\n",
				t-t0,roll/10.0,pitch/10.0,yaw/10.0,ax,ay,az,mag/512.0);}
		usleep(20000);
	}
	close(fd);return 0;
}
