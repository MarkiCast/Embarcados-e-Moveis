#if defined(ESP8266)
#include <ESP8266WiFi.h>
#elif defined(ESP32)
#include <WiFi.h>
#else
#error "Placa nao suportada para este sketch"
#endif

const char* wifiSsid = "Frodo";
const char* wifiPassword = "marceloo";
const char* serverHost = "192.168.43.168";
const uint16_t serverPort = 8000;
const char* healthPath = "/health";

const unsigned long sendIntervalMs = 10000;
const unsigned long wifiConnectTimeoutMs = 20000;
const unsigned long responseTimeoutMs = 5000;
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
  Serial.print(serverHost);
  Serial.print(":");
  Serial.println(serverPort);
  return true;
}

void checkHealth() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Sem Wi-Fi, teste ignorado");
    return;
  }

  WiFiClient client;
  Serial.print("Teste TCP para ");
  Serial.print(serverHost);
  Serial.print(":");
  Serial.println(serverPort);
  if (!client.connect(serverHost, serverPort)) {
    Serial.println("Falha TCP: nao conectou no host/porta");
    return;
  }

  client.print(String("GET ") + healthPath + " HTTP/1.1\r\n");
  client.print(String("Host: ") + serverHost + "\r\n");
  client.print("Connection: close\r\n\r\n");

  unsigned long startedAt = millis();
  while (!client.available()) {
    if (millis() - startedAt > responseTimeoutMs) {
      Serial.println("Sem resposta do /health (timeout)");
      client.stop();
      return;
    }
    delay(10);
  }

  String statusLine = client.readStringUntil('\n');
  statusLine.trim();
  Serial.print("Resposta /health: ");
  Serial.println(statusLine);

  String body;
  while (client.available()) {
    body += client.readString();
  }
  body.trim();
  if (body.length() > 0) {
    Serial.print("Corpo: ");
    Serial.println(body);
  }
  client.stop();

  messageCounter++;
  Serial.print("Checks enviados: ");
  Serial.println(messageCounter);
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
    checkHealth();
  }
}
