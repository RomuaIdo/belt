#pragma once

#include <Arduino.h>
#include <atomic>

// Um envio ao Telegram rodando numa task FreeRTOS propria.
// Ciclo: livre -> enviando (start) -> terminado (isDone) -> livre (release).
// Enquanto envia, so a task mexe no objeto. Nao pode ser copiado nem movido
// (a task guarda o endereco), entao deve ficar num array fixo.
class TelegramTask {
public:
    // Gera o JSON de sendMessage; vazio se chatId ou message forem vazios.
    // chatId e o identificador do chat no Telegram, nao um numero de telefone.
    static String createJsonPayload(const String& chatId, const String& message);

    // Inicia o envio em segundo plano, sem esperar a resposta. Retorna false se
    // ja estiver em uso, se algum dado for vazio ou se a task nao puder ser criada.
    // Requer Wi-Fi conectado e relogio valido (TLS com certificado verificado).
    bool startTask(const String& token, const String& event, const String& chat,
               const String& message);

    // Chamada bloqueante (ate ~30 s se a rede falhar) a qualquer metodo da API do
    // Telegram, como "getMe" ou "getUpdates". Retorna o status HTTP, ou um valor
    // negativo em falha local/de rede. Se `response` nao for nulo, recebe o corpo.
    static int request(const String& token, const char* method, const String& jsonBody,
                       String* response = nullptr);

    bool isFree() const { return !busy; }
    bool isDone() const { return busy && httpStatus != 0; }
    void release() { busy = false; }

    // Valido quando isDone(): status HTTP ou valor negativo em falha de rede.
    int getHttpStatus() const { return httpStatus; }
    const String& getEventId() const { return eventId; }
    const String& getChatId() const { return chatId; }

private:
    static void run(void* self);  // ponte para o xTaskCreate

    bool busy = false;
    String botToken;
    String eventId;
    String chatId;
    String payload;
    std::atomic<int> httpStatus{0};  // 0 enquanto a task roda
};
