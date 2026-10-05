
#pragma once

#include <Arduino.h>
#include <time.h>

// Same default message as the frontend (DEFAULT_MSG in frontend/app.js).
extern const char* const DEFAULT_ALERT_MESSAGE;

// Replaces the alert template's placeholders:
//   {nome} and {apelido} -> displayName
//   {hora} -> HH:MM, {data} -> DD/MM/YYYY (process-local timezone)
// An empty template falls back to DEFAULT_ALERT_MESSAGE. Without a valid
// clock, time/date render as "--:--" / "--/--/----". Unknown placeholders
// are left untouched.
String formatAlertMessage(const String& messageTemplate, const String& displayName,
                          time_t eventTime, bool clockValid);
