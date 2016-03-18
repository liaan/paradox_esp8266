#include <ESP8266WiFi.h>
#include <ESP.h>
#include <PubSubClient.h>

#define CLK 0 // Keybus Yellow
#define DTA 2 // Keybus Green


//AP definitions
#define AP_SSID "your_ssid"
#define AP_PASSWORD "your_password"
#define DEVICE_NAME  "test"
#define MQ_SERVER      "10.0.0.10" //Mqtt Server IP
#define MQ_SERVERPORT  1883
#define MQTT_SUB_TOPIC "/house/alarm/action/#"  //Not Used
#define MQTT_PUB_TOPIC "/house/alarm/"   //Publishing topic

/*******************************/
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
ADC_MODE(ADC_VCC);
/*********** VARS ********************/
const int MqttZoneUpdateInterval = 1000;
const int MqttAlarmUpdateInterval = 10000;

String BusMessage = "";
byte ZoneStatus[33];
byte OldZoneStatus[33];

unsigned long LastClkSignal = 0;

unsigned long lastZoneUpdate = 0;
unsigned long lastAlarmUpdate = 0;
//unsigned long lastZoneUpdate = 0;
/*******************************/

/************* Setup ******************/
void setup()
{
  WiFi.mode(WIFI_STA); 
  
  pinMode(CLK,INPUT);
  pinMode(DTA,INPUT);
  Serial.begin(115200);
  
  Serial.println("Booting");
  
  attachInterrupt(CLK,interuptClockFalling,FALLING);
  
  Serial.println("Setting up MQTT!");
  mqtt.setServer(MQ_SERVER, MQ_SERVERPORT);
  //mqtt.setCallback(do_mqtt_topic_receive);
  
 
  Serial.println("Ready!");
  Serial.print("Current Voltage:"); Serial.println(ESP.getVcc());

  
}

/*
 *  Main Loop
 *  
 *  Will only do MQTT if there was a new message, no need to update anything if nothing changed.
 *  
 *  Full zone status happens every MqttZoneUpdateInterval
 *  
 *  Any Changes gets send immedetialy
 */
