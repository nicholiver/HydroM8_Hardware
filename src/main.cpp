#include <Arduino.h>
#include <Stepper.h>
#include "DHTesp.h"
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Firebase_ESP_Client.h>
#include <FirebaseESP32.h>
#include <NTPClient.h>
#include <time.h>
#include <WiFiUdp.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>

#define API_KEY "AIzaSyDMGvHKEtQLxHZVk1UyM7N5J4Cqm8oTw88"
#define PROJ_ID "iotflutter-ec7c2"

#define FIREBASE_HOST "iotflutter-ec7c2-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_AUTH "SnZk1zAGELXFQhplg6kmxm7y9Rpi6eBhjzOkbOED"
#define EMAIL "esp32sensor@gmail.com"
#define PASSWORD "test123"

#define MOSFET_PIN 5
#define DHT_PIN 4
#define soilmoistPin 35
#define LIMIT_SWITCH_R 33
#define LIMIT_SWITCH_L 25

int StepsPerRevolution = 2048;
#define STEPPER_PIN_1 13
#define STEPPER_PIN_2 14
#define STEPPER_PIN_3 12
#define STEPPER_PIN_4 27

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define OLED_RESET     -1
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define NTP_SERVER "pool.ntp.org"
const long gmtOffsetsec = 25200;
const int daylightOffsetsec = 0;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, NTP_SERVER, gmtOffsetsec, daylightOffsetsec);
String currentTime;
String currentHour;
String currentDate;
String timeValue;
String currenthourFirestore;
String timeOn;

FirebaseData fbdo;
FirebaseData RTDB;
FirebaseAuth auth;
FirebaseConfig config;
FirebaseData fbdoStream;

DHTesp dht;
Stepper myStepper(StepsPerRevolution, STEPPER_PIN_1, STEPPER_PIN_2, STEPPER_PIN_3, STEPPER_PIN_4);

TaskHandle_t humidityTaskHandle;
TaskHandle_t updateTimeHandle;
TaskHandle_t displayOledHandle;
TaskHandle_t controlMotorTaskHandle;
TaskHandle_t streamTaskHandle;
TaskHandle_t updateToFireBaseHandler;
TaskHandle_t autoOnHandle;
TaskHandle_t updateMotorStatusHandle;
TaskHandle_t initiateAutoOnHandle;
SemaphoreHandle_t dataSemaphore;

void readHumidityTask(void *parameter);
void controlMotorTask(void *parameter);
void updateTime(void *parameter);
void wifiSetup();
void onFirebaseStream(FirebaseStream data);
void fireBaseSetup(const String& streamPath);
void updateToFirebase(void *parameter);
void displayOled(void *parameter);
void autoOn(void *parameter);
void updateMotorStatus(void *parameter);

int day;
int temperature = 0;
int humidity = 0;
int lastTemp;
int lastHumid;
int soilmoist;
int soilMoistPercent;
int lastSoilMoistPercent;
char docTempPath[100];
char docHumPath[100];
char docMoistPath[100];
char RTDBTempPath[100];

int temp;
int hum;
int moist;
int tempHumMoistPath;

bool uploadLimitSwitchR = false;
bool uploadLimitSwitchL = false;
bool isMotorMoving = false;
bool isReading = false;
bool monday = false;
bool tuesday = false;
bool wednesday = false;
bool thursday = false;
bool friday = false;
bool saturday = false;
bool sunday = false;

void setup() {
  Serial.begin(115200);
  pinMode(MOSFET_PIN, OUTPUT);
  pinMode(LIMIT_SWITCH_R, INPUT);
  pinMode(LIMIT_SWITCH_L, INPUT);
  digitalWrite(MOSFET_PIN, HIGH);
  display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS); 
  dht.setup(DHT_PIN, DHTesp::DHT11);
  display.clearDisplay();
  display.display();
  wifiSetup();
  fireBaseSetup("/test");
  timeClient.begin();
  myStepper.setSpeed(10);
  display.clearDisplay();
  display.display();
  display.setTextColor(WHITE);
  display.setTextSize(2);
  display.clearDisplay();
  display.setCursor((display.width() - 100) / 2, (display.height() - 16) / 2);
  display.println("HYDRO-M8");
  display.display();
  delay(3000);
  display.clearDisplay();

  dataSemaphore = xSemaphoreCreateBinary();
  xSemaphoreGive(dataSemaphore);

