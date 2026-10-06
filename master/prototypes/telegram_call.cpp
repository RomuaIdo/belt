#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#

// Sends Telegram message; returns HTTP status code or -1 on failure.
int sendTelegramMessage(const String botToken, const String &jsonPayload) {
  if (botToken == nullptr || botToken[0] == '\0') {
    return -1;
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  String url = String("https://api.telegram.org/bot") + botToken + "/sendMessage";

  if (!http.begin(client, url)) {
    return -1;
  }

  http.addHeader("Content-Type", "application/json");
  int statusCode = http.POST(jsonPayload);
  http.end();

  return statusCode;
}


// Formats JSON payload containing alias and message.
String createJsonPayload(const String username, const String message) {
  return String("{\"alias\":\"") + username + "\",\"message\":\"" + message + "\"}";
}