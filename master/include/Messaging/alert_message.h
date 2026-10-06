
#pragma once

#include <Arduino.h>
#include <time.h>

// Default alert template (matches frontend).
extern const char* const DEFAULT_ALERT_MESSAGE;

// Populates template placeholders ({nome}, {apelido}, {hora}, {data}).
String formatAlertMessage(const String& messageTemplate, const String& displayName,
                          time_t eventTime, bool clockValid);
