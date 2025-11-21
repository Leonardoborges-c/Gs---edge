#include <WiFi.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include "DHT.h"

// CONFIGURAÇÕES GERAIS

// Wi-Fi (Configurações Wokwi - Simulação)
const char* ssid = "Wokwi-GUEST";
const char* password = ""; // Senha vazia

// MQTT (broker público)
const char* mqtt_server = "broker.hivemq.com";
const uint16_t mqtt_port = 1883;

// Tópicos MQTT dedicados
const char* TOPIC_STATUS = "workspace/monitoramento/status";
const char* TOPIC_ALERT  = "workspace/monitoramento/alert";
const char* TOPIC_CMD    = "workspace/monitoramento/cmd";  // comandos externos

// HTTP Webhook (simples)
const char* http_endpoint = "http://example.com/webhook";

// Sensores e atuadores
#define DHTPIN 15    // Pino D15 (GPIO 15) para DHT22
#define DHTTYPE DHT22
#define LDR_PIN 34   // Pino VP (GPIO 34 / ADC1_CH6) para LDR
#define LED_PIN 2    // Pino D2 (GPIO 2) para LED
#define BUZZER_PIN 4 // Pino D4 (GPIO 4) para Buzzer

// Limiares (ajuste conforme ambiente real)
const float TEMP_THRESHOLD = 27.0;    // °C
const float HUM_THRESHOLD  = 70.0;    // %
const int   LDR_THRESHOLD  = 1500;    // 0-4095 (analogReadResolution(12) = 0 a 4095)

// Intervalos de leitura
const unsigned long SENSE_INTERVAL = 15UL * 1000UL;

// OBJETOS/GLOBAIS
DHT dht(DHTPIN, DHTTYPE);
WiFiClient espClient;
PubSubClient mqttClient(espClient);

unsigned long lastSense = 0;

// Wi-Fi
void setupWiFi() {
  Serial.print("Conectando ao WiFi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  // No Wokwi, a conexão geralmente é instantânea
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi conectado.");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nFalha ao conectar WiFi. Verifique as configurações de Wi-Fi.");
  }
}
// MQTT CALLBACK (recebe comandos)
void mqttCallback(char* topic, byte* message, unsigned int length) {
  String cmd = "";
  for (unsigned int i = 0; i < length; i++) cmd += (char)message[i];

  Serial.print("Comando recebido em ");
  Serial.print(topic);
  Serial.print(": ");
  Serial.println(cmd);

  if (cmd == "alert_test") {
    Serial.println("Comando MQTT: teste de alerta");
    // Aciona o buzzer e o LED brevemente
    tone(BUZZER_PIN, 2000, 400);
    digitalWrite(LED_PIN, HIGH);
    delay(500);
    digitalWrite(LED_PIN, LOW);
  }
}

// MQTT reconexão
void reconnectMQTT() {
  // Tenta reconectar até que seja bem-sucedido
  while (!mqttClient.connected()) {
    Serial.print("Conectando ao MQTT...");
    // Cria um ID de cliente único para evitar conflitos
    String clientId = "ESP32Monitor-" + String(random(0xffff), HEX);

    if (mqttClient.connect(clientId.c_str())) {
      Serial.println(" conectado!");
      // Subscreve ao tópico de comandos
      mqttClient.subscribe(TOPIC_CMD);
      Serial.print("Inscrito no tópico: ");
      Serial.println(TOPIC_CMD);
    } else {
      Serial.print("Falhou, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" tentando em 5s...");
      delay(5000);
    }
  }
}

// ALERTA VISUAL + AUDITIVO
void alertaVisualAudible() {
  // Blink e Beep por um curto período para chamar a atenção
  digitalWrite(LED_PIN, HIGH);
  tone(BUZZER_PIN, 2000); // 2kHz
  delay(800);
  noTone(BUZZER_PIN);
  digitalWrite(LED_PIN, LOW);
}

// MQTT – envio
void publishMQTT(const char* topic, String payload) {
  if (!mqttClient.connected()) reconnectMQTT();
  mqttClient.loop();

  // Garante que o tópico e o payload sejam do tipo correto
  boolean ok = mqttClient.publish(topic, payload.c_str());
  Serial.print("MQTT -> ");
  Serial.print(topic);
  Serial.print(" : ");
  Serial.println(ok ? "OK" : "FALHA");
}

// HTTP – envio
void sendHTTP(String json) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Sem WiFi → ignorando HTTP");
    return;
  }

  // O Wokwi simula o HTTP, então este bloco funcionará na simulação
  HTTPClient http;
  http.begin(http_endpoint);
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(json);

  Serial.print("HTTP POST para ");
  Serial.print(http_endpoint);
  Serial.print(" : ");
  Serial.println(code);
  http.end();
}

// SETUP
void setup() {
  Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  // Define a resolução de leitura analógica do ESP32 para 12 bits (0-4095)
  analogReadResolution(12);

  dht.begin();
  setupWiFi();

  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(mqttCallback);
}

// LOOP
void loop() {
  unsigned long now = millis();

  if (now - lastSense >= SENSE_INTERVAL) {
    lastSense = now;

    // Leitura dos sensores
    float temp = dht.readTemperature();
    float hum = dht.readHumidity();
    int ldr = analogRead(LDR_PIN);

    // O Wokwi permite simular a alteração dos valores (clique nos sensores)

    if (isnan(temp) || isnan(hum)) {
      Serial.println("Erro ao ler DHT22!");
      // Tenta ler novamente no próximo ciclo
      return;
    }

    // Payload padrão de status (JSON)
    String statusPayload = "{";
    statusPayload += "\"temp\":" + String(temp, 2) + ","; // 2 casas decimais
    statusPayload += "\"hum\":" + String(hum, 2) + ",";
    statusPayload += "\"ldr\":" + String(ldr) + ",";
    statusPayload += "\"ip\":\"" + WiFi.localIP().toString() + "\"";
    statusPayload += "}";

    Serial.println("🟦 Status: " + statusPayload);

    // Envio de dados
    publishMQTT(TOPIC_STATUS, statusPayload);
    sendHTTP(statusPayload);

    // Lógica de pausa inteligente
    bool badTemp = temp > TEMP_THRESHOLD;
    bool badHum  = hum > HUM_THRESHOLD;
    bool dark    = ldr < LDR_THRESHOLD; // Se LDR estiver baixo, significa que está escuro

    if (badTemp || badHum || dark) {
      Serial.println("⚠ Alerta detectado — sugerir pausa");

      alertaVisualAudible();

      // Monta o payload de alerta detalhado
      String alertPayload = "{";
      alertPayload += "\"alert\":true,";
      alertPayload += "\"temp\":" + String(temp, 1) + ",";
      alertPayload += "\"hum\":" + String(hum, 1) + ",";
      alertPayload += "\"ldr\":" + String(ldr) + ",";
      alertPayload += "\"reason\":\"";
      if (badTemp) alertPayload += "temperatura_alta ";
      if (badHum)  alertPayload += "umidade_alta ";
      if (dark)    alertPayload += "iluminacao_baixa ";
      alertPayload += "\"}";

      publishMQTT(TOPIC_ALERT, alertPayload);
    } else {
      Serial.println("Ambiente confortável — sem alerta.");
    }
  }

  // Manter conexão MQTT e processar comandos recebidos
  if (mqttClient.connected()) {
    mqttClient.loop();
  } else {
    // Tenta reconectar se a conexão WiFi estiver OK
    if (WiFi.status() == WL_CONNECTED) reconnectMQTT();
  }
}