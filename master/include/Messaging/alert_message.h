
#pragma once

#include <Arduino.h>
#include <time.h>

// Mesma mensagem padrao do frontend (DEFAULT_MSG em frontend/app.js).
extern const char* const DEFAULT_ALERT_MESSAGE;

// Substitui os placeholders do modelo do alerta:
//   {nome} e {apelido} -> displayName
//   {hora} -> HH:MM e {data} -> DD/MM/YYYY (fuso local do processo)
// Modelo vazio usa DEFAULT_ALERT_MESSAGE. Sem relogio valido, hora e data
// saem como "--:--" e "--/--/----". Placeholders desconhecidos ficam como estao.
String formatAlertMessage(const String& messageTemplate, const String& displayName,
                          time_t eventTime, bool clockValid);
