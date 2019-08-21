#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPClient.h>

#include <ArduinoJson.h>

#include <string>
#include <sstream>
#include <vector>
#include <math.h>
#include <tuple>
#include <algorithm>
#include <memory>

#include <StaticFunctions.h>

#include <UnixTimeHandler.h>

#include <Task.h>
#include <OneTimerTask.h>
#include <RecurringTask.h>
#include <ScheduleBD.h>
#include <Scheduler.h>
#include <ScheduleHandler.h>

#include <AnalogSensorPinEntry.h>

#include <MoistureSensorHandler.h>

#include <multiplexer.h>


using namespace std;
using namespace SF;


//Pin numbers Wemos D1 Mini:
//D0  16  
//D1  5 - pump relay
//D2  4 - optional relay for analog sensors
//D3  0
//D4  2
//D5  14  - 1st control pin for analog signal multiplexer
//D6  12  - 2nd control pin for analog signal multiplexer
//D7  13  - 3rd control pin for analog signal multiplexer
//D8  15  - 4th control pin for analog signal multiplexer
//TX  1
//RX  3

//------------------forward declarations-------------------

void updateServerData();

//------------------constants and global/static variables----------------------------

int PORT = 80;
String WIFI_NAME  = "YOUR_WIFI_SSID";
String WIFI_PWD   = "YOUR_WIFI_PASSWORD";
String DNS_NAME   = "d1miniirrigation";

std::shared_ptr<String> SERVER_MDNS = std::make_shared<String>("raspberrypi");
std::shared_ptr<String> ARDUINO_ID  = std::make_shared<String>("ARDUINO_IRRIGATION");
std::shared_ptr<String> SERVER_PORT = std::make_shared<String>("8080");

int IRRIGATION_PUMP_PIN = 5;

std::shared_ptr<int>           UNIX_DAY_OFFSET = std::make_shared<int>(3);
std::shared_ptr<unsigned long> UNIX_TIME       = std::make_shared<unsigned long>(0);

//std::vector<AnalogSensorPinEntry> ANALOG_SEN_ENTRIES;


MDNSResponder mdns;
std::shared_ptr<ESP8266WebServer> server = std::make_shared<ESP8266WebServer>(PORT);

String SERVER_REQUEST_BASE_PATH = "/irrigation";

//------ScheduleHandler------
std::shared_ptr<ScheduleHandler> scheduleHndlr = std::make_shared<ScheduleHandler>(server, 
                                                                                   SERVER_MDNS,
                                                                                   SERVER_PORT,
                                                                                   ARDUINO_ID,
                                                                                   UNIX_TIME,
                                                                                   UNIX_DAY_OFFSET,
                                                                                   IRRIGATION_PUMP_PIN,
                                                                                   SERVER_REQUEST_BASE_PATH);
//------UnixTimeHandler------
UnixTimeHandler unixTimeHndlr(server,
                              SERVER_MDNS,
                              SERVER_PORT,
                              ARDUINO_ID,
                              UNIX_TIME,
                              UNIX_DAY_OFFSET);

//------MoistureSensorHandler------
String SERVER_MOISTURE_SENSOR_REQUEST_BASE_PATH = SERVER_REQUEST_BASE_PATH + "/getMoistureSensorData";
int MAX_WAIT_PERIOD_ON_DRY_SOIL = 5 * 60 * 60;
float MIN_MOISTURE_SENSOR_VALUE = 400.0;
float MAX_MOISTURE_SENSOR_VALUE = 900.0; // Wemos D1 mini -> AnalogPin: 10 bit == 2**10 == 1024.0 <- abs max possible value!;
std::shared_ptr<MoistureSensorHandler> moistureSensorHandler = std::shared_ptr<MoistureSensorHandler>(
                                                                             new MoistureSensorHandler(scheduleHndlr,
                                                                                                       server,
                                                                                                       SERVER_MDNS,
                                                                                                       SERVER_PORT,
                                                                                                       ARDUINO_ID,
                                                                                                       SERVER_MOISTURE_SENSOR_REQUEST_BASE_PATH,
                                                                                                       UNIX_TIME,
                                                                                                       UNIX_DAY_OFFSET,
                                                                                                       {14, 12, 13, 15},
                                                                                                       {},
                                                                                                       A0,
                                                                                                       MIN_MOISTURE_SENSOR_VALUE,
                                                                                                       MAX_MOISTURE_SENSOR_VALUE,
                                                                                                       MAX_WAIT_PERIOD_ON_DRY_SOIL)
                                                                                                      );

//std::vector<int> testAnalogMultiplexerAnalogPins = {0,4,8};
//Multiplexer       multiplexer({14, 12, 13, 15}, testAnalogMultiplexerAnalogPins, A0);


//------------------main functions----------------------------

void setup() {
  Serial.begin(115200);
  Serial.println("\nESP started");
  WiFi.begin(WIFI_NAME.c_str(), WIFI_PWD.c_str());

  Serial.print("connecting to WiFi...");
  while(WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nconnected to WiFi!");
  Serial.print("D1 mini ip: ");
  Serial.print(WiFi.localIP());
  Serial.print("  on Port: ");
  Serial.println(PORT);

  if(mdns.begin(DNS_NAME, WiFi.localIP()))
  {
    Serial.print("SOC listening on DNS name: ");
    Serial.println(DNS_NAME);
  }

  MDNS.addService("http", "tcp", PORT);

  server->onNotFound([](){
    server->send(404, "text/plain", "link not found!");
  });

  server->on("/setServerIPAndPort",    receiveAndSetServerIpAndPort);
  
  server->on("/",                          landingPage);

  server->begin();

  //------------------------------------------------------------

  // setup/innit data by requesting server-data:
  
  Serial.println("setup/innit data by requesting server-data");
  Serial.println("requesting UNIX-time from server");

  updateServerData();

  //------------------------------------------------------------
}

void loop() {
  server->handleClient();

  unixTimeHndlr.updateUnixTime();

  updateServerData();

  scheduleHndlr->update();

  moistureSensorHandler->update();

  delay(10);
//  delay(1 * 1000);
}


//----------------------------------------------------------------------------

void landingPage()
{
  server->send(200, "text/plain", "Nila verwehrt den Zugang mit den lieblichen Worten: Haaaaa noooooooiiii!!!");
}

void updateServerData()
{
  unixTimeHndlr.requestDailyTimeFromServerIfNotAlreadyDone();

  if(unixTimeHndlr.successfullyReceivedUnixtTimeToday())
  {
    scheduleHndlr->requestDailyScheduleFromServerIfNotAlreadyDone();
    
    if(scheduleHndlr->successfullyReceivedScheduleToday()){
      moistureSensorHandler->requestDailyDataFromServerIfNotAlreadyDone();
    }
  }
}

void receiveAndSetServerIpAndPort()
{
  String serverIP = server->arg("ip");
  String serverPort = server->arg("port");
  if( serverIP.length() > 0 && serverPort.length() > 0 )
  {   
    *SERVER_MDNS = serverIP;
    *SERVER_PORT = serverPort;

    Serial.print("SERVER_MDNS: ");
    Serial.println(*SERVER_MDNS);
    Serial.print("serverPort: ");
    Serial.println(*SERVER_PORT);

    String msg("server-ip and server-port set to: IP: ");
    msg += *SERVER_MDNS;
    msg += String("   port: ");
    msg += *SERVER_PORT;
    
    Serial.println(msg);
    server->send(200, "text/plain", msg);
  }else{
    String msg("invalid server-ip and/or server-port received!");
    Serial.println(msg);
    server->send(400, "text/plain", msg);
  }
}
