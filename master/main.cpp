//Antes de compilar na Arduino IDE, instale a biblioteca ArduinoJson (por Benoit Blanchon) no Gerenciador de Bibliotecas.

#include <WiFi.h>
#include <esp_now.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// --- Configurações do Ponto de Acesso (Master) ---
const char* apSSID = "CintoAlerta_Setup";
const byte DNS_PORT = 53;

WebServer server(80);
DNSServer dnsServer;

// --- Estruturas de Dados ---
struct Configuracao {
  String wifiSSID;
  String wifiPass;
  String telegramToken;
  String telegramChatID;
  String slaveMac; // Para simplificar, armazenando 1 MAC. Pode ser expandido para array.
  String slaveName;
};

Configuracao configAtual;

// --- Flags de Controle ---
volatile bool quedaDetectada = false;
String usuarioQueda = "";
uint8_t macQueda[6];

// --- HTML da Interface Web (Simplificada) ---
const char* htmlPage = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Cinto Alerta</title>
  <style>
    body { font-family: Arial, sans-serif; margin: 0; padding: 20px; background: #f4f4f9; }
    .container { max-width: 400px; margin: auto; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 4px 6px rgba(0,0,0,0.1); }
    h1 { color: #d9534f; text-align: center; }
    label { font-weight: bold; display: block; margin-top: 15px; }
    input { width: 100%; padding: 8px; margin-top: 5px; border: 1px solid #ccc; border-radius: 4px; box-sizing: border-box; }
    button { width: 100%; padding: 10px; background: #5cb85c; color: white; border: none; border-radius: 4px; margin-top: 20px; font-size: 16px; cursor: pointer; }
    button:hover { background: #4cae4c; }
  </style>
</head>
<body>
  <div class="container">
    <h1>Cinto Alerta</h1>
    <form action="/salvar" method="POST">
      <h3>1. Internet (Wi-Fi)</h3>
      <label>Nome da Rede (SSID):</label> <input type="text" name="ssid" required>
      <label>Senha:</label> <input type="password" name="pass">
      
      <h3>2. Configuração do Slave</h3>
      <label>MAC Address (ex: AA:BB:CC:DD:EE:FF):</label> <input type="text" name="mac" required>
      <label>Nome do Usuário (Idoso):</label> <input type="text" name="nome" required>
      
      <h3>3. Contatos (Telegram)</h3>
      <label>Bot Token:</label> <input type="text" name="token" required>
      <label>Chat ID do Contato:</label> <input type="text" name="chatid" required>
      
      <button type="submit">Salvar Configurações</button>
    </form>
  </div>
</body>
</html>
)rawliteral";

// --- Funções de Arquivo (JSON) ---
void carregarConfig() {
  if (LittleFS.exists("/config.json")) {
    File file = LittleFS.open("/config.json", "r");
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    if (!error) {
      configAtual.wifiSSID = doc["wifiSSID"] | "";
      configAtual.wifiPass = doc["wifiPass"] | "";
      configAtual.telegramToken = doc["telegramToken"] | "";
      configAtual.telegramChatID = doc["telegramChatID"] | "";
      configAtual.slaveMac = doc["slaveMac"] | "";
      configAtual.slaveName = doc["slaveName"] | "";
      Serial.println("Configurações carregadas com sucesso!");
    }
    file.close();
  }
}

void salvarConfig() {
  File file = LittleFS.open("/config.json", "w");
  JsonDocument doc;
  doc["wifiSSID"] = configAtual.wifiSSID;
  doc["wifiPass"] = configAtual.wifiPass;
  doc["telegramToken"] = configAtual.telegramToken;
  doc["telegramChatID"] = configAtual.telegramChatID;
  doc["slaveMac"] = configAtual.slaveMac;
  doc["slaveName"] = configAtual.slaveName;
  
  serializeJson(doc, file);
  file.close();
  Serial.println("Configurações salvas no LittleFS.");
}

// --- Envio de Mensagem Telegram ---
void enviarAlertaTelegram(String mensagem) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi não conectado. Impossível enviar alerta.");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure(); // Não valida o certificado SSL para simplificar
  HTTPClient http;

  String url = "https://api.telegram.org/bot" + configAtual.telegramToken + "/sendMessage";
  String payload = "{\"chat_id\": \"" + configAtual.telegramChatID + "\", \"text\": \"" + mensagem + "\"}";

  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  
  int httpResponseCode = http.POST(payload);
  if (httpResponseCode > 0) {
    Serial.printf("Mensagem enviada. Código HTTP: %d\n", httpResponseCode);
  } else {
    Serial.printf("Erro ao enviar mensagem: %s\n", http.errorToString(httpResponseCode).c_str());
  }
  http.end();
}

// --- Callback do ESP-NOW ---
void onDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  // Convertemos o MAC recebido para String para comparar com o salvo
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
           
  Serial.printf("Sinal recebido de: %s\n", macStr);

  // Se for o MAC que cadastramos, acionamos o alerta e enviamos o ACK
  if (String(macStr).equalsIgnoreCase(configAtual.slaveMac)) {
    usuarioQueda = configAtual.slaveName;
    memcpy(macQueda, mac, 6);
    quedaDetectada = true; // Avisa o loop() para mandar o Telegram
    
    // Envia o ACK de volta para o Slave parar o Loop
    const char* ackMsg = "ACK";
    esp_now_send(mac, (uint8_t *)ackMsg, strlen(ackMsg));
    Serial.println("ACK enviado para o Slave.");
  }
}

// --- Configuração do Web Server ---
void setupWebServer() {
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", htmlPage);
  });

  server.on("/salvar", HTTP_POST, []() {
    if (server.hasArg("ssid")) configAtual.wifiSSID = server.arg("ssid");
    if (server.hasArg("pass")) configAtual.wifiPass = server.arg("pass");
    if (server.hasArg("mac")) configAtual.slaveMac = server.arg("mac");
    if (server.hasArg("nome")) configAtual.slaveName = server.arg("nome");
    if (server.hasArg("token")) configAtual.telegramToken = server.arg("token");
    if (server.hasArg("chatid")) configAtual.telegramChatID = server.arg("chatid");
    
    salvarConfig();
    
    // Conectar ao Wi-Fi configurado
    WiFi.begin(configAtual.wifiSSID.c_str(), configAtual.wifiPass.c_str());
    
    server.send(200, "text/html", "<h2>Configuracoes salvas! O dispositivo ira tentar se conectar ao Wi-Fi agora.</h2><a href='/'>Voltar</a>");
  });

  // Portal Cativo: Redireciona qualquer outra URL para a raiz
  server.onNotFound([]() {
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
  });

  server.begin();
}

