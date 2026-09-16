#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#

/* ------------------------------------------------------------------ --- 
| Method: sendTelegramMessage
|
| Return: It returns the HTTP status code from Telegram, or -1 if setup fails.
| 
|  Args:
|   - botToken: The token of the Telegram bot.
|   - jsonPayload: The JSON payload containing the message to be sent.
------------------------------------------------------------------------*/
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


/* --------------------------------------------------------------------- 
| Method: createJsonPayload
|
| Return: It returns the JSON payload containing the message to be sent.
| 
|  Args:
|   - username: The username of the sender.
|   - message: The message to be sent. 
------------------------------------------------------------------------*/
String createJsonPayload(const String username, const String message) {
  return String("{\"alias\":\"") + username + "\",\"message\":\"" + message + "\"}";
}