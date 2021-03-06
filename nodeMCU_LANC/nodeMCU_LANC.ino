/*
  Arduino LANC<->RS232 interface
  Version 1.0
  For communicating with cameras via LANC
  For the interface circuit interface see
  http://controlyourcamera.blogspot.com/2011/02/arduino-controlled-video-recording-over.html

  "LANC" is a registered trademark of SONY.
  CANON calls their LANC compatible port "REMOTE".

  ------------------------------------------------------------------------------------------
  Comments regarding service mode for Sony second generation D8 camcorders:
  DCR-TRV8000E, DCR-TRV8100E, DCR-TRV120E, DCR-TRV125E, DCR-TRV320E, DCR-TRV325E
  DCR-TRV420E, DCR-TRV520E, DCR-TRV620E, DCR-TRV725E

  LANC message layout when reading/writing EEPROM(8 bytes each sent with LSB first)
  B0 B1 B2 B3 B4 B5 B6 B7

  B0 = First sent byte from our adapter
  B1 = Second sent byte from our adapter
  B2
  B3
  B4 = The 4 highest bits b7..b4 tells which page in the EEPROM you are at
  B5 = Confirmation that the command has been received, read command confirmed with F0h, write commands confirmed with F1h
  B6 = Tells which address in the EEPROM you are at
  B7 = Data at address

  The following commands is used to navigate the EEPROM and change data
  B1 B2
  FFh 00h = Read command, tells you current page:address:data without changing anything
  FFh 67h = Increase page by 1
  FFh 65h = Decrase page by 1
  FFh 38h = Increase address by 1
  FFh 36h = Decrase address by 1
  FFh 34h = Increase data by 1
  FFh 30h = Decrase data by 1
  FFh 32h = STORE command

  Metod for checksums (PAGE:ADDRESS:DATA):
  1) enable changes in memory: 00:01:00 to 00:01:01 (Store)
  2) change data on page D, how You need (all with STORE).
  3) read new value:
  "xx" from 02:F0
  "yy" from 02:F1
  4) enable update and visibility of checksums on (0F:FE and 0F:FF):
  00:FF:00 -> 00:FF:02 (STORE)
  00:01:01 -> 00:01:80 (STORE)
  5) update new checksums:
  write to address:
  0F:FF data "xx" ( from 02:F0 ) (STORE)
  0F:FE data "yy" ( from 02:F1 ) (STORE)
  6) disable changes:
  00:FF:02 -> 00:FF:00 (STORE)
  00:01:80 -> 00:01:00 (STORE)

  Links to more information:
  http://lea.hamradio.si/~s51kq/DV-IN.HTM
  http://www.sps.volyne.cz/set1394/anin/code20.html

  ------------------------------------------------------------------------------------------
*/

///////////////////////////
// Libaries              //
///////////////////////////

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>



////////////////////////////////////////////////////////////////////////////////////////////// Wifi Common code ////////////////////////////////////////////////////////////////////////////////////////////////


IPAddress    apIP(42, 42, 42, 42);  // Defining a static IP address: local & gateway
// Default IP in AP mode is 192.168.4.1

/* This are the WiFi access point settings. Update them to your likin */
const char *ssid = "ESP8266";
const char *password = "ESP8266Test";

// Define a web server at port 80 for HTTP
ESP8266WebServer server(80);

const int ledPin = D1; // an LED is connected to NodeMCU pin D1 (ESP8266 GPIO5) via a 1K Ohm resistor

bool ledState = false;