void setup() {
  Serial.begin(115200);
  
  // Inicia sistema de arquivos
  if (!LittleFS.begin(true)) {
    Serial.println("Erro ao montar o LittleFS");
    return;
  }
  
  carregarConfig();

  // Configura Wi-Fi como Access Point (para config) e Station (para Telegram/Internet)
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apSSID);
  
  // Inicia Portal Cativo (DNS)
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  Serial.print("Access Point Iniciado. IP: ");
  Serial.println(WiFi.softAPIP());

  // Tenta conectar na rede configurada
  if (configAtual.wifiSSID != "") {
    WiFi.begin(configAtual.wifiSSID.c_str(), configAtual.wifiPass.c_str());
  }

  setupWebServer();

  // Inicializa ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Erro ao inicializar ESP-NOW");
    return;
  }
  esp_now_register_recv_cb(onDataRecv);
  
  // Como responderemos ao slave (ACK), precisamos registrar um peer vazio inicial
  // O peer será adicionado dinamicamente caso necessário ou podemos usar modo broadcast, 
  // mas o ESP-NOW gerencia retornos unicas baseados no MAC que enviou.
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();

  // Verifica se o callback avisou de uma queda
  if (quedaDetectada) {
    quedaDetectada = false; // Reseta a flag imediatamente
    
    Serial.println("--- ALERTA DE QUEDA PROCESSADO ---");
    String mensagem = "🚨 ALERTA: O usuário " + usuarioQueda + " enviou um pedido de socorro!";
    
    // Dispara a API
    enviarAlertaTelegram(mensagem);
    
    // (Opcional) Adicione aqui som de buzzer na placa Master
  }
}