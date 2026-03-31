#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>

const char* ssid = "Frodo";
const char* password = "marceloo";
const char* serverHost = "192.168.43.168";
const uint16_t serverPort = 8000;
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

bool ensureWiFiConnected() {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  Serial.print("Conectando no Wi-Fi ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < 15000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  return WiFi.status() == WL_CONNECTED;
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

  if (ensureWiFiConnected()) {
    Serial.println("Wi-Fi conectado no setup");
    printWiFiInfo();
  } else {
    Serial.println("Falha ao conectar no setup");
  }
  Serial.print("Servidor alvo: ");
  Serial.print(serverHost);
  Serial.print(":");
  Serial.println(serverPort);
}

void loop() {
  if (ensureWiFiConnected()) {
    Serial.println("Wi-Fi conectado");
    printWiFiInfo();

    WiFiClient client;
    HTTPClient http;
    String pingUrl = String("http://") + serverHost + ":" + String(serverPort) + "/api/ping";
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
    String postUrl = String("http://") + serverHost + ":" + String(serverPort) + "/api/device-message";
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
