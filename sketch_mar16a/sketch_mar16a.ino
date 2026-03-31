#if defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#elif defined(ESP32)
#include <WiFi.h>
#include <HTTPClient.h>
#else
#error "Placa nao suportada para este sketch"
#endif

const char* wifiSsid = "Frodo";
const char* wifiPassword = "marceloo";
const char* serverUrl = "http://192.168.43.168:8000/api/device-message";
const char* deviceId = "esp32-teste-01";

const unsigned long sendIntervalMs = 10000;
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

void sendMessage() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Sem Wi-Fi, envio ignorado");
    return;
  }

  HTTPClient http;
#if defined(ESP8266)
  WiFiClient client;
  http.begin(client, serverUrl);
#else
  http.begin(serverUrl);
#endif
  http.addHeader("Content-Type", "application/json");

  String payload = "{";
  payload += "\"deviceId\":\"" + String(deviceId) + "\",";
  payload += "\"counter\":" + String(messageCounter) + ",";
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
    sendMessage();
  }
}
