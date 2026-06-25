#pragma once
#include <Arduino.h>

#include <vector>

namespace api_client {
struct RegisterResult {
  bool ok;
  String deviceToken;
  String pairCode;  // empty when already paired
  String websocketUrl;
  bool paired;
  String error;
};

struct PersonaInfo {
  int id;
  String name;
};

struct MeResult {
  bool ok;
  int statusCode;
  bool paired;
  int personaId;  // -1 if none
  String personaName;
  String websocketUrl;
  std::vector<PersonaInfo> personas;
  String error;
};

void configure(const String &host, uint16_t port, bool tls);
void setDeviceToken(const String &token);

RegisterResult registerDevice(const String &deviceId, const String &name);
MeResult getMe();
bool setPersona(int personaId, String *errorOut = nullptr);
}  // namespace api_client
