#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>

#include "config.h"
#include "display.h"
#include "provision.h"

namespace {
WebServer s_server(80);
DNSServer s_dns;
bool s_done = false;

String apSsidStr() {
  uint64_t mac = ESP.getEfuseMac();
  char buf[32];
  snprintf(buf, sizeof(buf), "%s%04X", PROV_AP_PREFIX,
           (uint16_t)(mac & 0xFFFF));
  return String(buf);
}

const char FORM_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>SoulTalk Toy Setup</title>
<style>
body{font-family:sans-serif;max-width:480px;margin:1em auto;padding:0 1em;}
label{display:block;margin-top:.6em;font-size:.9em;color:#444}
input,select{width:100%;padding:.5em;font-size:1em;box-sizing:border-box}
button{margin-top:1em;padding:.7em 1.2em;font-size:1em;width:100%}
.row{display:flex;gap:.5em}.row > *{flex:1}
.note{color:#666;font-size:.85em;margin-top:1em}
</style></head>
<body>
<h2>SoulTalk Toy Setup</h2>
<form method="POST" action="/save">
  <label>WiFi SSID</label><input name="ssid" required>
  <label>WiFi Password</label><input name="pass" type="password">
  <label>SoulTalk Server Host</label><input name="host" value="%HOST%" required>
  <div class="row">
    <div><label>Port</label><input name="port" value="%PORT%" required></div>
    <div><label>TLS (https/wss)</label>
      <select name="tls"><option value="0" %TLS0%>No</option><option value="1" %TLS1%>Yes</option></select>
    </div>
  </div>
  <button type="submit">Save & Reboot</button>
</form>
<p class="note">After reboot, the device will register with the server and show
a 6-character pairing code on its screen. Open
<code>http(s)://&lt;host&gt;:&lt;port&gt;/api/devices/pair</code> while
logged in to SoulTalk to bind the device and pick a persona.</p>
<p class="note">Device ID: %DID%</p>
</body></html>
)HTML";

void handleRoot() {
  DeviceSettings cur;
  settings::load(cur);
  String html = FORM_HTML;
  html.replace("%HOST%", cur.host.length() ? cur.host : DEFAULT_HOST);
  html.replace("%PORT%", String(cur.port ? cur.port : DEFAULT_PORT));
  html.replace("%TLS0%", cur.tls ? "" : "selected");
  html.replace("%TLS1%", cur.tls ? "selected" : "");
  html.replace("%DID%", settings::deviceId());
  s_server.send(200, "text/html; charset=utf-8", html);
}

void handleSave() {
  DeviceSettings s;
  settings::load(s);
  s.wifiSsid = s_server.arg("ssid");
  s.wifiPass = s_server.arg("pass");
  s.host = s_server.arg("host");
  s.port = (uint16_t)s_server.arg("port").toInt();
  s.tls = s_server.arg("tls") == "1";
  // Reset device token + persona on reconfig: server identity may have changed
  s.deviceToken = "";
  s.personaId = -1;

  if (s.wifiSsid.length() == 0 || s.host.length() == 0) {
    s_server.send(400, "text/plain", "Missing required fields");
    return;
  }
  settings::save(s);
  s_server.send(200, "text/html; charset=utf-8",
                "<h3>Saved. Rebooting...</h3>");
  delay(800);
  s_done = true;
}

void handleNotFound() {
  s_server.sendHeader("Location", "/", true);
  s_server.send(302, "text/plain", "");
}
}  // namespace

namespace provision {

String apSsid() {
  return apSsidStr();
}

void runPortal() {
  String ssid = apSsidStr();
  Serial.printf("[provision] Starting SoftAP: %s\n", ssid.c_str());

  display_ui::setState(display_ui::State::Provision);
  display_ui::setLine(1, ssid);
  display_ui::render();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid.c_str(),
              strlen(PROV_AP_PASSWORD) ? PROV_AP_PASSWORD : nullptr);

  IPAddress ip = WiFi.softAPIP();
  Serial.printf("[provision] AP IP: %s\n", ip.toString().c_str());

  s_dns.start(53, "*", ip);
  s_server.on("/", handleRoot);
  s_server.on("/save", HTTP_POST, handleSave);
  s_server.onNotFound(handleNotFound);
  s_server.begin();

  s_done = false;
  while (!s_done) {
    s_dns.processNextRequest();
    s_server.handleClient();
    delay(2);
  }
  s_server.stop();
  s_dns.stop();
  WiFi.softAPdisconnect(true);
  delay(200);
  ESP.restart();
}

}  // namespace provision
