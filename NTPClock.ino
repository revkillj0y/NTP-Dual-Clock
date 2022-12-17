#include <ezTime.h>
#include <M5StackUpdater.h>

#include <M5Core2.h>





/**************************************************************************
       Title:   NTP Dual Clock
      Author:   Bruce E. Hall, w8bh.net
        Date:   02 Dec 2020
    Hardware:   Adafruit ESP32 Feather, ILI9341 TFT display module
    Software:   Arduino IDE 1.8.13 with Expressif ESP32 package 
                TFT_eSPI Library
                ezTime Library
       Legal:   Copyright (c) 2020  Bruce E. Hall.
                Open Source under the terms of the MIT License. 
                    
 Description:   Dual UTC/Local NTP Clock with TFT display 
                Time is refreshed via NTP every 30 minutes
                Optional time output to serial port 
                Status indicator for time freshness & WiFi strength
                Before using, please update WIFI_SSID and WIFI_PWD
                with your personal WiFi credentials.  Also, modify
                TZ_RULE with your own Posix timezone string.
                see w8bh.net for a detailled, step-by-step tutorial
  Version Hx:   11/27/20  Initial GitHub commit
                11/28/20  added code to handle dropped WiFi connection
                11/30/20  showTimeDate() mod by John Price (WA2FZW)
                12/01/20  showAMPM() added by John Price (WA2FZW)
        
    Modified:   By Ken Simmons, WR7D
                12/16/20 For use with M5Stack Core module
               
 **************************************************************************/



#include <ezTime.h>                                // https://github.com/ropg/ezTime
#include <WiFi.h>

#define TITLE              "NTP TIME"
#define WIFI_SSID          "WIFI_SSID"               
#define WIFI_PWD           "WIFI_PWD"      
#define NTP_SERVER         "pool.ntp.org"          // time.nist.gov, pool.ntp.org, etc    
    
#define TZ_RULE            "EST5EDT,M3.2.0/2:00:00,M11.1.0/2:00:00"

#define DEBUGLEVEL            INFO                 // NONE, ERROR, INFO, or DEBUG
#define PRINTED_TIME             1                 // 0=NONE, 1=UTC, or 2=LOCAL
#define TIME_FORMAT         COOKIE                 // COOKIE, ISO8601, RFC822, RFC850, RFC3339, RSS
#define BAUDRATE            115200                 // serial output baudrate
#define LEADING_ZERO         false                 // show "01:00" vs " 1:00"
#define SYNC_MARGINAL         3600                 // orange status if no sync for 1 hour
#define SYNC_LOST            86400                 // red status if no sync for 1 day   
#define LOCAL_FORMAT_12HR     true                 // local time format 12hr "11:34" vs 24hr "23:34"
#define UTC_FORMAT_12HR      false                 // UTC time format 12 hr "11:34" vs 24hr "23:34"
#define DISPLAY_AMPM          true                 // if true, show 'A' for AM, 'P' for PM
#define SCREEN_ORIENTATION       1                 // screen portrait mode:  use 1 or 3
#define TIMECOLOR         TFT_CYAN                 // color of 7-segment time display
#define DATECOLOR       TFT_YELLOW                 // color of displayed month & day
#define LABEL_FGCOLOR   TFT_YELLOW                 // color of label text
#define LABEL_BGCOLOR     TFT_BLUE                 // color of label background


// ============ GLOBAL VARIABLES =====================================================

//TFT_eSPI tft = TFT_eSPI();                         // display object 
Timezone local;                                    // local timezone variable
time_t t,oldT;                                     // current & displayed UTC
time_t lt,oldLt;                                   // current & displayed local time  
bool useLocalTime  = false;                        // temp flag used for display updates


// ============ DISPLAY ROUTINES =====================================================

void showClockStatus() {
  const int x=290,y=1,w=28,h=29,f=2;               // screen position & size
  int color;
  if (second()%10) return;                         // update every 10 seconds
  int syncAge = now()-lastNtpUpdateTime();         // how long has it been since last sync?
  if (syncAge < SYNC_MARGINAL)                     // GREEN: time is good & in sync
    color = TFT_GREEN;
  else if (syncAge < SYNC_LOST)                    // ORANGE: sync is 1-24 hours old
    color = TFT_ORANGE;
  else color = TFT_RED;                            // RED: time is stale, over 24 hrs old
  if (WiFi.status()!=WL_CONNECTED) {               //          
    color = TFT_DARKGREY;                          // GRAY: WiFi connection was lost
    WiFi.disconnect();                             // so drop current connection
    WiFi.begin(WIFI_SSID,WIFI_PWD);                // and attempt to reconnect
  }
  M5.Lcd.fillRoundRect(x,y,w,h,10,color);             // show clock status as a color
  M5.Lcd.setTextColor(TFT_BLACK,color);
  M5.Lcd.drawNumber(-WiFi.RSSI(),x+8,y+6,f);          // WiFi strength as a positive value
}