xTaskCreatePinnedToCore(
      controlMotorTask,  // Task function
      "MotorTask",       // Task name
      configMINIMAL_STACK_SIZE + 8192,              // Stack size
      NULL,              // Task parameter
      1,                 // Task priority
      &controlMotorTaskHandle,              // Task handle
      1                  // Core to run the task (0 or 1)
  );

  xTaskCreatePinnedToCore(
      readHumidityTask,   
      "HumidityTask",     
      configMINIMAL_STACK_SIZE + 4096,
      NULL,               
      2,                  
      &humidityTaskHandle,
      0                   
  );

  xTaskCreatePinnedToCore(
    updateToFirebase,
    "updateToFirebase",
    configMINIMAL_STACK_SIZE + 8192,
    NULL,
    1,
    &updateToFireBaseHandler,
    0
  );

  xTaskCreatePinnedToCore(
    updateTime,
    "UpdateTimeTask",
    configMINIMAL_STACK_SIZE + 2048,
    NULL,
    1,
    &updateTimeHandle,
    0
  );

  xTaskCreatePinnedToCore(
    displayOled,
    "DisplayOled",
    configMINIMAL_STACK_SIZE + 2048,
    NULL,
    2,
    &displayOledHandle,
    0
  );

  xTaskCreatePinnedToCore(
    autoOn,
    "autoOn",
    configMINIMAL_STACK_SIZE + 8192,
    NULL,
    2,
    &autoOnHandle,
    1
  );

  xTaskCreatePinnedToCore(
    updateMotorStatus,
    "updateMotorStatus",
    configMINIMAL_STACK_SIZE + 8192,
    NULL,
    1,
    &updateMotorStatusHandle,
    0
  );

}

void loop() {

}

void wifiSetup(){
  WiFi.mode(WIFI_STA);
  WiFiManager wifiManager;
  bool res;
  display.display();
    display.setTextColor (WHITE);
    display.setCursor((display.width() - 114) / 2, (display.height() - 10) / 2);
    display.setTextSize(1);
    display.println("Connecting to wifi");
    display.display();
    delay(3000);
  res = wifiManager.autoConnect("HYDRO-M8");
  if(!res){
    Serial.println("Failed to connect");
    delay(3000);
    ESP.restart();
  }
  else {
    display.clearDisplay();
    display.display();
    display.setTextColor (WHITE);
    display.setCursor((display.width() - 96) / 2, (display.height() - 10) / 2);
    display.setTextSize(1);
    display.println("Connected to wifi");
    display.display();
    delay(1000);
    Serial.println("Connected to wifi");
  }
}

