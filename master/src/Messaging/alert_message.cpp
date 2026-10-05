#include "Messaging/alert_message.h"

const char* const DEFAULT_ALERT_MESSAGE =
    "ALERT: {nome} may have fallen. Detected at {hora} on {data}.";

String formatAlertMessage(const String& messageTemplate, const String& displayName,
                          time_t eventTime, bool clockValid) {
    String out = messageTemplate.isEmpty() ? String(DEFAULT_ALERT_MESSAGE) : messageTemplate;

    String hour = "--:--";
    String date = "--/--/----";
    if (clockValid) {
        struct tm local;
        char buf[16];
        if (localtime_r(&eventTime, &local) != nullptr) {
            if (strftime(buf, sizeof(buf), "%H:%M", &local) > 0) hour = buf;
            if (strftime(buf, sizeof(buf), "%d/%m/%Y", &local) > 0) date = buf;
        }
    }

    out.replace("{nome}", displayName);
    out.replace("{apelido}", displayName);
    out.replace("{hora}", hour);
    out.replace("{data}", date);
    return out;
}