void handleRoot() {
  digitalWrite (LED_BUILTIN, 0); //turn the built in LED on pin DO of NodeMCU on
  digitalWrite (ledPin, server.arg("led").toInt());
  ledState = digitalRead(ledPin);

  /* Dynamically generate the LED toggle link, based on its current state (on or off)*/
  char ledText[80];

  if (ledState) {
    strcpy(ledText, "LED is on. <a href=\"/?led=0\">Turn it OFF!</a>");
  }

  else {
    strcpy(ledText, "LED is OFF. <a href=\"/?led=1\">Turn it ON!</a>");
  }

  ledState = digitalRead(ledPin);

  char html[1000];
  int sec = millis() / 1000;
  int min = sec / 60;
  int hr = min / 60;

  int brightness = analogRead(A0);
  brightness = (int)(brightness + 5) / 10; //converting the 0-1024 value to a (approximately) percentage value

  // Build an HTML page to display on the web-server root address
  snprintf ( html, 1000,

             "<html>\
  <head>\
    <meta http-equiv='refresh' content='10'/>\
    <title>ESP8266 WiFi Network</title>\
    <style>\
      body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; font-size: 1.5em; Color: #000000; }\
      h1 { Color: #AA0000; }\
    </style>\
  </head>\
  <body>\
    <h1>ESP8266 Wi-Fi Access Point and Web Server Demo</h1>\
    <p>Uptime: %02d:%02d:%02d</p>\
    <p>Brightness: %d%</p>\
    <p>%s<p>\
    <p>This page refreshes every 10 seconds. Click <a href=\"javascript:window.location.reload();\">here</a> to refresh the page now.</p>\
  </body>\
</html>",

             hr, min % 60, sec % 60,
             brightness,
             ledText
           );
  server.send ( 200, "text/html", html );
  digitalWrite ( LED_BUILTIN, 1 );
}


void handleNotFound() {
  digitalWrite ( LED_BUILTIN, 0 );
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += ( server.method() == HTTP_GET ) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";

  for ( uint8_t i = 0; i < server.args(); i++ ) {
    message += " " + server.argName ( i ) + ": " + server.arg ( i ) + "\n";
  }

  server.send ( 404, "text/plain", message );
  digitalWrite ( LED_BUILTIN, 1 ); //turn the built in LED on pin DO of NodeMCU off
}



////////////////////////////////////////////////////////////////////////////////////////////// LANC common code //////////////////////////////////////////////////////////////////////////////////////////////////


///////////////////////////
// Defines               //
///////////////////////////

#define lancPin 13
#define cmdPin 15
#define ledPin LED_BUILTIN
#define cmdPinON (digitalWrite(cmdPin, HIGH))   // Set digtal pin 7 (PD7)
#define cmdPinOFF (digitalWrite(cmdPin, LOW))  // Reset digtal pin 7 (PD7)
#define ledON (digitalWrite(ledPin, HIGH))      // Set LED pin 13 (PB5)
#define ledOFF (digitalWrite(ledPin, LOW))     // Reset LED pin 13 (PB5)
#define lancPinREAD (digitalRead(lancPin)) // Reads pin 11 (PB3)

///////////////////////////
// Variables             //
///////////////////////////

int bitDura = 104;           // Duration of one LANC bit in microseconds, orginal 104
int halfbitDura = 52;        // Half of bitDura
byte strPointer = 0;         // Index when receiving chars
char inString[5];            // A string to hold incoming data
char outString[25];          // A string to hold outgoing data
boolean strComplete = false; // Indicator to see if the string is complete
boolean lancCmd[16];         // Array for the lancCmd in bits
boolean lancMessage[64];     // Array for the complete LANC message in bits


///////////////////////////
// BMCC command codes    //
///////////////////////////

String Nop = "0000";
String RecordStart = "1833";
String RecordStop = "8C19";
String IrisIncrement = "2855"; // Used for IDLE state
String IrisDecrement = "2853"; // Used for IDLE state
String IrisRecIncrement = "942A"; // Used for RECORD state
String IrisRecDecrement = "9429"; // Used for RECORD state
String IrisAutoAdjust = "28AF";
String FocusShuttleFar = "28E0"; // Used for IDLE state (value mask 0x0F00: 1 3 5 7 9 B D F)
String FocusShuttleNear = "28F0"; // Used for IDLE state (value mask 0x0F00: 1 3 5 7 9 B D F)
String FocusShuttleRecFar = "9470"; // Used for RECORD state (value mask 0x0700: 0 1 2 3 4 5 6 7)
String FocusShuttleRecNear = "9478"; // Used for RECORD state (value mask 0x0700: 0 1 2 3 4 5 6 7)
String FocusFar = "2845"; // Used for IDLE state
String FocusNear = "2847"; // Used for IDLE state
String FocusRecFar = "9422"; // Used for RECORD state
String FocusRecNear = "9423"; // Used for RECORD state
String FocusAuto = "2843";


///////////////////////////
// Helper functions      //
///////////////////////////