/*
 *  Modified by John Price (WA2FZW)
 *
 *    In the original code, this was an empty function. I added code to display either
 *    an 'A' or 'P' to the right of the local time 
 *
 */

void showAMPM ( int hr, int x, int y )
{
  char  ampm;                                      // Will be either 'A' or 'P'
  if ( hr <= 11 )                                  // If the hour is 11 or less
    ampm = 'A';                                    // It's morning
  else                                             // Otherwise,
    ampm = 'P';                                    // It's afternoon
  M5.Lcd.drawChar ( ampm, x, y, 4 );                  // Show AM/PM indicator 
}

void showTime(time_t t, bool hr12, int x, int y) {
  const int f=7;                                   // screen font
  M5.Lcd.setTextColor(TIMECOLOR, TFT_BLACK);       // set time color
  int h=hour(t); int m=minute(t); int s=second(t); // get hours, minutes, and seconds
  if (hr12) {                                      // if using 12hr time format,
    if (DISPLAY_AMPM) showAMPM(h,x+220,y+14);      // show AM/PM indicator
    if (h==0) h=12;                                // 00:00 becomes 12:00
    if (h>12) h-=12;                               // 13:00 becomes 01:00
  }
  if (h<10) {                                      // is hour a single digit?
    if ((!hr12)||(LEADING_ZERO))                   // 24hr format: always use leading 0
      x+= M5.Lcd.drawChar('0',x,y,f);              // show leading zero for hours
    else {
      M5.Lcd.setTextColor(TFT_BLACK,TFT_BLACK);    // black on black text     
      x+=M5.Lcd.drawChar('8',x,y,f);               // will erase the old digit
      M5.Lcd.setTextColor(TIMECOLOR,TFT_BLACK);      
    }
  }
  x+= M5.Lcd.drawNumber(h,x,y,f);                  // hours
  x+= M5.Lcd.drawChar(':',x,y,f);                  // show ":"
  if (m<10) x+= M5.Lcd.drawChar('0',x,y,f);        // leading zero for minutes
  x+= M5.Lcd.drawNumber(m,x,y,f);                  // show minutes          
  x+= M5.Lcd.drawChar(':',x,y,f);                  // show ":"
  if (s<10) x+= M5.Lcd.drawChar('0',x,y,f);        // add leading zero for seconds
  x+= M5.Lcd.drawNumber(s,x,y,f);                  // show seconds
}

void showDate(time_t t, int x, int y) {
  const int f=4,yspacing=30;                       // screen font, spacing
  const char* months[] = {"JAN","FEB","MAR",
     "APR","MAY","JUN","JUL","AUG","SEP","OCT",
     "NOV","DEC"};
  M5.Lcd.setTextColor(DATECOLOR, TFT_BLACK);
  int m=month(t), d=day(t);                        // get date components  
  M5.Lcd.fillRect(x,y,50,60,TFT_BLACK);               // erase previous date       
  M5.Lcd.drawString(months[m-1],x,y,f);               // show month on top
  y += yspacing;                                   // put day below month
  if (d<10) x+=M5.Lcd.drawNumber(0,x,y,f);            // draw leading zero for day
  M5.Lcd.drawNumber(d,x,y,f);                         // draw day
}

void showTimeZone (int x, int y) {
  const int f=4;                                   // text font
  M5.Lcd.setTextColor(LABEL_FGCOLOR,LABEL_BGCOLOR);   // set text colors
  M5.Lcd.fillRect(x,y,80,28,LABEL_BGCOLOR);           // erase previous TZ
  if (!useLocalTime) 
    M5.Lcd.drawString("UTC",x,y,f);                   // UTC time
  else 
    M5.Lcd.drawString(local.getTimezoneName(),x,y,f); // show local time zone
}

