//The connections are:
//  PA2:  SDA
//  PA3:  SCL

#include "variables.h"
#include "touchpanel.h"
#include "DS3231.h"

//*****************************************************************************
//*****************************************************************************
void readtime(void)                     //Read time for display to main screen
	{
#if !PORT_1014D
      uint8 buffertimeX[3];
      uint8 jedsec,jedmin,jedhod,dessec,desmin,deshod;

      if (onoffRTC) 
      { tp_i2c_read_data(RTC_DEVICE_ADDR, 0x00, buffertimeX, 3);
        
        dessec=buffertimeX[0]>>4;
        dessec&=7;
        jedsec=buffertimeX[0]&15;

        desmin=buffertimeX[1]>>4;
        desmin&=7;
        jedmin=buffertimeX[1]&15;

        deshod=buffertimeX[2]>>4;
        deshod&=3;
        jedhod=buffertimeX[2]&15;
      }
      else 
      {
        deshod = hour / 10;
        jedhod = hour % 10;

        desmin = minute / 10;
        jedmin = minute % 10;

        dessec = sec / 10;
        jedsec = sec % 10;  
      }

      //if(deshod)buftime[0]=deshod+48; else buftime[0]=' ';
      buffertime[0]=deshod+48;
      buffertime[1]=jedhod+48;
      buffertime[2]=':';
      buffertime[3]=desmin+48;
      buffertime[4]=jedmin+48;
      buffertime[5]=':';
      buffertime[6]=dessec+48;
      buffertime[7]=jedsec+48;
      buffertime[8]=0;           //end of string   
#endif
	}
//------------------------------------------------------------------------------
//******************************************************************************
void settime (void)                     //Write time to DS3231
	{
#if !PORT_1014D
      //start register for writing
      uint8 RTC_CMD_REG = 0; //write to sec register
      uint8 buffertimeX[3];
        
      buffertimeX[0]=0;               //set 0sec
        
      buffertimeX[1]=(minute/10)<<4;  //set min
      buffertimeX[1]|=(minute%10);

      buffertimeX[2]=(hour/10)<<4;    //set hour
      buffertimeX[2]|=(hour%10);
        
      if(onoffRTC) 
      {
        tp_i2c_send_data(RTC_DEVICE_ADDR, RTC_CMD_REG, buffertimeX, 3);
      }
      return;
#endif
	}
//****************************************************************
//*************************  Send DATE  **************************
//****************************************************************
void setdate (void)                   //Write date to DS3231
	{
#if !PORT_1014D
      //start register for writing
      uint8 RTC_CMD_REG = 3;          //write to day register
      uint8 buffertimeX[4];
       
      buffertimeX[0]=6;               //set day
       
      buffertimeX[1]=(day/10)<<4;     //set date
      buffertimeX[1]|=(day%10);
        
      buffertimeX[2]=(month/10)<<4;   //set month
      buffertimeX[2]|=(month%10);

      buffertimeX[3]=(year/10)<<4;    //set year 20xx 00 to 99
      buffertimeX[3]|=(year%10);
        
      if(onoffRTC) 
      {
        tp_i2c_send_data(RTC_DEVICE_ADDR, RTC_CMD_REG, buffertimeX, 4);
      }
      return;
#endif
	}
//****************************************************************
//*************************  Send TIME ***************************
//****************************************************************
void hourUp (void)
	{
#if !PORT_1014D
      if (hour<23) hour++; else hour=0;
      settime();
      return;
#endif
	}

void hourDown (void)
	{
#if !PORT_1014D
	 if (hour>0) hour--; else hour=23;
     settime();
	 return;
#endif
	}
//--------------------------------------------------
void minuteUp (void)
	{
#if !PORT_1014D
	 if (minute<59) minute++; else minute=0;
     settime();
	 return;
#endif
	}

void minuteDown (void)
	{
#if !PORT_1014D
	 if (minute>0) minute--; else minute=59;
     settime();
	 return;
#endif
	}
//------------------------------------------------------------------------------
void dayUp (void)
    {
#if !PORT_1014D
     uint8 maxday = 31;      
     if ((month==4)|(month==6)|(month==9)|(month==11)) maxday=30;
     if (month==2) maxday=29;
    
     if (day<maxday) day++; else day=1;
     setdate();
     return;
#endif
    }
void dayDown (void)
    {
#if !PORT_1014D
     uint8 maxday = 31;      
     if ((month==4)|(month==6)|(month==9)|(month==11)) maxday=30;
     if (month==2) maxday=29;
    
     if (day>1) day--; else day=maxday;
     setdate();
     return;
#endif
    }
//--------------------------------------------------
void monthUp (void)
    {
#if !PORT_1014D
     if (month<12) month++; else month=1;
     setdate();
     return;
#endif
    }
void monthDown (void)
    {
#if !PORT_1014D
     if (month>1) month--; else month=12;
     setdate();
     return;
#endif
    }
//--------------------------------------------------
void yearUp (void)
    {
#if !PORT_1014D
    if (year<40) year++; else year=25;
    setdate();
    return;
#endif
    }
void yearDown (void)
    {
#if !PORT_1014D
    if (year>25) year--; else year=40;
    setdate();
    return;
#endif
    }
//------------------------------------------------------------------------------
//******************************************************************************
 void modnametoRTC ( int8 *d, int8 *s )
  {
#if !PORT_1014D
   while(*d) d++;
   *d=0;
   d++;
    while(*s) 
    {
       *d = *s;
       d++; 
       s++;
    }
    *d=0; 
#endif
  }
  
  void modnameClearRTC ( int8 *d )
  {
#if !PORT_1014D
   while(*d) d++;
   d++;
    while(*d) 
    {
      *d = 0;      //insert 0 null
      d++;     
    }
#endif
  }
