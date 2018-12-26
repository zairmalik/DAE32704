#include <UniversalTelegramBot.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <Wire.h>
#include <ArduCAM.h>
#include <SPI.h>
#include "memorysaver.h"

#if !(defined ESP8266)
#error Please select the ArduCAM ESP8266 UNO board in the Tools/Board
#endif

#if !(defined (OV2640_MINI_2MP)||defined (OV5640_MINI_5MP_PLUS) || defined (OV5642_MINI_5MP_PLUS) \
    || defined (OV5642_MINI_5MP) || defined (OV5642_MINI_5MP_BIT_ROTATION_FIXED) \
    ||(defined (ARDUCAM_SHIELD_V2) && (defined (OV2640_CAM) || defined (OV5640_CAM) || defined (OV5642_CAM))))
#error Please select the hardware platform and camera module in the ../libraries/ArduCAM/memorysaver.h file
#endif

const int CS = 16;
const int trigger = 0, echo = 2;

int wifiType = 0; // 0:Station  1:AP

//AP mode configuration
const char *AP_ssid = "arducam_esp8266";
const char *AP_password = "";

//Station mode you should put your ssid and password
const char *ssid = "OnePlus 5T";
const char *password = "54368148";

//Telegram Bot token
const char *botToken = "657738952:AAG9ux3sVO0IRTwiXMv3kXtWCXhZKZYdHIA";
const String groupId = "-337240309";

int Bot_mtbs = 1000; //mean time between scan messages
long Bot_lasttime;   //last time messages' scan has been done

static const size_t bufferSize = 4096;
static uint8_t buffer[bufferSize] = {0xFF};
uint8_t temp = 0, temp_last = 0;
int i = 0;
bool is_header = false;

ESP8266WebServer server(80);
WiFiClientSecure net_ssl;
UniversalTelegramBot bot(botToken, net_ssl);

#if defined (OV2640_MINI_2MP) || defined (OV2640_CAM)
ArduCAM myCAM(OV2640, CS);
#endif

void start_capture()
{
  myCAM.clear_fifo_flag();
  myCAM.start_capture();
}

void camCapture(ArduCAM myCAM)
{
  WiFiClient client = server.client();
  uint32_t len = myCAM.read_fifo_length();
  
  if(len >= MAX_FIFO_SIZE) //8Mb
  {
    Serial.println(F("Over size."));
  }
  if(len == 0) //0 kb
  {
    Serial.println(F("Size is 0."));
  }
  
  myCAM.CS_LOW();
  myCAM.set_fifo_burst();
  
  if (!client.connected())
  {
    return;
  }
  
  String response = "HTTP/1.1 200 OK\r\n";
  response += "Content-Type: image/jpeg\r\n";
  response += "Content-len: " + String(len) + "\r\n\r\n";
  server.sendContent(response);
  i = 0;
  
  while (len--)
  {
    temp_last = temp;
    temp = SPI.transfer(0x00);
    
    if((temp == 0xD9) && (temp_last == 0xFF))
    {
      buffer[i++] = temp;
      
      if(!client.connected())
      {
        break;
      }
      
      client.write(&buffer[0], i);
      is_header = false;
      i = 0;
      myCAM.CS_HIGH();
      break;
    }
    if (is_header == true)
    {
      if (i < bufferSize)
        buffer[i++] = temp;
      else
      {
        if (!client.connected()) 
        {
          break;
        }
        
        client.write(&buffer[0], bufferSize);
        i = 0;
        buffer[i++] = temp;
      }
    }
    else if((temp == 0xD8) & (temp_last == 0xFF))
    {
      is_header = true;
      buffer[i++] = temp_last;
      buffer[i++] = temp;
    }
  }
}

