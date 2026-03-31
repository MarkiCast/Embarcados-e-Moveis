#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <Wire.h>
#include <Adafruit_PN532.h>
#include <ArduinoJson.h>

// ---- CONFIG Wi-Fi ----
const char *ssid = "AP Ravi";
const char *pass = "!6bxupdTA5t";

// ---- CONFIG API ----
const char *api_server = "http://192.168.1.229:5000";

// ---- CONFIG PN532 (I2C) ----
#define PN532_IRQ -1
#define PN532_RESET -1
Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET);

// ---- CONFIG LEDs, BUZZER E BOTÃO ----
#define LED_VERDE D4       // GPIO2
#define LED_VERMELHO D6    // GPIO12
#define BUZZER D5          // GPIO14
#define BOTAO_CADASTRO D7  // GPIO13

// ---- VARIÁVEIS DE ESTADO ----
bool card_present = false;
bool modo_cadastro = false;
unsigned long last_read_time = 0;
unsigned long last_button_time = 0;
const unsigned long READ_INTERVAL = 2000;
const unsigned long DEBOUNCE_DELAY = 300;

void setup() {
  Serial.begin(115200);
  delay(10);

  // LEDs
  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_VERMELHO, OUTPUT);
  digitalWrite(LED_VERDE, HIGH);
  digitalWrite(LED_VERMELHO, HIGH);

  // Buzzer
  pinMode(BUZZER, OUTPUT);
  digitalWrite(BUZZER, LOW);

  // Botão
  pinMode(BOTAO_CADASTRO, INPUT_PULLUP);

  // ---- Conecta no WiFi ----
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, pass);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // ---- Inicializa PN532 ----
  Serial.println("Iniciando PN532 via I2C...");
  nfc.begin();

  Serial.println("Buscando PN532...");
  uint32_t versiondata = 0;

  for (int tentativa = 1; tentativa <= 5; tentativa++) {
    Serial.print("Tentativa ");
    Serial.print(tentativa);
    Serial.print("/5... ");
    versiondata = nfc.getFirmwareVersion();
    if (versiondata) {
      Serial.println("Encontrado!");
      break;
    }
    Serial.println("Falhou");
    delay(800);
  }

  if (!versiondata) {
    Serial.println("❌ PN532 NÃO encontrado! System continua...");
  } else {
    nfc.SAMConfig();
    Serial.println("✓ PN532 inicializado com sucesso!");
  }

  Serial.println("\n===========================================");
  Serial.println("SISTEMA PRONTO!");
  Serial.println("Aproxime um cartão para verificar acesso.");
  Serial.println("Pressione o botão para modo cadastro.");
  Serial.println("===========================================\n");
}

// ========================================================
// FUNÇÃO: Verificar se o cartão está autorizado
// ========================================================
bool checkCardInAPI(String uid) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi não conectado!");
    return false;
  }

  WiFiClient client;
  HTTPClient http;

  String url = String(api_server) + "/api/nfc/validate/" + uid;

  Serial.println("Verificando cartão na API...");
  Serial.println("URL: " + url);

  if (!http.begin(client, url)) {
    Serial.println("Erro ao iniciar conexão HTTP!");
    return false;
  }

  int httpCode = http.GET();

  if (httpCode <= 0) {
    Serial.printf("Erro GET: %s\n", http.errorToString(httpCode).c_str());
    http.end();
    return false;
  }

  Serial.printf("Código HTTP recebido: %d\n", httpCode);

  // ←← Aqui estava o problema!
  String payload = http.getString();
  http.end();

  Serial.println("DEBUG JSON recebido:");
  Serial.println(payload);

  // JSON da sua API é grande → precisa de documento maior
  StaticJsonDocument<1024> json;

  DeserializationError err = deserializeJson(json, payload);
  if (err) {
    Serial.print("Erro ao decodificar JSON: ");
    Serial.println(err.f_str());
    return false;
  }

  bool authorized = json["authorized"] | false;

  if (authorized) {
    Serial.println("✓ Cartão autorizado!");
    return true;
  } else {
    Serial.println("✗ Cartão NÃO autorizado.");
    return false;
  }
}