void executeCommand(String command) {
  inString[0] = command.charAt(0);
  inString[1] = command.charAt(1);
  inString[2] = command.charAt(2);
  inString[3] = command.charAt(3);
  strComplete = true;
}


void bitarraytohexchar() {
  // The bit array lancMessage contains the whole LANC message (8 bytes) with LSB first
  // This function converts them to hex chars and stores them in outString (16 chars)

  byte temp = 0;

  for ( int i = 0 ; i < 8 ; i++) {  // Byte loop

    for ( int j = 0 ; j < 4 ; j++) { // Bit loop
      temp += (pow2(j) * lancMessage[(j + 4) + i * 8]);
    }
    outString[i * 3] = bytetohexchar(temp);
    temp = 0;

    for ( int j = 0 ; j < 4 ; j++) { // Bit loop
      temp += (pow2(j) * lancMessage[j + i * 8]);
    }
    outString[i * 3 + 1] = bytetohexchar(temp);
    outString[i * 3 + 2] = ' ';
    temp = 0;
  }

  outString[24] = '\n';

}


boolean hexchartobitarray() {
  // The hex code in char (4 chars) is located in inString
  // This function fills the lancCmd array with the bits in the order they should be sent
  // First byte 1 then byte 2 but with LSB first for both bytes

  int byte1, byte2;

  for (int i = 0 ; i < 4 ; i++ ) {
    if (!(isHexadecimalDigit(inString[i]))) {
      return 0;
    }
  }

  byte1 = (hexchartoint(inString[0]) << 4) + hexchartoint(inString[1]);
  byte2 = (hexchartoint(inString[2]) << 4) + hexchartoint(inString[3]);

  for (int i = 0 ; i < 8 ; i++ ) {
    lancCmd[i] = bitRead(byte1, i);     // Reads one bit from a number, x is number, n is position (0 is LSB)
  }
  for (int i = 0 ; i < 8 ; i++ ) {
    lancCmd[i + 8] = bitRead(byte2, i); // Reads one bit from a number, x is number, n is position (0 is LSB)
  }

  return 1;
}


void sendLanc(byte repeats) {
  // This function is time critical and optimized for Arduino Pro Mini
  // It takes ~3.2us for the arduino to set a pin state with the digitalWrite command
  // It takes ~80ns for the arduino to set pin state using the direct register method
  // delayMicroseconds(50) ~ 49us, delayMicroseconds(100) ~ 99us

  int i = 0;
  int bytenr = 0;

  while (pulseIn(lancPin, HIGH) < 5000) {
    // Sync to next LANC message
    // "pulseIn, HIGH" catches any 0V TO +5V TRANSITION and waits until the LANC line goes back to 0V
    // "pulseIn" also returns the pulse duration so we can check if the previous +5V duration was long enough (>5ms) to be the pause before a new 8 byte data packet
  }

  while (repeats) {

    i = 0;
    bytenr = 0;

    ledON;                                       // LANC message LED indicator on


    for ( ; bytenr < 8 ; bytenr++) {
      delayMicroseconds(bitDura - 7);            // LOW after long pause means the START bit of Byte 0 is here, wait START bit duration
      for ( ; i < (8 * (bytenr + 1)) ; i++) {
        if (bytenr < 2) {
          if (lancCmd[i]) {                      // During the first two bytes the adapter controls the line and puts out data
            cmdPinON;
          }
          else {
            cmdPinOFF;
          }
        }
        delayMicroseconds(halfbitDura);
        lancMessage[i] = !lancPinREAD;           // Read data line during middle of bit
        delayMicroseconds(halfbitDura);
      }
      cmdPinOFF;
      if (bytenr == 7) {
        ledOFF;
      }
      delayMicroseconds(halfbitDura);            // Make sure to be in the stop bit before waiting for next byte
      while (lancPinREAD) {                      // Loop as long as the LANC line is +5V during the stop bit or between messages
      }
    }
    repeats--;
  }
}


/*
  SerialEvent occurs whenever a new data comes in the
  hardware serial RX.  This routine is run between each
  time loop() runs, so using delay inside loop can delay
  response.  Multiple bytes of data may be available.
*/
void serialEvent() {

}


