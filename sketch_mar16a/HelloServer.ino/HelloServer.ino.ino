#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
const char* ssid = "Frodo";
const char* password = "marceloo";

ESP8266WebServer server(80);
String latestBody = "";
String latestClientIp = "";
unsigned long messagesReceived = 0;

void handleHealth() {
  String body = "{";
  body += "\"status\":\"ok\",";
  body += "\"messagesReceived\":" + String(messagesReceived);
  body += "}";
  server.send(200, "application/json", body);
}

void handleLastMessage() {
  String body = "{";
  body += "\"messagesReceived\":" + String(messagesReceived) + ",";
  body += "\"latestClientIp\":\"" + latestClientIp + "\",";
  body += "\"latestBody\":" + (latestBody.length() > 0 ? latestBody : "null");
  body += "}";
  server.send(200, "application/json", body);
}

void handleDeviceMessage() {
  String body = server.arg("plain");
  if (body.length() == 0) {
    server.send(400, "application/json", "{\"error\":\"json vazio\"}");
    return;
  }

  latestBody = body;
  latestClientIp = server.client().remoteIP().toString();
  messagesReceived++;

  String response = "{";
  response += "\"ok\":true,";
  response += "\"messagesReceived\":" + String(messagesReceived);
  response += "}";
  server.send(200, "application/json", response);

  Serial.print("Mensagem recebida de ");
  Serial.print(latestClientIp);
  Serial.print(" -> ");
  Serial.println(latestBody);
}

void handleNotFound() {
  server.send(404, "application/json", "{\"error\":\"rota nao encontrada\"}");
}

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("Wi-Fi conectado. IP do servidor ESP: ");
  Serial.println(WiFi.localIP());

  server.on("/health", HTTP_GET, handleHealth);
  server.on("/api/last-message", HTTP_GET, handleLastMessage);
  server.on("/api/device-message", HTTP_POST, handleDeviceMessage);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("Servidor HTTP ESP8266 ativo");
}

void loop() {
  server.handleClient();
}
