#include <WiFi.h> //Connect to WiFi Network
#include <TFT_eSPI.h> // Graphics and font library for ST7735 driver chip
#include <SPI.h> //Used in support of TFT Display
#include <string.h>  //used for some string handling and processing.
#include <mpu6050_esp32.h>

TFT_eSPI tft = TFT_eSPI(); 


//state machine for pinging/incrementing, description of states in documentation for the function (scroll)
const uint8_t IDLE = 0;
const uint8_t INCREMENT = 1;
const uint8_t PING = 2;
uint8_t state = PING;

// state machine for face switching:
const uint8_t START_FACE = 3;
const uint8_t DOWN_FACE = 4; 
const uint8_t UP_FACE = 5;
bool display_seconds = 1; //boolean representing whether to display seconds or not
int face_state = START_FACE;


//state machine for accelerometer:
const uint8_t START = 6;
const uint8_t MOTION = 7;
const uint8_t STOPPED_INCREMENT = 8;
int acc_state = START;
unsigned long acc_timer; //timer storing the elapsed time since last motion
bool screen_on = 1; //boolean representing whether the screen should be on or not


//state machine for button 39 associated with accelerometer
const uint8_t IMU_IDLE = 9;
const uint8_t IMU_DOWN = 10;
const uint8_t IMU_UP = 11; 
int imu_state = IMU_IDLE;
bool always_on = 1; //boolean representing whether we are in the always on mode or the accelerometer mode.

const int RESPONSE_TIMEOUT = 6000; //ms to wait for response from host
const uint16_t IN_BUFFER_SIZE = 1000; //size of buffer to hold HTTP request
const uint16_t OUT_BUFFER_SIZE = 1000; //size of buffer to hold HTTP response
char request_buffer[IN_BUFFER_SIZE]; //char array buffer to hold HTTP request
char response_buffer[OUT_BUFFER_SIZE]; //char array buffer to hold HTTP response
char response_buffer_increment[OUT_BUFFER_SIZE]; //char array buffer to hold HTTP response
char response_copy[OUT_BUFFER_SIZE];
char formatted[15]; // char array buffer to hold formatted time response
char prev_formatted[15]; //char array holding the formatted time response at the last http ping. 
char fin[15]; //character array representing the final time that we are printing (need this variable for the colon flashing, one version of the final string will have the colon, other won't)

char network[] = "MIT";
char password[] = "";
uint8_t scanning = 0;//set to 1 if you'd like to scan for wifi networks (see below):
uint8_t channel = 1;
byte bssid[] = {0x5C, 0x5B, 0x35, 0xEF, 0x59, 0xC3}; //6 byte MAC address of AP you're targeting. Next House 5 west
//byte bssid[] = {0x5C, 0x5B, 0x35, 0xEF, 0x59, 0x03}; //3C
//byte bssid[] = {0xD4, 0x20, 0xB0, 0xC4, 0x9C, 0xA3}; //quiet side stud

unsigned long prev_time; //time value representing the time AT the last http ping. 
unsigned long elapsed_time; //timer representing time SINCE the last http  ping.
const int PING_TIMEOUT = 90000; //90 seconds

MPU6050 imu; //imu object called, appropriately, imu
const int BUTTON_IMU = 39;
const int BUTTON = 45; //pin connected to button 

//colon flashing related variables:
unsigned long elapsed_time_colon; //timer representing the last switch between colon/no colon
unsigned long prev_colon; //time AT the last switch between colon/no colon. 
bool colon_bool = 1; //boolean representing whether to display the colon. 


float old_acc_mag, older_acc_mag; //maybe use for remembering older values of acceleration magnitude
float acc_mag = 0;  //used for holding the magnitude of acceleration
float avg_acc_mag = 0; //used for holding the running average of acceleration magnitude
const float ZOOM = 9.81;
float x, y, z; //variables for grabbing x,y,and z values


