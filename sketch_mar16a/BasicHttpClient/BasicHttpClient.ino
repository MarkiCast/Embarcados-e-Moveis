#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>

ESP8266WiFiMulti WiFiMulti;
const char* ssid = "Frodo";
const char* password = "marceloo";
const char* serverBaseUrl = "http://192.168.43.210:8000";
unsigned long counter = 0;

void printWiFiInfo() {
  Serial.print("Wi-Fi status: ");
  Serial.println(WiFi.status());
  Serial.print("IP local: ");
  Serial.println(WiFi.localIP());
  Serial.print("Gateway: ");
  Serial.println(WiFi.gatewayIP());
  Serial.print("DNS: ");
  Serial.println(WiFi.dnsIP());
  Serial.print("RSSI: ");
  Serial.println(WiFi.RSSI());
}

void setup() {
  Serial.begin(115200);

  Serial.println();
  Serial.println();
  Serial.println();

  for (uint8_t t = 4; t > 0; t--) {
    Serial.printf("[SETUP] WAIT %d...\n", t);
    Serial.flush();
    delay(1000);
  }

  WiFi.mode(WIFI_STA);
  WiFiMulti.addAP(ssid, password);
  Serial.print("Wi-Fi SSID: ");
  Serial.println(ssid);
  Serial.print("Servidor alvo: ");
  Serial.println(serverBaseUrl);
}

void loop() {
  if ((WiFiMulti.run() == WL_CONNECTED)) {
    Serial.println("Wi-Fi conectado");
    printWiFiInfo();

    WiFiClient client;
    HTTPClient http;
    String pingUrl = String(serverBaseUrl) + "/api/ping";
    Serial.print("[HTTP] GET ");
    Serial.println(pingUrl);
    if (http.begin(client, pingUrl)) {
      int httpCode = http.GET();
      if (httpCode > 0) {
        Serial.printf("[HTTP] GET code: %d\n", httpCode);
        if (httpCode == HTTP_CODE_OK) {
          String payload = http.getString();
          Serial.println(payload);
        }
      } else {
        Serial.printf("[HTTP] GET falhou: %s\n", http.errorToString(httpCode).c_str());
      }
      http.end();
    } else {
      Serial.println("[HTTP] GET sem conexao");
    }

    HTTPClient httpPost;
    String postUrl = String(serverBaseUrl) + "/api/device-message";
    if (httpPost.begin(client, postUrl)) {
      httpPost.addHeader("Content-Type", "application/json");
      String body = "{";
      body += "\"deviceId\":\"esp8266-basic-http\",";
      body += "\"counter\":" + String(counter) + ",";
      body += "\"millis\":" + String(millis());
      body += "}";
      int postCode = httpPost.POST(body);
      if (postCode > 0) {
        Serial.printf("[HTTP] POST code: %d\n", postCode);
        Serial.println(httpPost.getString());
      } else {
        Serial.printf("[HTTP] POST falhou: %s\n", httpPost.errorToString(postCode).c_str());
      }
      httpPost.end();
    } else {
      Serial.println("[HTTP] POST sem conexao");
    }
    counter++;
  } else {
    Serial.println("Wi-Fi desconectado");
    Serial.print("Wi-Fi status: ");
    Serial.println(WiFi.status());
  }

  delay(10000);
}