int pow2(int x) {
  switch (x) {
    case 0:
      return 1;
      break;
    case 1:
      return 2;
      break;
    case 2:
      return 4;
      break;
    case 3:
      return 8;
      break;
    case 4:
      return 16;
      break;
    case 5:
      return 32;
      break;
    case 6:
      return 64;
      break;
    case 7:
      return 128;
      break;
    default:
      return 0;
      break;
  }
}


char bytetohexchar(byte hexbyte) {
  switch (hexbyte) {
    case 15:
      return 'F';
      break;
    case 14:
      return 'E';
      break;
    case 13:
      return 'D';
      break;
    case 12:
      return 'C';
      break;
    case 11:
      return 'B';
      break;
    case 10:
      return 'A';
      break;
    default:
      return (hexbyte + 48);
      break;
  }
}


int hexchartoint(char hexchar) {
  switch (hexchar) {
    case 'F':
      return 15;
      break;
    case 'f':
      return 15;
      break;
    case 'E':
      return 14;
      break;
    case 'e':
      return 14;
      break;
    case 'D':
      return 13;
      break;
    case 'd':
      return 13;
      break;
    case 'C':
      return 12;
      break;
    case 'c':
      return 12;
      break;
    case 'B':
      return 11;
      break;
    case 'b':
      return 11;
      break;
    case 'A':
      return 10;
      break;
    case 'a':
      return 10;
      break;
    default:
      return (int) (hexchar - 48);
      break;
  }
}

////////////////////////////////////////////////////////////////////////////////////////////// Common Functions ////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////
// Setup code            //
///////////////////////////

void setup() {
  pinMode(cmdPin, OUTPUT);
  pinMode(lancPin, INPUT);
  pinMode(ledPin, OUTPUT);
  cmdPinOFF;                  // Reset LANC control pin so that the LANC line is unaffected(HIGH)
  Serial.begin(9600);        // Start serial port

  delay(1000);
  Serial.println("Configuring access point...");

  //set-up the custom IP address
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));   // subnet FF FF FF 00

  /* You can remove the password parameter if you want the AP to be open. */
  WiFi.softAP(ssid, password);

  IPAddress myIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(myIP);

  server.on ( "/", handleRoot );
  server.on ( "/rec", handleRecordRequest);
  server.on ( "/focus_auto", handleFocusAutoRequest);
  server.on ( "/focus_near", handleFocusNearRequest);
  server.on ( "/focus_far", handleFocusFarRequest);
  server.on ( "/iris_auto", handleIrisAutoRequest);
  server.on ( "/iris_increment", handleIrisIncrementRequest);
  server.on ( "/iris_decrement", handleIrisDecrementRequest);
  server.on ( "/status", handleStatusRequest);
  server.onNotFound ( handleNotFound );

  server.begin();
  Serial.println("HTTP server started");
}

///////////////////////////
// Main loop             //
///////////////////////////

void loop() {
  server.handleClient(); // Handle http client stuff
  
  if (strComplete) {                     // inString has arrived
    if (hexchartobitarray()) {           // Convert hex chars to bitarray
      sendLanc(4);                       // The LANC command needs to be repeated 4 times
      bitarraytohexchar();               // Convert received bitarray to hex chars
    }
    for (int i = 0 ; i < 5 ; i++) {       // Clear input array
      inString[i] = 0;
    }
    strComplete = false;                  // Reset data received flag
  }
}

//////////////////////////////
// Request Handler Helpers
//////////////////////////////

void handleRecordRequest() {
  executeCommand(RecordStart);
  server.send ( 200, "text/plain", "Success" );
}

void handleFocusAutoRequest() {
  executeCommand(FocusAuto);
  server.send ( 200, "text/plain", "Success" );
}

void handleIrisAutoRequest() {
  executeCommand(IrisAutoAdjust);
  server.send ( 200, "text/plain", "Success" );
}

void handleFocusNearRequest() {
  executeCommand(FocusNear);
  server.send ( 200, "text/plain", "Success" );
}

void handleFocusFarRequest() {
  executeCommand(FocusFar);
  server.send ( 200, "text/plain", "Success" );
}

void handleIrisIncrementRequest() {
  executeCommand(IrisIncrement);
  server.send ( 200, "text/plain", "Success" );
}

void handleIrisDecrementRequest() {
  executeCommand(IrisDecrement);
  server.send ( 200, "text/plain", "Success" );
}

void handleStatusRequest(){
  server.send(200, "text/plain", "Success");
}