/*----------------------------------
 * char_append Function:
 * Arguments:
 *    char* buff: pointer to character array which we will append a
 *    char c: 
 *    uint16_t buff_size: size of buffer buff
 *    
 * Return value: 
 *    boolean: True if character appended, False if not appended (indicating buffer full)
 */
uint8_t char_append(char* buff, char c, uint16_t buff_size) {
        int len = strlen(buff);
        if (len>buff_size) return false;
        buff[len] = c;
        buff[len+1] = '\0';
        return true;
}

/*----------------------------------
 * do_http_GET Function:
 * Arguments:
 *    char* host: null-terminated char-array containing host to connect to
 *    char* request: null-terminated char-arry containing properly formatted HTTP GET request
 *    char* response: char-array used as output for function to contain response
 *    uint16_t response_size: size of response buffer (in bytes)
 *    uint16_t response_timeout: duration we'll wait (in ms) for a response from server
 *    uint8_t serial: used for printing debug information to terminal (true prints, false doesn't)
 * Return value:
 *    void (none)
 */
void do_http_GET(char* host, char* request, char* response, uint16_t response_size, uint16_t response_timeout, uint8_t serial){
  WiFiClient client; //instantiate a client object
  if (client.connect(host, 80)) { //try to connect to host on port 80
    if (serial) Serial.print(request);//Can do one-line if statements in C without curly braces
    client.print(request);
    memset(response, 0, response_size); //Null out (0 is the value of the null terminator '\0') entire buffer
    uint32_t count = millis();
    while (client.connected()) { //while we remain connected read out data coming back
      client.readBytesUntil('\n',response,response_size);
      if (serial) Serial.println(response);
      if (strcmp(response,"\r")==0) { //found a blank line!
        break;
      }
      memset(response, 0, response_size);
      if (millis()-count>response_timeout) break;
    }
    memset(response, 0, response_size);  
    count = millis();
    while (client.available()) { //read out remaining text (body of response)
      char_append(response,client.read(),OUT_BUFFER_SIZE);
    }
    if (serial) Serial.println(response);
    client.stop();
    if (serial) Serial.println("-----------");  
  }else{
    if (serial) Serial.println("connection failed :/");
    if (serial) Serial.println("wait 0.5 sec...");
    client.stop();
  }
}

/*----------------------------------
 * format_time Function: 
 This function's goal is to format the time from military time to non-military time. 
 * Arguments:
    * char* response: null terminated character array containing the unformatted time (military time)
    * char* destination: null terminated character array to store the properly formatted time.
*/
void format_time(char* response, char* destination){
  int hour = (response[0] - '0')*10 + (response[1]-'0');
  if(hour > 12){
    hour = hour-12;
  }
  //midnight
  else if(hour == 0){
    hour = 12;
  }
  destination[0] = (hour/10) + '0';
  destination[1] = (hour%10) + '0';
  for(int i = 2; i < 8; i++)
    destination[i] = response[i];
}