void onFirebaseStream(FirebaseStream data)
{
  Serial.printf("onFirebaseStream: Stream Path: %s, Data Path: %s, Data Type: %s, Data: %s\n",
                data.streamPath().c_str(), data.dataPath().c_str(), data.dataType().c_str(),
                data.stringData().c_str());
  if (data.dataType() == "boolean")
  {
    if (data.dataPath() == "/value")
    {
      isMotorMoving = data.boolData();
      Serial.printf("isMotorMoving: %d\n", isMotorMoving);
    }
    if (data.dataPath() == "/monday")
    {
      monday = data.boolData();
      Serial.printf("monday: %d\n", monday);
    }
    if (data.dataPath() == "/tuesday")
    {
      tuesday = data.boolData();
      Serial.printf("tuesday: %d\n", tuesday);
    }
    if (data.dataPath() == "/wednesday")
    {
      wednesday = data.boolData();
      Serial.printf("wednesday: %d\n", wednesday);
    }
    if (data.dataPath() == "/thursday")
    {
      thursday = data.boolData();
      Serial.printf("thursday: %d\n", thursday);
    }
    if (data.dataPath() == "/friday")
    {
      friday = data.boolData();
      Serial.printf("friday: %d\n", friday);
    }
    if (data.dataPath() == "/saturday")
    {
      saturday = data.boolData();
      Serial.printf("saturday: %d\n", saturday);
    }
    if (data.dataPath() == "/sunday")
    {
      sunday = data.boolData();
      Serial.printf("sunday: %d\n", sunday);
    }
  }
  if (data.dataType() == "int")
  {
    if (data.dataPath() == "/temp")
    {
      temperature = data.intData();
      Serial.printf("temperature: %d C\n", temperature);
    }
    if (data.dataPath() == "/hum")
    {
      humidity = data.intData();
      Serial.printf("humidity: %d %%\n", humidity);
    }
    if (data.dataPath() == "/soil")
    {
      soilMoistPercent = data.intData();
      Serial.printf("soilMoistPercent: %d %%\n", soilMoistPercent);
    }
  }
  if (data.dataType() == "string")
  {
    if (data.dataPath() == "/time")
    {
      timeOn = data.stringData().c_str();
      Serial.printf("timeOn: %s\n", timeOn.c_str());
    }
  }
  if (data.dataType() == "json")
  {
    if (data.dataPath() == "/")
    {
      DynamicJsonDocument doc(1024);
      DeserializationError error = deserializeJson(doc, data.stringData().c_str());
    if (error)
    {
      Serial.print("deserializeJson() failed: ");
      Serial.println(error.f_str());
      return;
    }

    if (doc.containsKey("hum"))
      {
        lastHumid = doc["hum"];
        Serial.printf("lastHumid: %d %%\n", lastHumid);
      }

      if (doc.containsKey("temp"))
      {
        lastTemp = doc["temp"];
        Serial.printf("lastTemp: %d C\n", lastTemp);
      }

      if (doc.containsKey("soil"))
      {
        lastSoilMoistPercent = doc["soil"];
        Serial.printf("lastSoilMoistPercent: %d %%\n", lastSoilMoistPercent);
      }

      if (doc.containsKey("value"))
      {
        isMotorMoving = doc["value"];
        Serial.printf("isMotorMoving: %d\n", isMotorMoving);
      }

      if (doc.containsKey("monday"))
      {
        monday = doc["monday"];
        Serial.printf("monday: %d\n", monday);
      }

      if (doc.containsKey("tuesday"))
      {
        tuesday = doc["tuesday"];
        Serial.printf("tuesday: %d\n", tuesday);
      }

      if (doc.containsKey("wednesday"))
      {
        wednesday = doc["wednesday"];
        Serial.printf("wednesday: %d\n", wednesday);
      }

      if (doc.containsKey("thursday"))
      {
        thursday = doc["thursday"];
        Serial.printf("thursday: %d\n", thursday);
      }

      if (doc.containsKey("friday"))
      {
        friday = doc["friday"];
        Serial.printf("friday: %d\n", friday);
      }

      if (doc.containsKey("saturday"))
      {
        saturday = doc["saturday"];
        Serial.printf("saturday: %d\n", saturday);
      }

      if (doc.containsKey("sunday"))
      {
        sunday = doc["sunday"];
        Serial.printf("sunday: %d\n", sunday);
      }      

      if (doc.containsKey("time"))
      {
        timeOn = doc["time"].as<String>();
        Serial.printf("timeOn: %s\n", timeOn.c_str());
      }
    }
    else
    {
      Serial.print ("invalid path");
    }
  }
  else
  {
    Serial.println("invalid data type");
  }
}