void serverCapture()
{
  myCAM.flush_fifo();
  myCAM.clear_fifo_flag();
  start_capture();
  
  Serial.println(F("CAM Capturing"));
  
  int total_time = 0;
  total_time = millis();
  
  while (!myCAM.get_bit(ARDUCHIP_TRIG, CAP_DONE_MASK));
  
  total_time = millis() - total_time;
  
  Serial.print(F("capture total_time used (in miliseconds):"));
  Serial.println(total_time, DEC);
  
  total_time = 0;
  
  Serial.println(F("CAM Capture Done."));
  
  total_time = millis();
  camCapture(myCAM);
  total_time = millis() - total_time;
  
  Serial.print(F("send total_time used (in miliseconds):"));
  Serial.println(total_time, DEC);
  Serial.println(F("CAM send Done."));
}

void serverStream()
{
  WiFiClient client = server.client();
  String response = "HTTP/1.1 200 OK\r\n";
  response += "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n";
  server.sendContent(response);

  while (1)
  {
    start_capture();
    
    while (!myCAM.get_bit(ARDUCHIP_TRIG, CAP_DONE_MASK));
    
    size_t len = myCAM.read_fifo_length();
    
    if(len >= MAX_FIFO_SIZE) //8Mb
    {
      Serial.println(F("Over size."));
      continue;
    }
    if(len == 0) //0 kb
    {
      Serial.println(F("Size is 0."));
      continue;
    }
    
    myCAM.CS_LOW();
    myCAM.set_fifo_burst();
    
    if (!client.connected())
    {
      Serial.println("break");
      break;
    }
    
    response = "--frame\r\n";
    response += "Content-Type: image/jpeg\r\n\r\n";
    server.sendContent(response);
    
    while(len--)
    {
      temp_last = temp;
      temp = SPI.transfer(0x00);

      if ((temp == 0xD9) && (temp_last == 0xFF))
      {
        buffer[i++] = temp;
        myCAM.CS_HIGH();
        
        if (!client.connected())
        {
          client.stop(); 
          is_header = false; 
          break;
        }
        
        client.write(&buffer[0], i);
        is_header = false;
        i = 0;
      }
      if(is_header == true)
      {
        if (i < bufferSize)
          buffer[i++] = temp;
        else
        {
          myCAM.CS_HIGH();
         
          if (!client.connected())
          {
            client.stop(); 
            is_header = false; 
            break;
          }
          
          client.write(&buffer[0], bufferSize);
          i = 0;
          buffer[i++] = temp;
          myCAM.CS_LOW();
          myCAM.set_fifo_burst();
        }
      }
      else if((temp == 0xD8) & (temp_last == 0xFF))
      {
        is_header = true;
        buffer[i++] = temp_last;
        buffer[i++] = temp;
      }
    }
    if (!client.connected())
    {
      client.stop(); 
      is_header = false; 
      break;
    }
  }
}

void handleNotFound()
{
  String message = "Server is running!\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  server.send(200, "text/plain", message);

  if (server.hasArg("ql"))
  {
    int ql = server.arg("ql").toInt();
    
    #if defined (OV2640_MINI_2MP) || defined (OV2640_CAM)
    myCAM.OV2640_set_JPEG_size(ql);
    #endif
    
    delay(1000);
    Serial.println("QL change to: " + server.arg("ql"));
  }
}