void showTimeDate(time_t t, time_t oldT, bool hr12, int x, int y) {   
  showTime(t,hr12,x,y);                            // display time HH:MM:SS 
  if ((!oldT)||(hour(t)!=hour(oldT)))              // did hour change?
    showTimeZone(x,y-42);                          // update time zone
  if ((!oldT)||(day(t)!=day(oldT)))                // did date change? (Thanks John WA2FZW!)
    showDate(t,x+250,y);                           // update date
}

void updateDisplay() {
  t = now();                                       // check latest time
  if (t!=oldT) {                                   // are we in a new second yet?
    lt = local.now();                              // keep local time current
    useLocalTime = true;                           // use local timezone
    showTimeDate(lt,oldLt,LOCAL_FORMAT_12HR,10,46);// show new local time
    useLocalTime = false;                          // use UTC timezone
    showTimeDate(t,oldT,UTC_FORMAT_12HR,10,172);   // show new UTC time
    showClockStatus();                             // and clock status
    printTime();                                   // send timestamp to serial port
    oldT=t; oldLt=lt;                              // remember currently displayed time
  }
}

void newDualScreen() {
  M5.Lcd.fillScreen(TFT_BLACK);                       // start with empty screen
  M5.Lcd.fillRoundRect(0,0,319,32,10,LABEL_BGCOLOR);  // title bar for local time
  M5.Lcd.fillRoundRect(0,126,319,32,10,LABEL_BGCOLOR);// title bar for UTC
  M5.Lcd.setTextColor(LABEL_FGCOLOR,LABEL_BGCOLOR);   // set label colors
  M5.Lcd.drawCentreString(TITLE,160,4,4);             // show title at top
  M5.Lcd.drawRoundRect(0,0,319,110,10,TFT_WHITE);     // draw edge around local time
  M5.Lcd.drawRoundRect(0,126,319,110,10,TFT_WHITE);   // draw edge around UTC
}

void startupScreen() {  
  M5.Lcd.fillScreen(TFT_BLACK);                       // start with empty screen
  M5.Lcd.fillRoundRect(0,0,319,30,10,LABEL_BGCOLOR);  // title bar
  M5.Lcd.drawRoundRect(0,0,319,239,10,TFT_WHITE);     // draw edge screen
  M5.Lcd.setTextColor(LABEL_FGCOLOR,LABEL_BGCOLOR);   // set label colors
  M5.Lcd.drawCentreString(TITLE,160,2,4);             // show sketch title on screen
  M5.Lcd.setTextColor(LABEL_FGCOLOR, TFT_BLACK);      // set text color
}


// ============ MISC ROUTINES =====================================================

void showConnectionProgress(){  
  int elapsed = 0;           
  M5.Lcd.drawString("WiFi starting",5,50,4);                   
  while (WiFi.status()!=WL_CONNECTED) {            // while waiting for connection                             
    M5.Lcd.drawNumber(elapsed++,230,50,4);            // show we are trying!
    delay(1000);
  }
  M5.Lcd.drawString("IP = " +                         // connected to LAN now
    WiFi.localIP().toString(),5,80,4);             // so show IP address 
  elapsed = 0;
  M5.Lcd.drawString("Waiting for NTP",5,140,4);       // Now get NTP info
  while (timeStatus()!=timeSet) {                  // wait until time retrieved
    events();                                      // allow ezTime to work
    M5.Lcd.drawNumber(elapsed++,230,140,4);           // show we are trying
    delay(1000);
  }
}

void printTime() {                                 // print time to serial port
  if (!PRINTED_TIME) return;                       // option 0: dont print
  Serial.print("TIME: ");
  if (PRINTED_TIME==1)
    Serial.println(dateTime(TIME_FORMAT));         // option 1: print UTC time
  else 
    Serial.println(local.dateTime(TIME_FORMAT));   // option 2: print local time
}


// ============ MAIN PROGRAM ===================================================

void setup() 
{
  M5.begin();
  
  Serial.begin(BAUDRATE);                          // open serial port
  checkSDUpdater( SD, MENU_BIN, 5000, TFCARD_CS_PIN // (usually default=4 but your mileage may vary)
  );  
  setDebug(DEBUGLEVEL);                            // enable NTP debug info
  setServer(NTP_SERVER);                           // set NTP server
  WiFi.begin(WIFI_SSID, WIFI_PWD);                 // start WiFi
  showConnectionProgress();                        // WiFi and NTP may take time
  local.setPosix(TZ_RULE);                         // estab. local TZ by rule 
  newDualScreen();                                 // show title & labels 
}

void loop() {
  events();                                        // get periodic NTP updates
  updateDisplay();                                 // update clock every second
}