/*----------------------------------
 * increment_time Function: 
 This function's goal is to increment the time by the number of milliseconds elapsed. 
 Reference refers to the time string at the last http get request in implementation. 
 * Arguments:
    * int milliseconds: containing the time value to increment by in milliseconds.
    * char* reference: null terminated character array containing the time string to be incremented.
    * char* destination: null terminated character array containing the destination for the incremented time.
    * bool military: whether to increment the time in military form or non military time form. 
*/
void increment_time(int milliseconds, char* reference, char* destination, bool military){
  //indices 0 and 1 are hour
  //indices 3 and 4 are mins
  //indices 6 and 7 are seconds
  //indices 2,5 are colons :p 
  
  //since we are pinging every 90 seconds, the elapsed time will always be less than or equal to 1 min 30 seconds
  int seconds = (milliseconds / 1000) % 60;
  int mins = (milliseconds / 1000) / 60;


  int response_mins = ((reference[3] - '0') * 10) + (reference[4] - '0');
  int response_seconds = ((reference[6] - '0')*10) + (reference[7]-'0');
  int response_hours = ((reference[0] - '0') * 10) + (reference[1] - '0');

  //increment mins and seconds
  response_mins += mins;
  response_seconds += seconds;

  //checking to see if we need to increment the minutes/hours more
  if(response_seconds >= 60){
    response_mins += 1;
    response_seconds -= 60;
  }
  if(response_mins >= 60){
    response_hours += 1;
    response_mins -= 60;
  }

  //if it's not military time, adjust the hours so that it is not in military time anymore.  
  if(military == 0){
    if(response_hours > 12){
      response_hours = response_hours % 12;
    }
  }

  //checking for midnight, otherwise if the ping happens before midnight the hour will increment to 24 instead of 00 and print AM. 
  else{
    if(response_hours >= 24)
       response_hours = 0;
  }
   
  //using this info to do some wacky string manipulation :D
  int tens = response_mins / 10;
  int ones = response_mins % 10;
  destination[3] = tens + '0';
  destination[4] = ones + '0';

  tens = response_seconds / 10;
  ones = response_seconds % 10;
  
  destination[6] = tens + '0';
  destination[7] = ones+ '0';

  tens = response_hours / 10;
  ones = response_hours % 10;
  destination[0] = tens + '0';
  destination[1] = ones+ '0';
}

/*----------------------------------
 * http_get_time Function: 
 This function pings the time server and also trims the output so that it just contains the time string. 
*/
void http_get_time(){
  memset(formatted, 0, sizeof(formatted));
    sprintf(request_buffer, "GET http://iesc-s3.mit.edu/esp32test/currenttime HTTP/1.1\r\n");
    strcat(request_buffer, "Host: iesc-s3.mit.edu\r\n"); //add more to the end
    strcat(request_buffer, "\r\n"); //add blank line!
    do_http_GET("iesc-s3.mit.edu", request_buffer, response_buffer, OUT_BUFFER_SIZE, RESPONSE_TIMEOUT, true);
    
    for(int i = 0; i < 8; i++){
      response_copy[i] = response_buffer[i + 11];
    }
    format_time(response_copy, formatted);
    //tft.println(formatted);

    
}

/*----------------------------------
 * morning Function: 
 This function's goal is to look at whether the current time is AM or PM. It does this by looking at the military time version of the string. 
 * Return value: returns a boolean value for whether it is morning or not (1 if AM, 0 or PM)
*/
bool morning(){
  int hour = 0;
  hour += (response_buffer_increment[0] - '0')*10 + (response_buffer_increment[1] - '0');
  if(hour < 12)
    return 1;
  else
    return 0;
}

