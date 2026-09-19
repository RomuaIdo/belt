#pragma once

#include <Arduino.h>

// Gera o JSON de sendMessage; retorna vazio se destinatario ou mensagem forem vazios.
// chatId e o identificador do chat no Telegram, nao um numero de telefone.
String createJsonPayload(const String& chatId, const String& message);

// Requer Wi-Fi conectado, relogio valido e certificado CA confiavel em PEM,
// mantido valido durante toda a chamada. Nao ha fallback para TLS sem verificacao.
// Retorna o status HTTP ou um valor negativo em caso de falha local/transporte
// (-1 para argumentos vazios ou falha de inicializacao). Nao interpreta o JSON
// de resposta do Telegram. Os limites de conexao, handshake e leitura sao 10 s
// cada; nao representam um prazo total de 10 s para a chamada.
int sendTelegramMessage(const String& botToken, const String& jsonPayload,
                        const char* caCertificate);
