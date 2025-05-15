#include <Adafruit_BMP280.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_HTS221.h>
#include <Adafruit_SHT4x.h>
#include <Wire.h>
#include <WiFi.h>
#include <PubSubClient.h>

// Wifi Setup
const char* ssid           = "TP-Link_E94F";
const char* wifi_password  = "61557498";

// NETPIE
const char* mqtt_server    = "192.168.0.197";
const char* clientID       = "YJmNOBuhHsOxqEeB";
const char* username       = "esp32";
const char* password_mqtt  = "public1121";

// Sensor
Adafruit_BMP280   bmp;    // BMP280 (0x76 or 0x77)
Adafruit_HTS221   hts;    // HTS221 (0x5F)
Adafruit_SHT4x    sht4;   // SHT4x  (0x44)
Adafruit_MPU6050  mpu;    // MPU6050 (0x68 or 0x69)
 
// Flags for each sensor
bool hasBMP     = false;
bool hasHTS221  = false;
bool hasSHT4x   = false;
bool hasMPU     = false;
 
// Queue
struct SensorData {
  float temperature;
  float humidity;
  float pressure;
  float accelX, accelY, accelZ;
  float gyroX, gyroY, gyroZ;
};
 
#define QUEUE_SIZE 10
SensorData queueData[QUEUE_SIZE];
int queueFront = 0;
int queueBack  = 0;
 
void enqueueData(SensorData data) {
  if (queueBack < QUEUE_SIZE) {
    queueData[queueBack++] = data;
  } else {
    Serial.println("Queue is full! Data lost.");
  }
}
 
bool dequeueData(SensorData &outData) {
  if (queueFront < queueBack) {
    outData = queueData[queueFront++];
    return true;
  } else {
    // reset queue if empty
    queueFront = 0;
    queueBack  = 0;
    return false;
  }
}

// WiFi
void setupWiFi(){
  WiFi.begin(ssid, wifi_password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWi-Fi connected!");
}



// MQTT //new
WiFiClient espClient;
PubSubClient client(espClient);
 
void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect(clientID, username, password_mqtt)) {
      Serial.println("Connected to EMQX!");
      client.subscribe("test/prediction"); // Subscribe to predictions
    } else {
      Serial.print("Failed, rc=");
      Serial.print(client.state());
      Serial.println(" Trying again in 5 seconds...");
      delay(5000);
    }
  }
}

// new
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("]: ");

  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.println(message);

  // You can parse the JSON if needed
  // Example: extract a predicted value and use it
}

 
// Thread 1
unsigned long lastReadMillis = 0;
const unsigned long READ_INTERVAL = 60000; // 1 minute
 
void readSensorsTask() {
  SensorData sdata;
  sdata.temperature = 0.0;
  sdata.humidity    = 0.0;
  sdata.pressure    = 0.0;
  sdata.accelX      = 0.0;
  sdata.accelY      = 0.0;
  sdata.accelZ      = 0.0;
  sdata.gyroX       = 0.0;
  sdata.gyroY       = 0.0;
  sdata.gyroZ       = 0.0;
 
  // Temperature/Humidity 
  if (hasHTS221) {
    sensors_event_t humEvent, tempEvent;
    hts.getEvent(&humEvent, &tempEvent);
    sdata.temperature = tempEvent.temperature;
    sdata.humidity    = humEvent.relative_humidity;
  } else if (hasSHT4x) {
    sensors_event_t shtHum, shtTemp;
    sht4.getEvent(&shtHum, &shtTemp);
    sdata.temperature = shtTemp.temperature;
    sdata.humidity    = shtHum.relative_humidity;
  }
 
  // Pressure
  if (hasBMP) {
    float pressurePa = bmp.readPressure(); 
    sdata.pressure   = pressurePa / 100.0F; 
  }
 
  // MPU6050 
  if (hasMPU) {
    sensors_event_t a, g, tempMPU;
    mpu.getEvent(&a, &g, &tempMPU);
    sdata.accelX = a.acceleration.x;
    sdata.accelY = a.acceleration.y;
    sdata.accelZ = a.acceleration.z;
    sdata.gyroX  = g.gyro.x;
    sdata.gyroY  = g.gyro.y;
    sdata.gyroZ  = g.gyro.z;
  }
 
  enqueueData(sdata);
  Serial.println("Thread 1: Read and enqueue data.");
}
 

// Thread 2
unsigned long lastPublishMillis = 0;
const unsigned long PUBLISH_INTERVAL = 15000; // 15 seconds
 
void publishTask() {
  SensorData data;
  if (dequeueData(data)) {
    // JSON
    String payload = "{ \"data\": { ";
    payload += "\"temp\": " + String(data.temperature) + ", ";
    payload += "\"humid\": " + String(data.humidity) + ", ";
    payload += "\"pressure\": " + String(data.pressure) + ", ";
    payload += "\"accel\": [" + String(data.accelX) + "," + String(data.accelY) + "," + String(data.accelZ) + "], ";
    payload += "\"gyro\": [" + String(data.gyroX) + "," + String(data.gyroY) + "," + String(data.gyroZ) + "] ";
    payload += "} }";
 
    client.publish("test/aask", payload.c_str());
    Serial.println("Thread 2: Publishing... " + payload);
  } else {
    Serial.println("Thread 2: Empty");
  }
}
 
// Initialization
void setupHardware() {
  // I2C pins: SDA=41, SCL=40, freq=100kHz
  Wire.begin(41, 40, 100000);
 
  if (bmp.begin(0x76)) {
    hasBMP = true;
    Serial.println("BMP280 sensor connected!");
  } else {
    Serial.println("BMP280 not found");
  }
 
  if (hts.begin_I2C()) {
    hasHTS221 = true;
    Serial.println("HTS221 sensor connected!");
  } else {
    Serial.println("HTS221 not found");
  }
 
  if (!hasHTS221) {
    if (sht4.begin()) {
      hasSHT4x = true;
      Serial.println("SHT4x sensor connected!");
    } else {
      Serial.println("SHT4x not found");
    }
  }
 
  if (mpu.begin()) {
    hasMPU = true;
    Serial.println("MPU6050 sensor connected!");
  } else {
    Serial.println("MPU6050 not found");
  }
 
  pinMode(2, OUTPUT);
  digitalWrite(2, HIGH);
}

void setup() {
  Serial.begin(115200);

  setupHardware();
  setupWiFi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback); //new
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
 
  unsigned long currentMillis = millis();
 
  // Thread 1
  if (currentMillis - lastReadMillis >= READ_INTERVAL) {
    lastReadMillis = currentMillis;
    readSensorsTask();
  }
 
  // Thread 2
  if (currentMillis - lastPublishMillis >= PUBLISH_INTERVAL) {
    lastPublishMillis = currentMillis;
    publishTask();
  }
}