void fireBaseSetup(const String& streamPath){
    config.api_key = API_KEY;
    config.host = FIREBASE_HOST;
    auth.user.email = EMAIL;
    auth.user.password = PASSWORD;
    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);

    fbdo.setResponseSize(2048);
    Firebase.RTDB.setwriteSizeLimit(&fbdo, "medium");
    while (!Firebase.ready())
    {
      Serial.println("Connecting to firebase");
      delay(500);
    }
    String path = streamPath;
    if (Firebase.RTDB.beginStream(&fbdoStream, path.c_str()))
    {
      Serial.println("Stream started at: "+ path);
      Firebase.RTDB.setStreamCallback(&fbdoStream, onFirebaseStream, 0);
    }
    else{
      Serial.println("Stream failed: "+ fbdoStream.errorReason());
    }
    
}

void updateTime(void *parameter){
  for(;;){
  timeClient.update();
  unsigned long epochTime = timeClient.getEpochTime();
  struct tm *timeinfo;
  time_t rawTime = (time_t)epochTime;
  timeinfo = localtime(&rawTime);
  day = timeClient.getDay();
  // Print the date and time
  char formattedDate[20];
  strftime(formattedDate, sizeof(formattedDate), "%Y-%m-%d", timeinfo);

  if(!(strncmp(formattedDate, "2036",4)==0 || strncmp(formattedDate, "1970",4)==0))
  {
  char TimeValue[20];
  strftime(TimeValue, sizeof(TimeValue), "%Y%m%d%H%M%S", timeinfo);
  char formattedTime[20];
  strftime(formattedTime, sizeof(formattedTime), "%Y-%m-%d_%H-%M-%S", timeinfo);
  char formattedHour[20];
  strftime(formattedHour, sizeof(formattedHour), "%H:%M:%S", timeinfo);
  char formattedHourFirestore[20];
  strftime(formattedHourFirestore, sizeof(formattedHourFirestore), "%H:%M", timeinfo);
  currentTime = String(formattedTime).c_str();
  currentHour = String(formattedHour).c_str();
  currentDate = String(formattedDate).c_str();
  currenthourFirestore = String(formattedHourFirestore).c_str();
  timeValue = String(TimeValue).c_str();
  }
  else{
    Serial.println("Time not updated");
    timeClient.forceUpdate();
  }
  vTaskDelay(pdMS_TO_TICKS(500));
  }
}