/*----------------------------------
 * increment_sm Function: 
 This function represents a state machine for whether it is time to put in a new http request or just increment the time using information from millis().
 It also is the function responsible for printing to the LCD screen. 
 IDLE: Set the timers and switch to ping state.
 INCREMENT: Increments the time unless it's been longer than 90 seconds, then switch to Ping if it is.
 PING: Performs the http request. 
 * Arguments:
    * bool mode: corresponding to whether the colon should be flashing or not, mode = 0 corresponds to colon flashing without seconds, 1 to displaying seconds
    * bool screen_on: correspnoding to whether the screen should be on or not(important for imu)
*/
void increment_sm(bool mode, bool screen_on){
  switch(state){
    case IDLE:
      state = PING;
      prev_time = millis();
      prev_colon = millis();
      elapsed_time = 0;
      break;
    case INCREMENT:
      if(elapsed_time > PING_TIMEOUT)
        state = PING;
      else{
        if(screen_on){
          elapsed_time = millis()-prev_time;
          elapsed_time_colon = millis() - prev_colon;
          increment_time(elapsed_time, prev_formatted, formatted, 0);
          increment_time(elapsed_time, response_copy, response_buffer_increment, 1);
          formatted[2] = ':';
          //colon flashing mode related things:
          if(mode == 0){
            if(elapsed_time_colon >= 1000){
              colon_bool = !colon_bool;
              prev_colon = millis();

            }
            if(!colon_bool){
              formatted[2] = ' ';
            }
            else{
              formatted[2] = ':';
            }
            
            for(int i = 0; i < 5; i++){
              fin[i] = formatted[i];
            }
            //basically replace the colon and seconds with blank spaces so "11:00:24" becomes "11:00   "
            for(int i = 5; i < strlen(formatted);i++){
              fin[i] = ' ';
            }
          }
          else{
            strcpy(fin, formatted);
          }
          //gets rid of the leading 0
          if(fin[0] == char(48)){
            memset(fin, ' ', 1);
          }
          //tft printing
          tft.setCursor(0, 50, 1);
          tft.println(fin);
          if(morning())
            tft.println("AM");
          else
            tft.println("PM");
          strcpy(fin, "");
        }
        else{
          tft.fillScreen(TFT_BLACK);
        }
      }
        
      break; 

    case PING:
      http_get_time();

      //these lines are for an offset I noticed I think is related to the time it takes to ping the server? I noticed my time was consistently off by about a second. 
      formatted[7] = (formatted[7]-'0'+1) + '0';
      response_copy[7] = (response_copy[7]-'0'+1) + '0';


      prev_time = millis();
      elapsed_time = 0;
      strcpy(prev_formatted, formatted);
      strcpy(response_buffer_increment, response_copy);
      Serial.println(response_buffer_increment);
      state = INCREMENT;
      break;
    

  }
}

/*----------------------------------
 * face_sm Function: 
 This function is a state machine for switching between the two faces (colon flashing or displaying seconds with no colon flash)
 START_FACE: waits for a push on button.
 DOWN_FACE: detected a push, waiting for the release
 UP_FACE: Flips the boolean display_seconds and resets to START_FACE.
 * Arguments:
    * int button45: Value found from digitalRead for whether button at GPIO pin 45 has been pushed.
*/
void face_sm(int button45){
  switch(face_state){
    case(START_FACE):
      if(button45 == 0)
        face_state = DOWN_FACE;
      break;
    case(DOWN_FACE):
      if(button45 == 1)
        face_state = UP_FACE;      
      break;
    case(UP_FACE):
      display_seconds = !display_seconds;
      face_state = START_FACE;          
      
      colon_bool = 1;
      break;
  }
}

/*----------------------------------
 * acc_sm Function: 
 This function is responsible for switching between on and off when in IMU mode:
 START: The start state, defaults to turning the screen on and waiting for 15 seconds or for motion. 
 MOTION: Switches to this state if there is motion detected, if the imu reads a reading that corresponds to no motion, switches to the stop/increment state. 
 STOPED_INCREMENT: switches to this state when there is no motion detected or from the start state. 
*/
void acc_sm(){
  imu.readAccelData(imu.accelCount);
  x = ZOOM * imu.accelCount[0] * imu.aRes;
  y = ZOOM * imu.accelCount[1] * imu.aRes;
  z = ZOOM * imu.accelCount[2] * imu.aRes;
  acc_mag = sqrt(x*x + y*y + z*z);
  avg_acc_mag = (old_acc_mag + older_acc_mag + acc_mag) /3.0;
  older_acc_mag = old_acc_mag;
  old_acc_mag = acc_mag;
  
  switch(acc_state){     
    case(START):
      screen_on = 1;
      
      if(avg_acc_mag < 8)
        acc_state = MOTION;
      else if(millis()-acc_timer > 15000)
        acc_state = STOPPED_INCREMENT;
      break;
    case(MOTION):
      screen_on = 1;
      acc_timer = millis();
      if(avg_acc_mag > 10)
        acc_state = STOPPED_INCREMENT;
      break;
    case (STOPPED_INCREMENT):
      if(avg_acc_mag<8)
        acc_state = MOTION;
      else if(millis()-acc_timer > 15000)
        screen_on = 0;  
      break;
  }
  
}