//------------------------------------------------------------------------------
//****************************************************************************** 
void readnameRTC(void)                  //Read time for use time stamp in file
	{
#if !PORT_1014D
	uint8 buffertimeX[7];
    uint8 jedsec,jedmin,jedhod,dessec,desmin,deshod,dated,datej,monthd,monthj,yeard,yearj;

    tp_i2c_read_data(RTC_DEVICE_ADDR, 0x00, buffertimeX, 7);
  
	dessec=buffertimeX[0]>>4;
	dessec&=7;
	jedsec=buffertimeX[0]&15;

	desmin=buffertimeX[1]>>4;
	desmin&=7;
	jedmin=buffertimeX[1]&15;

	deshod=buffertimeX[2]>>4;
	deshod&=3;
    jedhod=buffertimeX[2]&15;
        
    //day=buftimeX[3]&7;
        
	dated=buffertimeX[4]>>4;
	dated&=3;
    datej=buffertimeX[4]&15;
        
	monthd=buffertimeX[5]>>4;
	monthd&=1;
    monthj=buffertimeX[5]&15;

    yeard=buffertimeX[6]>>4;
	yeard&=15;
    yearj=buffertimeX[6]&15;
        
    filenameRTC[0]= yeard+48;
    filenameRTC[1]= yearj+48;
    filenameRTC[2]=monthd+48;
    filenameRTC[3]=monthj+48;
    filenameRTC[4]= dated+48;
    filenameRTC[5]= datej+48; 
    filenameRTC[6]='-';
    filenameRTC[7]= deshod+48;
    filenameRTC[8]= jedhod+48;
    filenameRTC[9]= desmin+48;
    filenameRTC[10]=jedmin+48;     
    filenameRTC[11]=dessec+48;
    filenameRTC[12]=jedsec+48;
        
    filenameRTC[13]=0;           //end of string 
#endif
	}
//;-----------------------------------------------------------------------------
//******************************************************************************
void decodethumbnailfilename (int8 *s)
  {
#if !PORT_1014D
  uint8 x=0;
  uint8 dx=0;
   
  //finds 's'
  while(*s)
    {
    if (*s==115) {s=s+2;break;}   //find 115 is 's' and go 2positions right
    s++; 
    }

   //copy *s to filenameRTC  
  while(*s) 
    {
    filenameRTC[x] = *s;
    s++;
    x++;
    }
   
  s++;
  if  (*s!=0)  //file wihtout RTC, no write to display
    {
    s--;
    filenameRTC[x]=' ';
    x++;
    filenameRTC[x]='2';
    x++;
    filenameRTC[x]='0';
    x++;
    s++;
    }
   
   //copy *s to filenameRTC  
  while(*s) 
    {
    filenameRTC[x] = *s;
    if (dx==1) {x++;filenameRTC[x]='.';}
    if (dx==3) {x++;filenameRTC[x]='.';}
    if (dx==8) {x++;filenameRTC[x]=':';}
    if (dx==10) {x++;filenameRTC[x]=':';}
    s++;
    x++;
    dx++;
    }  
  filenameRTC[x++]=0;
#endif
  }
//;-----------------------------------------------------------------------------
//******************************************************************************
DWORD get_fattime (void)
  {
#if !PORT_1014D
    uint8   tm_sec = 0;
    uint8   tmp;
    uint8   bufferfattime[7];
    
    if(onoffRTC) 
    { //RTC on
      tp_i2c_read_data(RTC_DEVICE_ADDR, 0x00, bufferfattime, 7);
  
      tmp=(bufferfattime[0]>>4)&7;	
      tm_sec=(bufferfattime[0]&15)+(10*tmp);

      tmp=(bufferfattime[1]>>4)&7;	
      minute=(bufferfattime[1]&15)+(10*tmp);

      tmp=(bufferfattime[2]>>4)&3;	
      hour=(bufferfattime[2]&15)+(10*tmp);

      tmp=(bufferfattime[4]>>4)&3;	
      day=(bufferfattime[4]&15)+(10*tmp);

      tmp=(bufferfattime[5]>>4)&1;	
      month=(bufferfattime[5]&15)+(10*tmp);

      tmp=(bufferfattime[6]>>4)&15;	
      year=(bufferfattime[6]&15)+(10*tmp);
    } 

      //return date and time (FATtime original year-1980)
    return (DWORD)(year+20) << 25 |
           (DWORD)(month) << 21 |
           (DWORD)(day) << 16 |
           (DWORD)(hour) << 11 |
           (DWORD)minute << 5 |
           (DWORD)tm_sec >> 1;
#else
    //1014D firmware (2020-03-22 22:48:58) — 0 has month/day 0, an invalid FAT date that
    //PCs render as nonsense on every file the scope writes
    return 1349957149;
#endif
  }

//------------------------------------------------------------------------------
//******************************************************************************
uint8 readtemperature(void)     //Read temperature for scope_diagnostic_screen
  {
#if !PORT_1014D
    if(onoffRTC)
    {
      uint8 buffer[2];
      uint8 tMSB;

      tp_i2c_read_data(RTC_DEVICE_ADDR, 0x11, buffer, 2);
      tMSB = buffer[0];
      return tMSB;
    }
    return 99;
#else
    return 99;
#endif
  }
//;-----------------------------------------------------------------------------
//******************************************************************************
//******************************** End DS3231 **********************************
//******************************************************************************