void readHumidityTask(void *parameter) {

  for (;;) {
    // Read temperature and humidity values from DHT sensor
    temperature = dht.getTemperature();
    humidity = dht.getHumidity();

    if (temperature==2147483647 || humidity==2147483647) {
      Serial.println("Failed to read from DHT sensor!");
      temperature = lastTemp;
      humidity = lastHumid;
    }else{
      lastTemp = temperature;
      lastHumid = humidity;
    }

    soilmoist = analogRead(soilmoistPin);
    soilMoistPercent = map(soilmoist, 4095, 0, 0, 100);

    Serial.printf("Temperature: %d C\n", temperature);
    Serial.printf("Humidity: %d %%\n", humidity);
    Serial.printf("Soil Moisture: %d %%\n", soilMoistPercent);
    Serial.println();

    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}

void updateToFirebase(void *parameter){
  for(;;){
    xSemaphoreTake(dataSemaphore,portMAX_DELAY);
  // update data to firebase
    temp = sprintf(docTempPath, "temperature/data_%s", currentTime.c_str());
    hum = sprintf(docHumPath, "humidity/data_%s", currentTime.c_str());
    moist = sprintf(docMoistPath, "soilmoisture/data_%s", currentTime.c_str());
    tempHumMoistPath = sprintf(RTDBTempPath, "test/temp/%d", String(temperature).c_str());
    String TempPath = String(docTempPath).c_str();
    String HumPath = String(docHumPath).c_str();
    String SoilPath = String(docMoistPath).c_str();
    String RTDBTemp= String(RTDBTempPath).c_str();
    
    FirebaseJson json1;
    json1.set("fields/temp/integerValue", String(temperature).c_str());
    json1.set("fields/timevalue/integerValue", timeValue);
    json1.set("fields/hourvalue/stringValue", currenthourFirestore.c_str());
    
    FirebaseJson json2;
    json2.set("fields/hum/integerValue", String(humidity).c_str());
    json2.set("fields/timevalue/integerValue", timeValue);
    json2.set("fields/hourvalue/stringValue", currenthourFirestore.c_str());

    FirebaseJson json3;
    json3.set("fields/moist/integerValue", String(soilMoistPercent).c_str());
    json3.set("fields/timevalue/integerValue", timeValue);
    json3.set("fields/hourvalue/stringValue", currenthourFirestore.c_str());

    Firebase.Firestore.createDocument(&fbdo, PROJ_ID, "", TempPath, json1.raw());
    Firebase.Firestore.createDocument(&fbdo, PROJ_ID, "", HumPath, json2.raw());
    Firebase.Firestore.createDocument(&fbdo, PROJ_ID, "", SoilPath, json3.raw());

    if (Firebase.RTDB.setInt(&fbdo, "test/temp", temperature)) {
      Serial.println("Temperature upload successful");
    } else {
      Serial.print("Temperature upload failed: ");
      Serial.println(fbdo.errorReason());
    }

    if (Firebase.RTDB.setInt(&fbdo, "test/hum", humidity)) {
      Serial.println("Humidity upload successful");
    } else {
      Serial.print("Humidity upload failed: ");
      Serial.println(fbdo.errorReason());
    }

    if (Firebase.RTDB.setInt(&fbdo, "test/soil", soilMoistPercent)) {
      Serial.println("Soil Moisture upload successful");
    } else {
      Serial.print("Soil Moisture upload failed: ");
      Serial.println(fbdo.errorReason());
    }

    Serial.println(timeValue);

    xSemaphoreGive(dataSemaphore);

    vTaskDelay(pdMS_TO_TICKS(60*10*1000)); // 10 minutes delay
    
  }
}

void controlMotorTask(void *parameter) {
  int step = 1;
  for (;;) {
    
    xSemaphoreTake(dataSemaphore, portMAX_DELAY);

    // Check if control button is pressed
    if (isMotorMoving) 
    {
      digitalWrite(MOSFET_PIN, LOW);  
      digitalWrite(STEPPER_PIN_1, HIGH);
      digitalWrite(STEPPER_PIN_2, HIGH);
      digitalWrite(STEPPER_PIN_3, HIGH);
      digitalWrite(STEPPER_PIN_4, HIGH);
      vTaskSuspend(humidityTaskHandle);
      vTaskSuspend(autoOnHandle);
      printf("Moving motor\n");

      // Rotate the stepper motor
      while (digitalRead(LIMIT_SWITCH_R) == HIGH && digitalRead(LIMIT_SWITCH_L) == HIGH) {
      myStepper.step(step);
      isMotorMoving = true;
      }
    }

    // Check if limit switch R is pressed
    if (digitalRead(LIMIT_SWITCH_R) == LOW) {
      digitalWrite(MOSFET_PIN, HIGH);
      myStepper.step(-500);
      digitalWrite(STEPPER_PIN_1, LOW);
      digitalWrite(STEPPER_PIN_2, LOW);
      digitalWrite(STEPPER_PIN_3, LOW);
      digitalWrite(STEPPER_PIN_4, LOW);
      Serial.printf ("motor has been stopped from limit switch R\n");
      step = -1;
      isMotorMoving = false;
      uploadLimitSwitchR = true;
      Serial.printf ("uploadlimitswitch R = %d\n", uploadLimitSwitchR);
    }

    // Check if limit switch L is pressed
    if (digitalRead(LIMIT_SWITCH_L) == LOW) {
      digitalWrite(MOSFET_PIN, HIGH);
      myStepper.step(500);
      digitalWrite(STEPPER_PIN_1, LOW);
      digitalWrite(STEPPER_PIN_2, LOW);
      digitalWrite(STEPPER_PIN_3, LOW);
      digitalWrite(STEPPER_PIN_4, LOW);
      Serial.printf ("motor has been stopped from limit switch L\n");
      step = 1;
      isMotorMoving = false;
      uploadLimitSwitchL = true;
      Serial.printf ("uploadlimitswitch L = %d\n", uploadLimitSwitchL);
    }
    vTaskResume(humidityTaskHandle);
    vTaskResume(autoOnHandle);
    xSemaphoreGive(dataSemaphore);
    vTaskDelay(pdMS_TO_TICKS(100));  // Delay for 100 milliseconds
  }
}

void updateMotorStatus(void *parameter){
  for(;;){
    xSemaphoreTake(dataSemaphore, portMAX_DELAY);
    if (uploadLimitSwitchL || uploadLimitSwitchR)
    {
      Serial.println("Updating motor status");
      if (Firebase.RTDB.setBool(&fbdo, "test/value", isMotorMoving)){
      Serial.println("Motor status updated");
      uploadLimitSwitchL = false;
      uploadLimitSwitchR = false;
      }
    }
    xSemaphoreGive(dataSemaphore);
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void displayOled(void *parameter){
  for(;;){
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(WHITE);
    display.setCursor(0, 0);
    display.println(currentHour);
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.print(day);
    display.print("/");
    display.println(currentDate);
    display.setTextSize(1);
    display.println("");
    display.print("Temp: ");
    display.print(temperature);
    display.println(" C");
    display.print("Hum: ");
    display.print(humidity);
    display.println(" %");
    display.print("Soil: ");
    display.print(soilMoistPercent);
    display.println(" %");
     if (isMotorMoving == true){
      display.println("Watering...");
    }
    else{
      display.println("Watering stopped");
    }
    display.display();
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void autoOn(void*parameter){
  for(;;){

    xSemaphoreTake(dataSemaphore, portMAX_DELAY);
    Serial.println(day);
    Serial.println (timeOn);
    Serial.println (currentTime);
    if (timeOn == currenthourFirestore && day == 1 && monday){
      Serial.println("Auto on initiated");
      Firebase.RTDB.setBool(&fbdo, "test/value", isMotorMoving);
      isMotorMoving = true;
      }
    else if (timeOn == currenthourFirestore && day == 2 && tuesday){
      Serial.println("Auto on initiated");
      Firebase.RTDB.setBool(&fbdo, "test/value", isMotorMoving);
      isMotorMoving = true;
      }
    else if (timeOn == currenthourFirestore && day == 3 && wednesday)
    {
      Serial.println("Auto on initiated");
      isMotorMoving = true;
      Firebase.RTDB.setBool(&fbdo, "test/value", isMotorMoving);
    }
    else if (timeOn == currenthourFirestore && day == 4 && thursday)
    {
      Serial.println("Auto on initiated");
      isMotorMoving = true;
      Firebase.RTDB.setBool(&fbdo, "test/value", isMotorMoving);
    }
    else if (timeOn == currenthourFirestore && day == 5 && friday)
    {
      Serial.println("Auto on initiated");
      isMotorMoving = true;
      Firebase.RTDB.setBool(&fbdo, "test/value", isMotorMoving);
    }
    else if (timeOn == currenthourFirestore  && day == 6 && saturday)
    {
      Serial.println("Auto on initiated");
      isMotorMoving = true;
      Firebase.RTDB.setBool(&fbdo, "test/value", isMotorMoving);
    }
    else if (timeOn == currenthourFirestore && day == 7 && sunday)
    {
      Serial.println("Auto on initiated");
      isMotorMoving = true;
      Firebase.RTDB.setBool(&fbdo, "test/value", isMotorMoving);
    }
    xSemaphoreGive(dataSemaphore);
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