/*----------------------------------
 * imu_button_sm Function: 
 This function represents a state machine for the button at pin 39, responsible for switching between the imu mdoe/ always on mode.
 IMU_IDLE: Waiting for a button push
 IMU_DOWN: Button has been pushed; waiting for button release.
 IMU_UP: Button has been pressed and released, switch between the mode using the always_on boolean.
*/
void imubutton_sm(){
  int imu_pushed = digitalRead(BUTTON_IMU);
  switch(imu_state){
    case(IMU_IDLE):
      if(imu_pushed == 0)
        imu_state = IMU_DOWN;
      break;

    case(IMU_DOWN):
      if(imu_pushed == 1)
        imu_state = IMU_UP;
      break;
    case(IMU_UP):
      always_on = !always_on;
      if(!always_on)
        acc_timer = millis();
      imu_state = IMU_IDLE;
      break;
  }
}

void setup() {
  tft.init();  //init screen
  tft.setRotation(3); //adjust rotation
  tft.setTextSize(3); //default font size
  tft.fillScreen(TFT_BLACK); //fill background
  tft.setTextColor(TFT_WHITE, TFT_BLACK); //set color of font to green foreground, black background
  Serial.begin(115200); //begin serial comms
  delay(100); //wait a bit (100 ms)
  Wire.begin();
  delay(50); //pause to make sure comms get set up
  if (imu.setupIMU(1)) {
    Serial.println("IMU Connected!");
  } else {
    Serial.println("IMU Not Connected :/");
    Serial.println("Restarting");
    ESP.restart(); // restart the ESP (proper way)
  }
  if (scanning){
    int n = WiFi.scanNetworks();
    Serial.println("scan done");
    if (n == 0) {
      Serial.println("no networks found");
    } else {
      Serial.print(n);
      Serial.println(" networks found");
      for (int i = 0; i < n; ++i) {
        Serial.printf("%d: %s, Ch:%d (%ddBm) %s ", i + 1, WiFi.SSID(i).c_str(), WiFi.channel(i), WiFi.RSSI(i), WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "open" : "");
        uint8_t* cc = WiFi.BSSID(i);
        for (int k = 0; k < 6; k++) {
          Serial.print(*cc, HEX);
          if (k != 5) Serial.print(":");
          cc++;
        }
        Serial.println("");
      }
    }
  }
  delay(100); //wait a bit (100 ms)



  //if using regular connection use line below:
 // WiFi.begin("MIT", "");
  //if using channel/mac specification for crowded bands use the following:
  WiFi.begin(network, password, channel, bssid);
  uint8_t count = 0; //count used for Wifi check times
  Serial.print("Attempting to connect to ");
  Serial.println(network);
  while (WiFi.status() != WL_CONNECTED && count<6) {
    delay(500);
    Serial.print(".");
    count++;
  }
  delay(2000);
  if (WiFi.isConnected()) { //if we connected then print our IP, Mac, and SSID we're on
    Serial.println("CONNECTED!");
    Serial.printf("%d:%d:%d:%d (%s) (%s)\n",WiFi.localIP()[3],WiFi.localIP()[2],
                                            WiFi.localIP()[1],WiFi.localIP()[0], 
                                          WiFi.macAddress().c_str() ,WiFi.SSID().c_str());
    delay(500);
  } else { //if we failed to connect just Try again.
    Serial.println("Failed to Connect :/  Going to restart");
    Serial.println(WiFi.status());
    ESP.restart(); // restart the ESP (proper way)
  }

  prev_time = millis();
  pinMode(BUTTON, INPUT_PULLUP);
  pinMode(BUTTON_IMU, INPUT_PULLUP);
}


void loop(){
  imubutton_sm();
  if(!always_on){
    acc_sm();
  }
  else{
    //screen should always be on in... always on mode.
    screen_on = 1;
    acc_state = START;
  }
  increment_sm(display_seconds, screen_on);
  face_sm(digitalRead(BUTTON));
  
}