void setup()
{
  uint8_t vid, pid;
  uint8_t temp;
  
  #if defined(__SAM3X8E__)
  Wire1.begin();
  #else
  Wire.begin();
  #endif
  
  Serial.begin(115200);
  Serial.println(F("ArduCAM Start!"));
  
  pinMode(CS, OUTPUT);
  pinMode(trigger,OUTPUT);
  pinMode(echo,INPUT);
  SPI.begin();
  SPI.setFrequency(4000000); //4MHz
  
  myCAM.write_reg(ARDUCHIP_TEST1, 0x55);
  temp = myCAM.read_reg(ARDUCHIP_TEST1);
  
  if (temp != 0x55)
  {
    Serial.println(F("SPI interface Error!"));
    while (1);
  }
  
  #if defined (OV2640_MINI_2MP) || defined (OV2640_CAM)
  //Check if the camera module type is OV2640
  myCAM.wrSensorReg8_8(0xff, 0x01);
  myCAM.rdSensorReg8_8(OV2640_CHIPID_HIGH, &vid);
  myCAM.rdSensorReg8_8(OV2640_CHIPID_LOW, &pid);
  
  if ((vid != 0x26 ) && (( pid != 0x41 ) || ( pid != 0x42 )))
    Serial.println(F("Can't find OV2640 module!"));
  else
    Serial.println(F("OV2640 detected."));
  #endif

  myCAM.set_format(JPEG);
  myCAM.InitCAM();
  
  #if defined (OV2640_MINI_2MP) || defined (OV2640_CAM)
  myCAM.OV2640_set_JPEG_size(OV2640_320x240);
  #endif

  myCAM.clear_fifo_flag();
  
  if (wifiType == 0)
  {
    if (!strcmp(ssid, "SSID"))
    {
      Serial.println(F("Please set your SSID"));
      while (1);
    }
    if (!strcmp(password, "PASSWORD"))
    {
      Serial.println(F("Please set your PASSWORD"));
      while (1);
    }
    
    Serial.println();
    Serial.println();
    Serial.print(F("Connecting to "));
    Serial.println(ssid);

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    
    while (WiFi.status() != WL_CONNECTED)
    {
      delay(500);
      Serial.print(F("."));
    }
    
    Serial.println(F("WiFi connected"));
    Serial.println("");
    Serial.println(WiFi.localIP());
  }
  else if (wifiType == 1)
  {
    Serial.println();
    Serial.println();
    Serial.print(F("Share AP: "));
    Serial.println(AP_ssid);
    Serial.print(F("The password is: "));
    Serial.println(AP_password);

    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_ssid, AP_password);
    Serial.println("");
    Serial.println(WiFi.softAPIP());
  }

  server.on("/capture", HTTP_GET, serverCapture);
  server.on("/stream", HTTP_GET, serverStream);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println(F("Server started"));
}

int inline microsecondsToCentimetres(long duration)
{
  return duration*0.017;
}

void handleNewMessages(int numNewMessages)
{
  Serial.println("handleNewMessages");
  Serial.println(String(numNewMessages));

  for (int i=0; i<numNewMessages; i++)
  {
    String chat_id = String(bot.messages[i].chat_id);
    String text = bot.messages[i].text;
    String from_name = bot.messages[i].from_name;
    
    if (from_name == "")
    {
      from_name = "Guest";
    }
    if (text == "/stopbuzzer")
    {
      Serial.write(5); 
    }
    if (text == "/start") {
      String welcome = "Welcome to Universal Arduino Telegram Bot library, " + from_name + ".\n";
      bot.sendMessage(chat_id, welcome, "");
    }
  }
}

void loop()
{
  long duration;
  int cm;

  server.handleClient();

  digitalWrite(trigger,LOW);
  delayMicroseconds(2);
  digitalWrite(trigger,HIGH);
  delayMicroseconds(10);
  digitalWrite(trigger,LOW);

  duration = pulseIn(echo,HIGH);
  cm = microsecondsToCentimetres(duration);

  Serial.print(cm);
  Serial.println(" cm\n");

  if(cm <= 100)
  {
    Serial.println("Code 1: Someone's moving.");
    bot.sendMessage(groupId, "Someone's intruding. Open Arducam app to capture picture or record video.", "");
    Serial.write(1);
  }
  else
  {
    Serial.println("Code 0: Nothing's happening.");
    Serial.write(0);
  }

  if (millis() > Bot_lasttime + Bot_mtbs) 
  {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);

    while(numNewMessages)
    {
      Serial.println("got response");
      handleNewMessages(numNewMessages);
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    }

    Bot_lasttime = millis();
  }
  delay(500);
}