void loop()
{
  
  unsigned long currentMillis = millis();
  
  // ##Check if wifi still on
  if (WiFi.status() != WL_CONNECTED){
    wifiConnect();
    return;
  }
  // ##Check if Mqtt still connected
  if (mqtt.state() != MQTT_CONNECTED) {
    MQTT_connect();
    return;
  }

  
  
  // ## Check if there anything new on the Bus
  if(!checkClockIdle() or BusMessage.length() < 2  ){
    return ; 
  }

  // ## Save message and clear buffer
  String Message = BusMessage ;  
  // ## Clear old message
  BusMessage = "";
  
  // ##Copy old
  // ##Copy old
  memcpy(OldZoneStatus, ZoneStatus, sizeof(ZoneStatus));

  // ## Decode the message
  decodeMessage(Message);
  
  // ## Send all Zone statuss every interval
  if (currentMillis - lastZoneUpdate >= MqttZoneUpdateInterval) {
    Serial.println("Sending Zone Updates");
    // Send All zones status
    sendZonesStatus();
  }
  
  // ## Send all Zone statuss every interval
  if (currentMillis - lastAlarmUpdate >= MqttAlarmUpdateInterval) {
    Serial.println("Sending Alarm Updates");
    // ## Send Alarm Status
    sendAlarmStatus();
  }
  
  // Do immedate Update if zone changed
  if(memcmp ( OldZoneStatus, ZoneStatus, sizeof(ZoneStatus)) == 0){
   // Serial.println("Satus same");
    
  }else{
    
    for (int i = 0;i<33;i++){
      Serial.print(ZoneStatus[i],DEC);  
    }
    
    Serial.println("");
     for (int i = 0;i<33;i++){
      Serial.print(OldZoneStatus[i],DEC);  
    }
     Serial.println("");
    Serial.println("Not same");

    sendZonesStatus(false) ; 
  }

  // ## Do MQTT
  mqtt.loop();
  
}
/******************************  Send Zones Status  *******************************/
void sendZonesStatus(){
  sendZonesStatus(true) ;  
}
void sendZonesStatus(bool All){
  
  for(int i=0;i <= 31;i++){ 
    if(All == true){
        sendZoneStatus((String) (i+1),ZoneStatus[i]);     
    }else{
      if(ZoneStatus[i] != OldZoneStatus[i]){
        sendZoneStatus((String) (i+1),ZoneStatus[i]);   
      }
    }
  }
  // Set last update
  lastZoneUpdate = millis();
}
void sendAlarmStatus(){
  lastAlarmUpdate = millis();
}
void sendZoneStatus(String ZONE, int status )
{
  String topic;
  // ## Make topic
  topic = MQTT_PUB_TOPIC;
  topic += "zone/status/" ;
  topic += ZONE;
  
 // Serial.print("Publishing Zone to Topic: ");  Serial.print(topic);Serial.print(" Value off:");Serial.println(status,DEC);
  // ##Convert Int to Ascii
  status = status + '0';
  if (send_mqtt((char*)topic.c_str(), (char*)&status) == false) {
    Serial.println(F("Failed to send Zone update"));
  }
  else {
   // Serial.println(F("OK!"));
 }

}
/**********************************************  WIFI connect *******************************************************/
void wifiConnect()
{
  Serial.print("Connecting to AP:");
  Serial.print(AP_SSID);

  WiFi.begin(AP_SSID, AP_PASSWORD);
  
  int repeat = 0;
  
  while (WiFi.status() != WL_CONNECTED) {

    delay(1000);
    Serial.print(".");
    repeat++;
     
    if (repeat > 10){
     // wifi_no_connect();
      break;
      return;
        ;
    }
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}
/***********************************************************************************************************************/

/****************************************** Send MQTT *******************************************************************/
bool send_mqtt(char* topic, char* value) {

  return mqtt.publish(topic, value);

}
/****************************************** MQTT  *******************************************************************/

void MQTT_connect() {

  
  String clientName;
  
  clientName = DEVICE_NAME;
  clientName += String(micros() & 0xff, 16);

  Serial.print("Connecting to ");
  Serial.print(MQ_SERVER);
  Serial.print(" as ");
  Serial.println(clientName);


  int8_t retry_count = 0;

  // Stop if already connected.
  if (mqtt.connected()) {
    return;
  }

  Serial.print("Connecting to MQTT... ");


  while (mqtt.connected() != true ) { // connect will return 0 for connected

    if (mqtt.connect((char*)clientName.c_str())) {

      Serial.println("Connected to MQTT broker");
      Serial.print("Pub Topic is: ");
      Serial.println(MQTT_PUB_TOPIC);

      if (mqtt.publish(MQTT_PUB_TOPIC, "Hello, Johnny 5 is alive!")) {
        Serial.println("Publish ok");
        Serial.print("Subscribing to: ");
        Serial.println(MQTT_SUB_TOPIC);

        //Subscribe to topic
        mqtt.subscribe(MQTT_SUB_TOPIC);
      }
      else {
        Serial.println("Publish failed");
      }
    }
    else {
      Serial.println("MQTT connect failed");
      Serial.print("State:");      Serial.println(mqtt.state());
      Serial.println("Waiting a bit to try again.");
      //abort();
    }
    retry_count++;
    if (retry_count > 5){
      //mqtt_no_connect();
      break;
      return;
    }
    delay(2000);  // wait 2 seconds
  }
  Serial.println("MQTT Connected!");
}

/*******************************************************************************************************/
/****************************** Process Zone Status connect *****************************************/
/**
 * All ok
 * 11010000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000001 01100011
 * Zone 1
 * 11010000 00000000 01000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000001 10111001   
 * Zone 1 and 2 and 3
 * 11010000 00000000 01010100 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000001 1010 0001
 */
void processZoneStatus(String &msg){
    //Zone 1 = bit 17
    //Zone 2 = bit 19
    //Zone 3 = bit 21
    
   // Serial.println("ProcessingZones");  
    
    for (int i = 0;i < 32;i++){
      //Serial.println(17+(i*2));
      
      if(msg[17+(i*2)] == '1'){
        ZoneStatus[i] = 1;
        //Serial.println("Zone:"+(String)(i+1)+"Active");     
        
      }else{
      
        ZoneStatus[i] = 0;
      }
     // Serial.print("ZoneStatus:"+(String)(i+1)+":");Serial.println(ZoneStatus[i+1],BIN);
    }
     
}


void decodeMessage(String &msg){
  
  int cmd = GetIntFromString(msg.substring(0,8));   

  switch (cmd){
    case 0xd0: //Zone Status Message
      
      processZoneStatus(msg);
      
    break;
    case 0xD1: //Not sure yet
    
    break;
    case 0xD2: //Action Message;
      
    break;

    case 0x20: //
    
    break;
    case 0xE0: // Status
    
    break;
    default:
    //Do Nothing
    break;
    ;
    }
  //Serial.print("Cmd=");
  //Serial.println(cmd,HEX);
  
}



/*
 *   Check if clock idle, means end of message 
 *   Each messasge is split by  10 Millisecond (10 000 microsecond) delay, 
 *   So assume message been send if 4 clock signals (500us x 4  x 2 = 4000) is low
 *   
*/
bool checkClockIdle(){

   unsigned long currentMicros = micros();
   //time diff in 
   long idletime = (currentMicros - LastClkSignal);
   
   if(idletime > 8000){
      
      //Serial.println("Idle:"+(String)idletime +":"+currentMicros+":"+ LastClkSignal);
      return true;
   }else{
   
    return false;
   }
}




/**
 * Get int from String
 * 
 * Convert the Binary 10001000 to a int
 * 
 */
unsigned int GetIntFromString(String str){
  int r  = 0;
 // Serial.print("Received:");  Serial.println(str);
  int length = str.length();
  
  for(int j=0;j<length;j++)
  {    
    if (str[length-j-1] == '1') {      
      r |= 1 << j;      
    }
  
  }
  
 
  return r;
}
void interuptClockFalling()
{

   //## Set last Clock time
  LastClkSignal = micros();
  
  /*
  * Code need to be updated to ignore the response from the keypad (Rising Edge Comms).
  * 
  * Panel to other is on Clock Falling edge, Reply is after keeping DATA low (it seems) and then reply on Rising edge
  */
  
  //Just add small delay to make sure DATA is already set, each clock is 500 ~microseconds, Seem to have about 50ms delay before Data goes high when keypad responce and creating garbage .
  delayMicroseconds(150);  
  if (!digitalRead(DTA)) BusMessage += "1"; else BusMessage += "0";
  
  if (BusMessage.length() > 200)
  {
    Serial.println("String to long");
    //Serial.println((String) BusMessage);
    BusMessage = "";
//    printSerialHex(BusMessage);
    return; // Do not overflow the arduino's little ram
  }
  
  
}

/** 
 *  waitCLKChange
 *  
 *  Wait for 5000ms
 *  
 */
unsigned long waitCLKchange(int currentState)
{
  unsigned long c = 0;
  while (digitalRead(CLK) == currentState)
  {
    delayMicroseconds(10);
    c += 10;    
    if (c > 10000) break;
  }
  return c;
}



void printSerialDec(String &st)
{
  
  Serial.print("DEC: ");
  //Get number of Bytes
  int Bytes = (st.length()) / 8;
  
  String  val = "";

  for(int i=0;i<Bytes;i++)
  {
    String kk = "012345670123456701234567";
    val = st.substring((i*8),((i*8))+8);
 
    Serial.print(GetIntFromString(val),DEC);
    Serial.print(" ");
    
  }
   Serial.println("");
}





