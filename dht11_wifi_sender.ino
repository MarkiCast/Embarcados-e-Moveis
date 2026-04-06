#include <dht11.h>

#if defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#elif defined(ESP32)
#include <WiFi.h>
#include <HTTPClient.h>
#else
#error "Placa nao suportada para este sketch"
#endif

#define DHT11PIN 4

dht11 DHT11;

const char* wifiSsid = "Frodo";
const char* wifiPassword = "marceloo";
const char* serverUrl = "http://192.168.43.168:8000/api/device-message";
const char* deviceId = "esp-dht11-01";

const unsigned long sendIntervalMs = 2000;
const unsigned long wifiConnectTimeoutMs = 20000;
unsigned long lastSendMs = 0;
unsigned long messageCounter = 0;

bool connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSsid, wifiPassword);
  Serial.print("Conectando no Wi-Fi ");
  Serial.println(wifiSsid);

  unsigned long startedAt = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - startedAt > wifiConnectTimeoutMs) {
      Serial.println("Falha ao conectar no Wi-Fi");
      return false;
    }
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("Wi-Fi conectado");
  Serial.print("IP local: ");
  Serial.println(WiFi.localIP());
  Serial.print("Servidor alvo: ");
  Serial.println(serverUrl);
  return true;
}

void sendDhtReading() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Sem Wi-Fi, envio ignorado");
    return;
  }

  int chk = DHT11.read(DHT11PIN);
  if (chk != 0) {
    Serial.print("Falha leitura DHT11, codigo: ");
    Serial.println(chk);
    return;
  }

  float humidity = (float)DHT11.humidity;
  float temperature = (float)DHT11.temperature;

  Serial.print("Humidity (%): ");
  Serial.println(humidity, 2);
  Serial.print("Temperature (C): ");
  Serial.println(temperature, 2);

  WiFiClient client;
  HTTPClient http;

  if (!http.begin(client, serverUrl)) {
    Serial.println("Erro ao iniciar HTTP");
    return;
  }

  http.addHeader("Content-Type", "application/json");

  String payload = "{";
  payload += "\"deviceId\":\"" + String(deviceId) + "\",";
  payload += "\"counter\":" + String(messageCounter) + ",";
  payload += "\"tempC\":" + String(temperature, 2) + ",";
  payload += "\"humidity\":" + String(humidity, 2) + ",";
  payload += "\"millis\":" + String(millis());
  payload += "}";

  int httpCode = http.POST(payload);
  if (httpCode > 0) {
    String responseBody = http.getString();
    Serial.print("HTTP ");
    Serial.print(httpCode);
    Serial.print(" -> ");
    Serial.println(responseBody);
  } else {
    Serial.print("Falha HTTP: ");
    Serial.println(http.errorToString(httpCode));
  }

  http.end();
  messageCounter++;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  connectWiFi();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED && !connectWiFi()) {
    delay(2000);
    return;
  }

  unsigned long now = millis();
  if (now - lastSendMs >= sendIntervalMs) {
    lastSendMs = now;
    sendDhtReading();
  }
}