// ========================================================
// FUNÇÃO: Cadastrar cartão na API
// ========================================================
bool addCardToAPI(String uid) {
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClient client;
  HTTPClient http;

  String url = String(api_server) + "/api/nfc/sync";

  if (!http.begin(client, url)) {
    Serial.println("Erro: http.begin falhou!");
    return false;
  }

  http.addHeader("Content-Type", "application/json");

  // JSON pequeno, seguro
  StaticJsonDocument<128> doc;
  doc["nfc_card_uuid"] = uid;

  String json;
  serializeJson(doc, json);

  Serial.println("Cadastrando cartão na API...");

  int httpCode = http.POST(json);
  if (httpCode <= 0) {
    Serial.println("Erro HTTP: " + http.errorToString(httpCode));
    http.end();
    return false;
  }

  Serial.printf("Código HTTP: %d\n", httpCode);
  Serial.print("Resposta:\n");

  // ⚠️ NÃO usa getString() – evita estouro de heap  
  WiFiClient *stream = http.getStreamPtr();

  // buffer fixo = seguro
  const size_t CAPACITY = 1024;
  StaticJsonDocument<CAPACITY> response;

  // Lê o stream JSON direto, incremental
  DeserializationError err = deserializeJson(response, *stream);

  if (err) {
    Serial.print("Erro ao ler JSON: ");
    Serial.println(err.f_str());
    http.end();
    return false;
  }

  http.end();

  // Lendo valores do JSON
  const char *status = response["status"] | "";
  const char *msg = response["message"] | "";

  if ((httpCode == 200 || httpCode == 201) && strcmp(status, "success") == 0) {
    Serial.println("✓ Cartão cadastrado com sucesso!");
    return true;
  }

  Serial.print("✗ Erro ao cadastrar: ");
  Serial.println(msg);

  return false;
}


// ========================================================
// FEEDBACKS VISUAIS / SONOROS
// ========================================================
void feedbackAutorizado() {
  digitalWrite(LED_VERDE, LOW);
  digitalWrite(LED_VERMELHO, HIGH);

  for (int i = 0; i < 3; i++) {
    digitalWrite(BUZZER, HIGH); delay(120);
    digitalWrite(BUZZER, LOW); delay(120);
  }

  delay(1200);
  digitalWrite(LED_VERDE, HIGH);
}

void feedbackNegado() {
  digitalWrite(LED_VERMELHO, LOW);
  digitalWrite(LED_VERDE, HIGH);

  for (int i = 0; i < 2; i++) {
    digitalWrite(BUZZER, HIGH); delay(400);
    digitalWrite(BUZZER, LOW); delay(250);
  }

  digitalWrite(LED_VERMELHO, HIGH);
}

void feedbackCadastroSucesso() {
  for (int i = 0; i < 5; i++) {
    digitalWrite(LED_VERDE, LOW); delay(100);
    digitalWrite(LED_VERDE, HIGH); delay(100);
  }
}

void feedbackCadastroErro() {
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_VERMELHO, LOW); delay(250);
    digitalWrite(LED_VERMELHO, HIGH); delay(200);
  }
}

void feedbackModoCadastro() {
  digitalWrite(LED_VERDE, LOW);
  digitalWrite(LED_VERMELHO, HIGH);
}

// ========================================================
// LOOP PRINCIPAL
// ========================================================
void loop() {

  // ---------------- BOTÃO DE CADASTRO ----------------
  if (digitalRead(BOTAO_CADASTRO) == LOW && !modo_cadastro) {
    unsigned long t = millis();

    if (t - last_button_time > DEBOUNCE_DELAY) {
      modo_cadastro = true;

      Serial.println("\n=== MODO CADASTRO ATIVADO! ===");
      Serial.println("Aproxime o cartão para cadastrar.\n");

      feedbackModoCadastro();
      last_button_time = t;
    }
  }

  // ---------------- LEITURA NFC ----------------
  uint8_t uid[7];
  uint8_t uidLength;

  bool found = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 100);

  if (found) {
    unsigned long t = millis();

    if (!card_present || (t - last_read_time > READ_INTERVAL)) {

      String uidStr = "";
      for (int i = 0; i < uidLength; i++) {
        if (uid[i] < 0x10) uidStr += "0";
        uidStr += String(uid[i], HEX);
      }

      Serial.println("\n==========================================");
      Serial.print("Cartão detectado! UID: ");
      Serial.println(uidStr);

      if (modo_cadastro) {
        bool ok = addCardToAPI(uidStr);

        if (ok) feedbackCadastroSucesso();
        else feedbackCadastroErro();

        modo_cadastro = false;
        Serial.println("Modo cadastro DESATIVADO.");
      } else {
        bool authorized = checkCardInAPI(uidStr);

        if (authorized) feedbackAutorizado();
        else feedbackNegado();
      }

      Serial.println("==========================================\n");

      card_present = true;
      last_read_time = t;
    }
  }
  else {
    if (card_present) {
      Serial.println("Cartão removido.");
      card_present = false;
    }

    if (modo_cadastro) digitalWrite(LED_VERDE, LOW);
    else digitalWrite(LED_VERDE, HIGH);

    digitalWrite(LED_VERMELHO, HIGH);
    digitalWrite(BUZZER, LOW);
  }

  delay(100);
}