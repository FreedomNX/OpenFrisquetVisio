#include "Portal.h"
#include "Frisquet/HistoriqueVeille.h"
#include "Frisquet/StockageHistorique.h"
#include "ProgrammationPortail.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <stdarg.h>
#include <cstring>
#include <esp_system.h>
#include <esp_timer.h>
#include "Frisquet/NetworkID.h" 
#include "Version.h"
#include "generated/WebAssets.h"

// Déclaration du logger global défini dans Logs.cpp
extern Logs logs;

// Le tampon d'affichage ne peut pas contenir plus de lignes que le journal.
static constexpr size_t kMaxQueryLines = Logs::kMaxLines < 120 ? Logs::kMaxLines : 120;
static Logs::Line s_logLines[kMaxQueryLines];
static volatile bool s_logsBusy = false;
static uint8_t s_memoryMessageId = 0x10;

// Helpers NetworkID <-> String "AA:BB:CC:DD"
static String networkIdToStr(const NetworkID& id) {
  char buf[12];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X",
           id.bytes[0], id.bytes[1], id.bytes[2], id.bytes[3]);
  return String(buf);
}

static bool parseUint16Hex(const String& input, uint16_t& out) {
  String s = input;
  s.trim();
  if (s.length() == 0) {
    return false;
  }

  char* end = nullptr;
  unsigned long value = strtoul(s.c_str(), &end, 16);
  if (!end || *end != '\0' || value > 0xFFFFUL) {
    return false;
  }
  out = static_cast<uint16_t>(value);
  return true;
}

// Entier décimal dans [minimum, maximum]. Recevoir la String par référence
// garde `end` valide pendant le contrôle de fin, même pour un argument
// temporaire comme `_srv.arg(...)`, détruit seulement après l'appel.
static bool parseDecimalArg(const String& input, unsigned long minimum, unsigned long maximum,
                            uint16_t& out) {
  char* end = nullptr;
  const unsigned long value = strtoul(input.c_str(), &end, 10);
  if (input.isEmpty() || !end || *end != '\0' || value < minimum || value > maximum) {
    return false;
  }
  out = static_cast<uint16_t>(value);
  return true;
}

static bool parseNetworkIdFromString(const String& s, NetworkID& out) {
  String hex = s;
  hex.trim();
  hex.replace(":", "");
  hex.replace("-", "");
  hex.toUpperCase();

  if (hex.length() != 8) return false;

  uint8_t b[4];
  for (int i = 0; i < 4; ++i) {
    String sub = hex.substring(i * 2, i * 2 + 2);
    char* end = nullptr;
    long v = strtol(sub.c_str(), &end, 16);
    if (!end || *end != '\0' || v < 0 || v > 255) {
      return false;
    }
    b[i] = (uint8_t)v;
  }

  out = NetworkID(b[0], b[1], b[2], b[3]);
  return true;
}

// Validate an IPv4 address string: four octets 0..255 separated by '.'
static bool isValidIPv4(const String& s) {
  String str = s;
  str.trim();
  if (str.length() == 0) return false;
  int parts = 0;
  int len = str.length();
  int i = 0;
  while (i < len) {
    int j = i;
    while (j < len && str[j] != '.') j++;
    String part = str.substring(i, j);
    if (part.length() == 0) return false;
    if (part.length() > 3) return false;
    for (int k = 0; k < part.length(); ++k) {
      if (part[k] < '0' || part[k] > '9') return false;
    }
    long val = part.toInt();
    if (val < 0 || val > 255) return false;
    parts++;
    i = j + 1;
  }
  return parts == 4;
}

// Limites du core Wi-Fi et de MQTT, vérifiées avant l'enregistrement.
static bool fitsBytes(const String& s, size_t maximum) {
  return s.length() <= maximum && strlen(s.c_str()) == s.length();
}

static bool isValidHostname(const String& s) {
  // Le core copie le hostname dans 32 octets, NUL compris (RFC 1123 : lettres, chiffres, tirets).
  if (s.isEmpty() || !fitsBytes(s, 31) || s[0] == '-' || s[s.length() - 1] == '-') return false;
  for (size_t i = 0; i < s.length(); i++) {
    const char c = s[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-')) return false;
  }
  return true;
}

static bool isValidWifiPassword(const String& s) {
  // WPA : phrase de 8 à 63 caractères, ou clé de 64 chiffres hexadécimaux.
  if (!fitsBytes(s, 64)) return false;
  if (s.length() < 64) return s.length() >= 8;
  for (size_t i = 0; i < s.length(); i++) {
    const char c = s[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
  }
  return true;
}

static bool isValidMqttBaseTopic(const String& s) {
  // Les jokers + et # sont interdits dans un topic de publication. Vide : topics à la racine.
  for (size_t i = 0; i < s.length(); i++) {
    if (s[i] == '+' || s[i] == '#' || (uint8_t)s[i] < 0x20) return false;
  }
  return true;
}

static const char* resetReasonToString(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_UNKNOWN: return "UNKNOWN";
    case ESP_RST_POWERON: return "POWERON";
    case ESP_RST_EXT: return "EXT";
    case ESP_RST_SW: return "SW";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SDIO: return "SDIO";
    default: return "UNKNOWN";
  }
}

static String jsonEscape(const String& s) {
    String out;
    out.reserve(s.length() + 10);

    for (uint16_t i = 0; i < s.length(); i++) {
        char c = s[i];

        switch (c) {
            case '\"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if ((uint8_t)c < 0x20) {
                    // Caractère de contrôle → tout en \u00XX
                    char buf[7];
                    sprintf(buf, "\\u%04X", (uint8_t)c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

static String jsonFloat(float value, unsigned int decimals = 1) {
  return isfinite(value) ? String(value, decimals) : String("null");
}

static String jsonIntOrNull(int32_t value) {
  return value >= 0 ? String(value) : String("null");
}

// Helper pour parser un bool depuis une string
static bool parseBoolArg(const String& s, bool defaultVal) {
  String v = s;
  v.trim();
  v.toLowerCase();
  if (v == "1" || v == "true" || v == "on"  || v == "yes" || v == "oui")  return true;
  if (v == "0" || v == "false"|| v == "off" || v == "no"  || v == "non") return false;
  return defaultVal;
}

Portal::Portal(FrisquetManager& frisquetManager, uint16_t port)
: _srv(port), _frisquetManager(frisquetManager) {}

String Portal::sessionCookie() {
  const String cookies = _srv.header("Cookie");
  int start = 0;
  while (start < static_cast<int>(cookies.length())) {
    int end = cookies.indexOf(';', start);
    if (end < 0) end = cookies.length();
    String cookie = cookies.substring(start, end);
    cookie.trim();
    if (cookie.startsWith("ofv_session=")) return cookie.substring(12);
    start = end + 1;
  }
  return String();
}

bool Portal::isAuthenticated() {
  const String token = sessionCookie();
  if (token.length() != 64) return false;
  const uint64_t now = static_cast<uint64_t>(esp_timer_get_time()) / 1000;
  for (Session& session : _sessions) {
    if (now - session.started >= SESSION_DURATION_MS) session.token[0] = '\0';
    if (session.token[0] && token == session.token) return true;
  }
  return false;
}

bool Portal::requireAuthentication() {
  _srv.sendHeader("Cache-Control", "no-store");
  if (isAuthenticated()) return true;
  if (_srv.uri().startsWith("/api/")) {
    _srv.send(401, "application/json; charset=utf-8",
              "{\"ok\":false,\"err\":\"Authentification requise\"}");
  } else {
    _srv.sendHeader("Location", "/login");
    _srv.send(303, "text/plain; charset=utf-8", "Connexion requise");
  }
  return false;
}

void Portal::createSession() {
  const uint64_t now = static_cast<uint64_t>(esp_timer_get_time()) / 1000;
  Session* selected = &_sessions[0];
  for (Session& session : _sessions) {
    if (!session.token[0] || now - session.started >= SESSION_DURATION_MS) {
      selected = &session;
      break;
    }
    if (now - session.started > now - selected->started) selected = &session;
  }
  uint8_t random[32];
  esp_fill_random(random, sizeof(random));
  for (size_t i = 0; i < sizeof(random); ++i) {
    snprintf(selected->token + i * 2, 3, "%02x", random[i]);
  }
  selected->started = now;
  _srv.sendHeader("Set-Cookie", String("ofv_session=") + selected->token
                 + "; Path=/; HttpOnly; SameSite=Strict; Max-Age=43200");
}

void Portal::clearSessions() {
  for (Session& session : _sessions) session.token[0] = '\0';
  _srv.sendHeader("Set-Cookie",
                 "ofv_session=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
}

void Portal::sendLoginPage(int status, const char* message) {
  String page = FPSTR(WebAssets::LOGIN_HTML);
  page.replace("{{LOGIN_ERROR}}", message);
  _srv.sendHeader("Cache-Control", "no-store");
  _srv.send(status, "text/html; charset=utf-8", page);
}

void Portal::handleLoginPage() {
  if (isAuthenticated()) {
    _srv.sendHeader("Cache-Control", "no-store");
    _srv.sendHeader("Location", "/");
    _srv.send(303);
    return;
  }
  sendLoginPage(200, "");
}

void Portal::handleLogin() {
  const uint32_t now = millis();
  if (_loginBlocked && now - _loginBlockedAt < 30000UL) {
    _srv.sendHeader("Retry-After", "30");
    sendLoginPage(429, "Trop de tentatives. Réessayez dans 30 secondes.");
    return;
  }
  if (_loginBlocked) { _loginBlocked = false; _failedLogins = 0; }
  const String password = _srv.arg("password");
  if (password.isEmpty() || password.length() > 64
      || password != _frisquetManager.config().portalPassword()) {
    if (++_failedLogins >= 5) {
      _loginBlocked = true;
      _loginBlockedAt = now;
      _srv.sendHeader("Retry-After", "30");
      sendLoginPage(429, "Trop de tentatives. Réessayez dans 30 secondes.");
    } else {
      sendLoginPage(401, "Mot de passe incorrect.");
    }
    return;
  }
  _failedLogins = 0;
  createSession();
  _srv.sendHeader("Cache-Control", "no-store");
  _srv.sendHeader("Location", "/");
  _srv.send(303);
}

void Portal::handleLogout() {
  const String token = sessionCookie();
  for (Session& session : _sessions) {
    if (token == session.token) session.token[0] = '\0';
  }
  _srv.sendHeader("Cache-Control", "no-store");
  _srv.sendHeader("Set-Cookie",
                 "ofv_session=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
  _srv.sendHeader("Location", "/login");
  _srv.send(303);
}

void Portal::onAuthenticated(const char* uri, HTTPMethod method, void (Portal::*handler)(),
                             void (Portal::*uploadHandler)()) {
  auto authenticatedHandler = [this, handler, uploadHandler]() {
    if (requireAuthentication()) (this->*handler)();
    else if (uploadHandler) cancelHttpUpdate();
  };
  if (uploadHandler) {
    _srv.on(uri, method, authenticatedHandler, [this, uploadHandler]() {
      // L'upload est traité AVANT le handler final : vérifier chaque callback
      // avant toute écriture flash. Le handler final refuse la requête non authentifiée.
      if (isAuthenticated()) (this->*uploadHandler)();
      else cancelHttpUpdate();
    });
  } else {
    _srv.on(uri, method, authenticatedHandler);
  }
}

void Portal::begin(bool startApFallbackIfNoWifi) {
  if (startApFallbackIfNoWifi && !WiFi.isConnected()) {
    startAp();
  }

  // Le serveur web ignore les en-têtes de requête qui ne sont pas listés ici.
  // Cookie : identifiant de session. If-None-Match : ETag (empreinte de la
  // ressource) de la copie que le navigateur a en cache ; si elle est encore
  // à jour, le serveur répond 304 Not Modified sans corps au lieu de renvoyer
  // la page.
  // @see https://developer.mozilla.org/fr/docs/Web/HTTP/Reference/Headers/ETag
  // @see https://developer.mozilla.org/fr/docs/Web/HTTP/Reference/Status/304
  const char* headerKeys[] = {"Cookie", "If-None-Match"};
  _srv.collectHeaders(headerKeys, sizeof(headerKeys) / sizeof(headerKeys[0]));
  // Ressources visuelles publiques : nécessaires avant l’authentification.
  _srv.on("/theme.css", HTTP_GET, [this] {
    sendStaticAsset(WebAssets::THEME_CSS);
  });
  _srv.on("/commande-zone.js", HTTP_GET, [this] { sendStaticAsset(WebAssets::COMMANDE_ZONE_JS); });
  _srv.on("/theme.js", HTTP_GET, [this] {
    sendStaticAsset(WebAssets::THEME_JS);
  });
  // Renvoie les pages vers /login quand l’API répond 401. Sans donnée privée.
  _srv.on("/session.js", HTTP_GET, [this] { sendStaticAsset(WebAssets::SESSION_JS); });
  _srv.on("/login", HTTP_GET, [this] { handleLoginPage(); });
  _srv.on("/login", HTTP_POST, [this] { handleLogin(); });
  onAuthenticated("/logout", HTTP_POST, &Portal::handleLogout);

  onAuthenticated("/", HTTP_GET, &Portal::handleIndex);
  onAuthenticated("/settings", HTTP_GET, &Portal::handleSettingsPage);
  onAuthenticated("/logo.png", HTTP_GET, &Portal::handleLogo);
  onAuthenticated("/api/ping", HTTP_GET, &Portal::handlePing);
  onAuthenticated("/api/dashboard", HTTP_GET, &Portal::handleDashboard);
  onAuthenticated("/api/consommation/quotidienne", HTTP_GET, &Portal::handleHistoriqueVeille);
  onAuthenticated("/programming", HTTP_GET, &Portal::handleProgrammationPage);
  onAuthenticated("/api/programming", HTTP_GET, &Portal::handleGetProgrammation);
  onAuthenticated("/api/commande/reessayer", HTTP_POST, &Portal::handleReessaiZone);
  onAuthenticated("/api/programming", HTTP_POST, &Portal::handlePostProgrammation);
  onAuthenticated("/api/config", HTTP_GET, &Portal::handleGetConfig);
  onAuthenticated("/api/config", HTTP_POST, &Portal::handlePostConfig);
  onAuthenticated("/api/reboot", HTTP_POST, &Portal::handleReboot);
  onAuthenticated("/api/logs", HTTP_GET, &Portal::handleGetLogs);
  onAuthenticated("/api/logs/clear", HTTP_POST, &Portal::handleClearLogs);
  onAuthenticated("/logs", HTTP_GET, &Portal::handleLogsPage);
  onAuthenticated("/api/status", HTTP_GET, &Portal::handleStatus);
  onAuthenticated("/logs-radio", HTTP_GET, &Portal::handleRadioLogsPage);
  onAuthenticated("/api/memory", HTTP_GET, &Portal::handleMemoryRead);
  onAuthenticated("/api/memory/scan", HTTP_GET, &Portal::handleMemoryScan);
  onAuthenticated("/memory", HTTP_GET, &Portal::handleMemoryPage);
  onAuthenticated("/api/radio/send", HTTP_POST, &Portal::handleSendRadio);
  onAuthenticated("/api/update", HTTP_POST, &Portal::handleUpdate, &Portal::handleUpdateUpload);
  onAuthenticated("/api/connect/pair", HTTP_POST, &Portal::handlePairConnect);
  onAuthenticated("/api/sonde-ext/pair", HTTP_POST, &Portal::handlePairSondeExt);
  onAuthenticated("/api/satellite/z1/pair", HTTP_POST, &Portal::handlePairSatelliteZ1);
  onAuthenticated("/api/satellite/z2/pair", HTTP_POST, &Portal::handlePairSatelliteZ2);
  onAuthenticated("/api/satellite/z3/pair", HTTP_POST, &Portal::handlePairSatelliteZ3);
  onAuthenticated("/api/network-id/recup", HTTP_POST, &Portal::handleRecupNetworkId);


  _srv.onNotFound([this](){
    if (!requireAuthentication()) return;
    _srv.send(404, "text/plain; charset=utf-8", "404 Non trouvé");
  });

  _srv.begin();
  info("[PORTAIL] Serveur HTTP démarré");
}

void Portal::loop() {
  _srv.handleClient();
}

void Portal::handleClearLogs() {
  logs.clear();
  _srv.send(200, "text/plain; charset=utf-8", "OK");
}

void Portal::cancelHttpUpdate() {
  if (!_httpUpdateActive) return;
  Update.abort();
  _httpUpdateActive = false;
  if (_lastUpdateError.length() == 0) _lastUpdateError = "Upload interrompu";
  _frisquetManager.radio().suspend(FrisquetRadio::Pause::HttpOta, false);
}

void Portal::handleUpdate() {
  if (!_httpUpdateActive || _lastUpdateError.length() > 0 || Update.hasError()) {
    cancelHttpUpdate();
    String err = _lastUpdateError.length() > 0 ? _lastUpdateError : "Erreur pendant la mise à jour";
    error("[PORTAIL] OTA HTTP: %s", err.c_str());
    _srv.send(500, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"Mise à jour échouée\"}");
    return;
  }

  // La requête multipart est entièrement lue : aucun autre fichier ne peut plus
  // arriver. Update.end() valide l'image et sélectionne la partition de démarrage.
  if (!Update.end(true)) {
    _lastUpdateError = "Update.end a échoué";
    Update.printError(Serial);
    cancelHttpUpdate();
    error("[PORTAIL] OTA HTTP: %s", _lastUpdateError.c_str());
    _srv.send(500, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"Mise à jour échouée\"}");
    return;
  }

  const bool rebootScheduled = scheduleReboot(1500);
  _httpUpdateActive = false;
  _frisquetManager.radio().suspend(FrisquetRadio::Pause::HttpOta, false);
  if (!rebootScheduled) {
    _srv.send(500, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"Redémarrage indisponible\"}");
    return;
  }
  _srv.send(200, "application/json; charset=utf-8", "{\"ok\":true}");
  info("[PORTAIL] OTA HTTP: OK, redémarrage...");
}

void Portal::handleUpdateUpload() {
  HTTPUpload& upload = _srv.upload();
  if (upload.status == UPLOAD_FILE_START) {
    if (_httpUpdateActive) {
      // Second fichier dans la même requête : abandonner sans rien activer.
      if (_lastUpdateError.length() == 0) _lastUpdateError = "Un seul fichier par mise à jour";
      Update.abort();
      return;
    }
    _httpUpdateActive = true;
    _frisquetManager.radio().suspend(FrisquetRadio::Pause::HttpOta, true);
    _lastUpdateError = "";
    info("[PORTAIL] OTA HTTP: début upload (%s, %u bytes)", upload.filename.c_str(), upload.totalSize);
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
      _lastUpdateError = "Update.begin a échoué";
      Update.printError(Serial);
    }
  } else if (!_httpUpdateActive) {
    // Une session expirée ne peut pas reprendre un upload déjà annulé.
    return;
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      _lastUpdateError = "Update.write a échoué";
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    // Fin du fichier seulement : la partition est activée par handleUpdate().
    info("[PORTAIL] OTA HTTP: fichier reçu (%u bytes)", upload.totalSize);
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    cancelHttpUpdate();
  }
}

// -------------------- API --------------------

void Portal::handleIndex() {
  sendStaticAsset(WebAssets::INDEX_HTML);
}

void Portal::handleSettingsPage() {
  sendStaticAsset(WebAssets::SETTINGS_HTML);
}

void Portal::handleProgrammationPage() {
  sendStaticAsset(WebAssets::PROGRAMMING_HTML);
}

static String programmationHex(const Zone::Programmation& programmation) {
  static_assert(sizeof(Zone::Programmation) == 42, "Programmation : 42 octets");
  const byte* bytes = reinterpret_cast<const byte*>(&programmation);
  const char* digits = "0123456789ABCDEF";
  String hex;
  hex.reserve(84);
  for(size_t i = 0; i < sizeof(programmation); ++i) {
    hex += digits[bytes[i] >> 4];
    hex += digits[bytes[i] & 0x0F];
  }
  return hex;
}

static String programmationRevision(Zone& zone) {
  return String(zone.programmeEditeConnu() ? "1" : "0") + programmationHex(zone.programmeEdite());
}

void Portal::handleGetProgrammation() {
  auto& cfg = _frisquetManager.config();
  auto& connect = _frisquetManager.connect();
  const bool enabled[] = {cfg.useZone1(), cfg.useZone2(), cfg.useZone3()};
  Zone* zones[] = {&connect.getZone1(), &connect.getZone2(), &connect.getZone3()};
  String json = "{\"ok\":true,\"zones\":[";
  json.reserve(1600);
  for(uint8_t i = 0; i < 3; ++i) {
    Zone& zone = *zones[i];
    String reason = programmationReadOnlyReason(cfg, connect, zone, enabled[i]);
    if(i) json += ",";
    json += "{\"id\":" + String(i + 1);
    json += ",\"enabled\":" + String(enabled[i] ? "true" : "false");
    json += ",\"editable\":" + String(reason.isEmpty() ? "true" : "false");
    json += ",\"reason\":\"" + jsonEscape(reason) + "\"";
    json += ",\"known\":" + String(zone.programmeEditeConnu() ? "true" : "false");
    json += ",\"failed\":" + String(zone.programmationNonConfirmee() ? "true" : "false");
    json += ",\"pending\":" + String(zone.programmationEnvoiEnAttente() ? "true" : "false");
    json += ",\"mode\":\"" + jsonEscape(zone.getNomMode()) + "\"";
    json += ",\"program\":\"" + programmationHex(zone.programmeEdite()) + "\"";
    json += ",\"revision\":\"" + programmationRevision(zone) + "\"}";
  }
  json += "]}";
  _srv.sendHeader("Cache-Control", "no-store");
  _srv.send(200, "application/json; charset=utf-8", json);
}

void Portal::handleReessaiZone() {
  const String numero = _srv.arg("zone");
  auto& connect = _frisquetManager.connect();
  Zone* zones[] = {&connect.getZone1(), &connect.getZone2(), &connect.getZone3()};
  if(numero.length() != 1 || numero[0] < '1' || numero[0] > '3') {
    _srv.send(400, "text/plain", "Zone invalide.");
    return;
  }
  if(!zones[numero[0] - '1']->reessayer()) {
    _srv.send(409, "text/plain", "Aucune commande à relancer.");
    return;
  }
  _srv.send(200, "text/plain", "Nouvelle tentative en cours.");
}

void Portal::handlePostProgrammation() {
  String numero = _srv.arg("zone");
  if(numero != "1" && numero != "2" && numero != "3") {
    _srv.send(400, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"Zone invalide.\"}");
    return;
  }
  auto& cfg = _frisquetManager.config();
  auto& connect = _frisquetManager.connect();
  const bool enabled[] = {cfg.useZone1(), cfg.useZone2(), cfg.useZone3()};
  Zone* zones[] = {&connect.getZone1(), &connect.getZone2(), &connect.getZone3()};
  const uint8_t index = numero.toInt() - 1;
  Zone& zone = *zones[index];
  String reason = programmationReadOnlyReason(cfg, connect, zone, enabled[index]);
  if(!reason.isEmpty()) {
    _srv.send(409, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"" + jsonEscape(reason) + "\"}");
    return;
  }
  Zone::Programmation programmation;
  size_t length = 0;
  String hex = _srv.arg("program");
  if(hex.length() != 84 || !hexStringToBufferRaw(hex, reinterpret_cast<byte*>(&programmation), sizeof(programmation), length) || length != sizeof(programmation)) {
    _srv.send(400, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"Programmation invalide : sept journées complètes sont nécessaires.\"}");
    return;
  }
  if(_srv.arg("revision") != programmationRevision(zone)) {
    _srv.send(409, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"La programmation a changé depuis son chargement. Rechargez-la avant d’enregistrer.\"}");
    return;
  }
  zone.modifierProgrammation(programmation);
  info("[PORTAIL] Programmation de la zone %d enregistrée (%s).", index + 1,
       zone.programmationEnvoiEnAttente() ? "envoi Connect en attente" : "programme local");
  _srv.send(200, "application/json; charset=utf-8", "{\"ok\":true,\"revision\":\"" + programmationRevision(zone)
       + "\",\"pending\":" + String(zone.programmationEnvoiEnAttente() ? "true" : "false") + "}");
}

void Portal::handleLogo() {
  sendStaticAsset(WebAssets::LOGO_PNG);
}

// Le navigateur revalide la ressource à chaque usage : tant que le fichier ne
// change pas, même après une mise à jour du firmware, l’ETag réduit cet échange
// à une réponse 304 sans corps.
void Portal::sendStaticAsset(const WebAssets::StaticAsset& asset) {
  _srv.sendHeader("Cache-Control", "no-cache");
  _srv.sendHeader("ETag", asset.etag);
  // Le firmware ne garde que la version compressée, et tous les navigateurs
  // acceptent gzip : Accept-Encoding n’est donc pas consulté.
  // @see https://developer.mozilla.org/fr/docs/Web/HTTP/Reference/Headers/Content-Encoding
  _srv.sendHeader("Content-Encoding", "gzip");
  if (_srv.header("If-None-Match") == asset.etag) {
    // À la réception d’une 304, le navigateur garde sa copie et en remplace
    // les en-têtes par ceux de la 304 : type et encodage doivent donc être
    // exacts. Content-Length doit être celui de la réponse 200, soit gzipSize ;
    // sans setContentLength, la bibliothèque enverrait 0.
    // @see https://developer.mozilla.org/fr/docs/Web/HTTP/Reference/Status/304
    _srv.setContentLength(asset.gzipSize);
    _srv.send(304, asset.contentType);
    return;
  }
  _srv.send_P(200, asset.contentType, reinterpret_cast<PGM_P>(asset.gzipData), asset.gzipSize);
}

void Portal::handlePing() {
  _srv.send(200, "application/json", "{\"ok\":true}");
}

static String jsonConsommationsMensuelles(const Connect::ConsommationsMensuelles& donnees, bool anneePrecedente = false) {
  if (!donnees.disponibles) return "null";
  String json;
  json.reserve(256);
  json = "{\"year\":";
  if (donnees.dateReception) {
    const time_t date = donnees.dateReception;
    char texte[20];
    // L'horloge chaudière est locale : ne pas la présenter comme une date UTC.
    snprintf(texte, sizeof(texte), "%02d/%02d/%04d %02d:%02d:%02d",
             day(date), month(date), year(date), hour(date), minute(date), second(date));
    json += String(year(date) - (anneePrecedente ? 1 : 0)) + ",\"month\":" + String(anneePrecedente ? 12 : month(date)) + ",\"receivedAt\":\"" + String(texte) + "\"";
  } else {
    json += "null,\"month\":null,\"receivedAt\":null";
  }
  json += ",\"complete\":" + String(anneePrecedente ? "true" : "false");
  json += ",\"heating\":[";
  for (uint8_t mois = 0; mois < 12; ++mois) {
    if (mois) json += ",";
    json += String(donnees.chauffage[mois]);
  }
  json += "],\"hotWater\":[";
  for (uint8_t mois = 0; mois < 12; ++mois) {
    if (mois) json += ",";
    json += String(donnees.ecs[mois]);
  }
  return json + "]}";
}

// Mois civil ISO dans l’API, numéro consécutif dans l’archive.
static String moisIso(int numero) {
  if (!numero) return "null";
  char texte[24];
  snprintf(texte, sizeof(texte), "\"%04d-%02d\"", numero / 12, numero % 12 + 1);
  return String(texte);
}

void Portal::handleHistoriqueVeille() {
  int selection = 0;
  if (_srv.hasArg("mois")) {
    const String iso = _srv.arg("mois");
    unsigned annee, mois;
    char fin;
    if (iso.length() != 7 || iso[4] != '-' || strspn(iso.c_str(), "0123456789") != 4 ||
        strspn(iso.c_str() + 5, "0123456789") != 2 ||
        sscanf(iso.c_str(), "%u-%u%c", &annee, &mois, &fin) != 2 ||
        annee < 1970 || annee > 2099 || mois < 1 || mois > 12) {
      _srv.send(400, "text/plain; charset=utf-8", "Mois invalide (AAAA-MM).");
      return;
    }
    selection = annee * 12 + mois - 1;
  }
  auto& config = _frisquetManager.config();
  HistoriqueVeille archive;
  if (!archive.lire(config, selection)) {
    const char* erreur = erreurStockageHistorique();
    _srv.send(503, "text/plain; charset=utf-8", erreur ? erreur : "Lecture des fichiers de l’historique impossible.");
    return;
  }
  if (selection && !archive.mois) {
    _srv.send(404, "text/plain; charset=utf-8", "Mois hors de la période conservée.");
    return;
  }
  const uint32_t fin = archive.dernierJour();
  String json;
  json.reserve(640);
  const String version = networkIdToStr(config.getNetworkID()) + "-" + String(config.boilerId()) + "-" + String(archive.revision);
  json = "{\"version\":\"" + version + "\",\"firstMonth\":" + moisIso(archive.premierMois) + ",\"lastMonth\":" + moisIso(archive.dernierMois)
       + ",\"month\":" + moisIso(archive.mois) + ",\"lastDay\":" + String(fin) + ",\"days\":[";
  for (unsigned j = archive.nbJours; j > 0; --j) {
    if (j < archive.nbJours) json += ',';
    const auto& valeurs = archive.jours[j - 1];
    if (valeurs.disponible()) json += "[" + jsonIntOrNull(valeurs.chauffage) + "," + jsonIntOrNull(valeurs.ecs) + "]";
    else json += "null";
  }
  _srv.send(200, "application/json", json + "]}");
}

void Portal::handleDashboard() {
  auto& cfg = _frisquetManager.config();
  auto& chaudiere = _frisquetManager.chaudiere();
  auto etat = chaudiere.getEtatChaudiere();

  String json;
  json.reserve(3100);
  json = "{\"ok\":true,";
  json += "\"developerMode\":" + String(cfg.developerMode() ? "true" : "false") + ",";
  json += "\"defaultPortalPassword\":" + String(cfg.portalPassword() == "admin" ? "true" : "false") + ",";
  json += "\"version\":\"" + jsonEscape(String(OPENFRISQUETVISIO_VERSION)) + "\",";
  json += "\"uptimeSec\":" + String(millis() / 1000) + ",";
  json += "\"connectivity\":{";
  json += "\"wifiConnected\":" + String(WiFi.isConnected() ? "true" : "false") + ",";
  json += "\"mqttConnected\":" + String(_frisquetManager.mqtt().connected() ? "true" : "false") + ",";
  json += "\"ssid\":\"" + jsonEscape(WiFi.isConnected() ? WiFi.SSID() : String()) + "\",";
  json += "\"ip\":\"" + jsonEscape(WiFi.isConnected() ? WiFi.localIP().toString() : String()) + "\",";
  json += "\"rssi\":" + String(WiFi.isConnected() ? WiFi.RSSI() : 0);
  json += "},";

  const bool chaudiereEnabled = cfg.useConnect()
      || cfg.useSatelliteZ1() || cfg.useSatelliteZ2() || cfg.useSatelliteZ3();
  json += "\"boiler\":{";
  json += "\"enabled\":" + String(chaudiereEnabled ? "true" : "false") + ",";
  json += "\"state\":\"" + jsonEscape(chaudiere.getNomEtatChaudiere()) + "\",";
  json += "\"anomaly\":" + String(!chaudiere.etatChaudiereConnu() ? "null" : etat.anomalieDetecte() ? "true" : "false") + ",";
  json += "\"hotWaterMode\":\"" + jsonEscape(chaudiere.getNomModeECS()) + "\",";
  json += "\"outdoorTemperature\":" + jsonFloat(chaudiere.getTemperatureExterieure()) + ",";
  json += "\"hotWaterTemperature\":" + jsonFloat(chaudiere.getTemperatureECS()) + ",";
  json += "\"heatingBodyTemperature\":" + jsonFloat(chaudiere.getTemperatureCDC()) + ",";
  json += "\"flueTemperature\":" + jsonFloat(chaudiere.getTemperatureFumees()) + ",";
  json += "\"pressure\":" + jsonFloat(chaudiere.getPression(), 2) + ",";
  json += "\"hotWaterPower\":" + jsonFloat(chaudiere.getPuissanceInstantaneeECS()) + ",";
  json += "\"heatingPower\":" + jsonFloat(chaudiere.getPuissanceInstantaneeChauffage()) + ",";
  json += "\"estimatedConsumptionDay\":" + (chaudiere.getJourConsommation() ? String(chaudiere.getJourConsommation()) : String("null")) + ",";
  json += "\"hotWaterConsumption\":" + jsonIntOrNull(chaudiere.getConsommationECS()) + ",";
  json += "\"heatingConsumption\":" + jsonIntOrNull(chaudiere.getConsommationChauffage()) + ",";
  json += "\"monthlyConsumption\":" + jsonConsommationsMensuelles(_frisquetManager.connect().getConsommationsMensuelles());
  json += ",\"previousYearConsumption\":" + jsonConsommationsMensuelles(_frisquetManager.connect().getConsommationsAnneePrecedente(), true);
  json += "},";

  json += "\"outdoorProbe\":{";
  json += "\"enabled\":" + String(cfg.useSondeExterieure() ? "true" : "false") + ",";
  json += "\"temperature\":" + jsonFloat(_frisquetManager.sondeExterieure().getTemperatureExterieure());
  json += "},";

  json += "\"zones\":[";
  auto appendZone = [&](uint8_t id, bool enabled, Zone& zone) {
    const char* source = "Satellite physique";
    if (zone.getSource() == Zone::SOURCE::CONNECT) source = "Connect";
    else if (zone.getSource() == Zone::SOURCE::SATELLITE_VIRTUEL) source = "Satellite virtuel";

    json += "{";
    json += "\"id\":" + String(id) + ",";
    json += "\"enabled\":" + String(enabled ? "true" : "false") + ",";
    json += "\"source\":\"" + String(source) + "\",";
    json += "\"mode\":\"" + jsonEscape(zone.getNomMode()) + "\",";
    json += "\"boost\":" + String(zone.boostActif() ? "true" : "false") + ",";
    json += "\"ambientTemperature\":" + jsonFloat(zone.getTemperatureAmbiante()) + ",";
    json += "\"setpointTemperature\":" + jsonFloat(zone.getTemperatureConsigne()) + ",";
    json += "\"flowTemperature\":" + jsonFloat(zone.getTemperatureDepart()) + ",";
    if(zone.getSource() == Zone::CONNECT && zone.pilotageDisponible()) json += "\"commande\":" + zone.suiviJson() + ",";
    json += "\"flowSetpointTemperature\":" + jsonFloat(zone.getTemperatureConsigneDepart());
    json += "}";
  };

  appendZone(1, cfg.useZone1(), _frisquetManager.connect().getZone1());
  json += ",";
  appendZone(2, cfg.useZone2(), _frisquetManager.connect().getZone2());
  json += ",";
  appendZone(3, cfg.useZone3(), _frisquetManager.connect().getZone3());
  json += "]}";

  _srv.send(200, "application/json; charset=utf-8", json);
}

void Portal::handleGetConfig() {
  auto& w = _frisquetManager.config().getWiFiOptions();
  auto& m = _frisquetManager.config().getMQTTOptions();

  String json = "{";
  json += "\"developerMode\":" + String(_frisquetManager.config().developerMode() ? "true" : "false") + ",";
#if defined(OFV_SCREEN_V3)
  const auto& screenCfg = _frisquetManager.config();
  json += "\"displaySupported\":true,";
  json += "\"displayEnabled\":" + String(screenCfg.displayEnabled() ? "true" : "false") + ",";
  json += "\"displayBrightness\":" + String(screenCfg.displayBrightness()) + ",";
  json += "\"displayTimeout\":" + String(screenCfg.displayTimeout()) + ",";
  json += "\"displayFlip\":" + String(screenCfg.displayFlip() ? "true" : "false") + ",";
  json += "\"displayAutoCycle\":" + String(screenCfg.displayAutoCycle() ? "true" : "false") + ",";
#else
  json += "\"displaySupported\":false,";
#endif
  if (_frisquetManager.config().developerMode()) {
    const auto& cfg = _frisquetManager.config();
    json += "\"radioRxBandwidth\":" + String(cfg.radioRxBandwidth(), 1) + ",";
    json += "\"radioDefaultRxBandwidth\":" + String(RadioSettings::DEFAULT_RX_BANDWIDTH, 1) + ",";
    json += "\"radioLnaBypass\":" + String(cfg.radioLnaBypass() ? "true" : "false") + ",";
    json += "\"radioBypassSupported\":" + String(RadioSettings::supportsLnaBypass() ? "true" : "false") + ",";
    json += "\"radioRxBandwidths\":[";
    bool first = true;
    for (float bandwidth : RadioSettings::RX_BANDWIDTHS) {
      if (!first) json += ",";
      first = false;
      json += String(bandwidth, 1);
    }
    json += "],";
  }
  json += "\"wifiHostname\":\"" + jsonEscape(String(w.hostname)) + "\",";
  json += "\"wifiSsid\":\"" + jsonEscape(String(w.ssid)) + "\",";
  // Les mots de passe ne sont jamais relus : seulement s'ils sont enregistrés.
  json += "\"wifiPassConfigured\":" + String(w.password.length() ? "true" : "false") + ",";
  json += "\"wifiStatic\":" + String(w.useStaticIp ? "true" : "false") + ",";
  json += "\"wifiIp\":\"" + jsonEscape(w.localIp.toString()) + "\",";
  json += "\"wifiGw\":\"" + jsonEscape(w.gateway.toString()) + "\",";
  json += "\"wifiMask\":\"" + jsonEscape(w.subnet.toString()) + "\",";
  json += "\"wifiDns1\":\"" + jsonEscape(w.dns1.toString()) + "\",";
  json += "\"wifiDns2\":\"" + jsonEscape(w.dns2.toString()) + "\",";
  json += "\"mqttHost\":\"" + jsonEscape(String(m.host)) + "\",";
  json += "\"mqttPort\":" + jsonEscape(String(m.port)) + ",";
  json += "\"mqttUser\":\"" + jsonEscape(String(m.username)) + "\",";
  json += "\"mqttPassConfigured\":" + String(m.password.length() ? "true" : "false") + ",";
  json += "\"mqttClientId\":\"" + jsonEscape(String(m.clientId)) + "\",";
  json += "\"mqttBaseTopic\":\"" + jsonEscape(String(m.baseTopic)) + "\",";

  // --- Frisquet ---
  const NetworkID& nid = _frisquetManager.config().getNetworkID(); // adapte le nom si besoin
  json += "\"networkID\":\"" + jsonEscape(networkIdToStr(nid)) + "\",";

  json += "\"useConnect\":" +
          String(_frisquetManager.config().useConnect() ? "true" : "false") + ",";
  json += "\"useConnectPassive\":" +
          String(_frisquetManager.config().useConnectPassive() ? "true" : "false") + ",";
  json += "\"useSondeExt\":" +
          String(_frisquetManager.config().useSondeExterieure() ? "true" : "false") + ",";
  json += "\"useDS18B20\":" +
          String(_frisquetManager.config().useDS18B20() ? "true" : "false") + ",";
  json += "\"useSondeExistante\":" + String(_frisquetManager.config().useSondeExistante() ? "true" : "false") + ",";
    // Zones présentes
    json += "\"useZone1\":" +
      String(_frisquetManager.config().useZone1() ? "true" : "false") + ",";
    json += "\"useZone2\":" +
      String(_frisquetManager.config().useZone2() ? "true" : "false") + ",";
    json += "\"useZone3\":" +
      String(_frisquetManager.config().useZone3() ? "true" : "false") + ",";
   
  // Satellites physiques
  json += "\"useSatelliteZ1\":" +
          String(_frisquetManager.config().useSatelliteZ1() ? "true" : "false") + ",";
  json += "\"useSatelliteZ2\":" +
          String(_frisquetManager.config().useSatelliteZ2() ? "true" : "false") + ",";
  json += "\"useSatelliteZ3\":" +
          String(_frisquetManager.config().useSatelliteZ3() ? "true" : "false") + ",";

  // Satellites virtuels (remplacement)
  json += "\"useSatelliteVirtualZ1\":" +
          String(_frisquetManager.config().useSatelliteVirtualZ1() ? "true" : "false") + ",";
  json += "\"useSatelliteVirtualZ2\":" +
          String(_frisquetManager.config().useSatelliteVirtualZ2() ? "true" : "false") + ",";
  json += "\"useSatelliteVirtualZ3\":" +
          String(_frisquetManager.config().useSatelliteVirtualZ3() ? "true" : "false");

  json += "}";
  _srv.send(200, "application/json; charset=utf-8", json);
}

void Portal::handlePostConfig() {
  if (_srv.method() != HTTP_POST) {
    _srv.send(405, "application/json", "{\"ok\":false,\"err\":\"Méthode non autorisée\"}");
    return;
  }

  auto& cfg = _frisquetManager.config();
  if (_srv.hasArg("useSondeExistante")) {
    const String value = _srv.arg("useSondeExistante");
    if (value != "true" && value != "false" && value != "1" && value != "0") {
      _srv.send(400, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"Choix de sonde existante invalide\"}");
      return;
    }
  }
#if defined(OFV_SCREEN_V3)
  uint16_t screenBrightness = cfg.displayBrightness(), screenTimeout = cfg.displayTimeout();
  bool validScreen = true;
  for (const char* name : {"displayEnabled", "displayFlip", "displayAutoCycle"}) {
    if (_srv.hasArg(name)) {
      const String value = _srv.arg(name);
      if (value != "true" && value != "false") validScreen = false;
    }
  }
  if ((_srv.hasArg("displayBrightness") && !parseDecimalArg(_srv.arg("displayBrightness"), 1, 255, screenBrightness))
      || (_srv.hasArg("displayTimeout") && !parseDecimalArg(_srv.arg("displayTimeout"), 0, 3600, screenTimeout))) validScreen = false;
  if (!validScreen) {
    _srv.send(400, "application/json; charset=utf-8",
              "{\"ok\":false,\"err\":\"Écran : luminosité de 1 à 255, veille de 0 à 3600 secondes, options true/false\"}");
    return;
  }
#endif
  // Ne jamais renvoyer le mot de passe dans GET /api/config. Un champ vide
  // conserve la valeur existante ; valider avant toute mutation de paramètres.
  const String portalPassword = _srv.arg("portalPassword");
  const String portalPasswordConfirm = _srv.arg("portalPasswordConfirm");
  const bool changePortalPassword = !portalPassword.isEmpty();
  if (portalPassword.length() > 64
      || strlen(portalPassword.c_str()) != portalPassword.length()
      || portalPassword != portalPasswordConfirm) {
    _srv.send(400, "application/json; charset=utf-8",
              "{\"ok\":false,\"err\":\"Les mots de passe doivent être identiques et ne pas dépasser 64 octets\"}");
    return;
  }
  // Valider les réglages radio avant toute modification de la configuration.
  const bool hasBandwidth = _srv.hasArg("radioRxBandwidth");
  const bool hasBypass = _srv.hasArg("radioLnaBypass");
  float rxBandwidth = cfg.radioRxBandwidth();
  bool lnaBypass = cfg.radioLnaBypass();
  if (hasBandwidth || hasBypass) {
    if (!requireDeveloperMode()) return;
    if (hasBandwidth) {
      String value = _srv.arg("radioRxBandwidth");
      value.trim();
      char* end = nullptr;
      rxBandwidth = strtof(value.c_str(), &end);
      if (value.length() == 0 || !end || *end != '\0' || !RadioSettings::validRxBandwidth(rxBandwidth)) {
        _srv.send(400, "application/json; charset=utf-8",
                  "{\"ok\":false,\"err\":\"Bande RX non prise en charge par le SX1262\"}");
        return;
      }
    }
    if (hasBypass) {
      if (!RadioSettings::supportsLnaBypass()) {
        _srv.send(400, "application/json; charset=utf-8",
                  "{\"ok\":false,\"err\":\"Bypass LNA disponible uniquement sur Heltec V4.3 avec KCT8103L\"}");
        return;
      }
      const String value = _srv.arg("radioLnaBypass");
      if (value != "true" && value != "false" && value != "1" && value != "0") {
        _srv.send(400, "application/json; charset=utf-8",
                  "{\"ok\":false,\"err\":\"Valeur de bypass LNA invalide\"}");
        return;
      }
      lnaBypass = value == "true" || value == "1";
    }
  }
  // Refuser ce que le core Wi-Fi ou le broker n'accepteraient pas, avant toute modification.
  // Un mot de passe Wi-Fi vide n'est pas contrôlé : réseau ouvert.
  const char* invalidNetwork = nullptr;
  if (_srv.hasArg("wifiHostname") && !isValidHostname(_srv.arg("wifiHostname")))
    invalidNetwork = "Hostname invalide (1 à 31 caractères : lettres, chiffres et tirets, sans tiret au début ni à la fin)";
  else if (_srv.hasArg("wifiSsid") && !fitsBytes(_srv.arg("wifiSsid"), 32))
    invalidNetwork = "SSID trop long (32 octets au plus)";
  else if (!_srv.arg("wifiPass").isEmpty() && !isValidWifiPassword(_srv.arg("wifiPass")))
    invalidNetwork = "Mot de passe Wi-Fi invalide (8 à 63 caractères, ou 64 chiffres hexadécimaux)";
  else if (_srv.hasArg("mqttBaseTopic") && !isValidMqttBaseTopic(_srv.arg("mqttBaseTopic")))
    invalidNetwork = "Préfixe MQTT invalide (sans + ni #)";
  if (invalidNetwork) {
    _srv.send(400, "application/json; charset=utf-8", String("{\"ok\":false,\"err\":\"") + invalidNetwork + "\"}");
    return;
  }

  auto& w = _frisquetManager.config().getWiFiOptions();
  auto& m = _frisquetManager.config().getMQTTOptions();

  // Champs WiFi
  if (_srv.hasArg("wifiHostname")) w.hostname = _srv.arg("wifiHostname");
  if (_srv.hasArg("wifiSsid"))     w.ssid     = _srv.arg("wifiSsid");
  // Mot de passe vide ou absent : conserver celui enregistré. L'effacement
  // (réseau ouvert, broker sans mot de passe) est demandé explicitement.
  if (_srv.hasArg("wifiPass") && !_srv.arg("wifiPass").isEmpty()) w.password = _srv.arg("wifiPass");
  else if (parseBoolArg(_srv.arg("wifiPassClear"), false)) w.password = "";

  // IP statique / DHCP
  if (_srv.hasArg("wifiStatic")) {
    bool cur = w.useStaticIp;
    bool v = parseBoolArg(_srv.arg("wifiStatic"), cur);
    w.useStaticIp = v;
  }
  // Validate IP fields: if provided and invalid, reject the whole request
  if (_srv.hasArg("wifiIp")) {
    String s = _srv.arg("wifiIp");
    if (s.length() && !isValidIPv4(s)) {
      _srv.send(400, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"Adresse IP statique invalide (format attendu: a.b.c.d)\"}");
      return;
    }
    IPAddress ip; if (s.length() && ip.fromString(s)) w.localIp = ip;
  }
  if (_srv.hasArg("wifiGw")) {
    String s = _srv.arg("wifiGw");
    if (s.length() && !isValidIPv4(s)) {
      _srv.send(400, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"Adresse gateway invalide (format attendu: a.b.c.d)\"}");
      return;
    }
    IPAddress ip; if (s.length() && ip.fromString(s)) w.gateway = ip;
  }
  if (_srv.hasArg("wifiMask")) {
    String s = _srv.arg("wifiMask");
    if (s.length() && !isValidIPv4(s)) {
      _srv.send(400, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"Masque de réseau invalide (format attendu: a.b.c.d)\"}");
      return;
    }
    IPAddress ip; if (s.length() && ip.fromString(s)) w.subnet = ip;
  }
  if (_srv.hasArg("wifiDns1")) {
    String s = _srv.arg("wifiDns1");
    if (s.length() && !isValidIPv4(s)) {
      _srv.send(400, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"DNS 1 invalide (format attendu: a.b.c.d)\"}");
      return;
    }
    IPAddress ip; if (s.length() && ip.fromString(s)) w.dns1 = ip;
  }
  if (_srv.hasArg("wifiDns2")) {
    String s = _srv.arg("wifiDns2");
    if (s.length() && !isValidIPv4(s)) {
      _srv.send(400, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"DNS 2 invalide (format attendu: a.b.c.d)\"}");
      return;
    }
    IPAddress ip; if (s.length() && ip.fromString(s)) w.dns2 = ip;
  }

  // Champs MQTT
  if (_srv.hasArg("mqttHost"))     m.host     = _srv.arg("mqttHost");
  if (_srv.hasArg("mqttPort"))     m.port     = (uint16_t)_srv.arg("mqttPort").toInt();
  if (_srv.hasArg("mqttUser"))     m.username = _srv.arg("mqttUser");
  if (_srv.hasArg("mqttPass") && !_srv.arg("mqttPass").isEmpty()) m.password = _srv.arg("mqttPass");
  else if (parseBoolArg(_srv.arg("mqttPassClear"), false)) m.password = "";
  if (_srv.hasArg("mqttClientId")) m.clientId = _srv.arg("mqttClientId");
  if (_srv.hasArg("mqttBaseTopic"))m.baseTopic= _srv.arg("mqttBaseTopic");

  // --- Frisquet: NetworkID ---
  if (_srv.hasArg("networkID")) {
    String s = _srv.arg("networkID");
    NetworkID nid;
    if (parseNetworkIdFromString(s, nid)) {
      _frisquetManager.config().setNetworkID(nid);
      info("[PORTAIL] NetworkID mis à jour: %s", networkIdToStr(nid).c_str());
    } else {
      info("[PORTAIL] NetworkID invalide reçu: '%s'", s.c_str());
      _srv.send(400, "application/json; charset=utf-8", "{\"ok\":false,\"err\":\"NetworkID invalide (format attendu: AA:BB:CC:DD en hexadécimal)\"}");
      return;
    }
  }

  // --- Frisquet: booleans ---
  if (_srv.hasArg("useConnect")) {
    bool cur = _frisquetManager.config().useConnect();
    bool v = parseBoolArg(_srv.arg("useConnect"), cur);
    _frisquetManager.config().useConnect(v);
  }
  if (_srv.hasArg("useConnectPassive")) {
    bool cur = _frisquetManager.config().useConnectPassive();
    bool v = parseBoolArg(_srv.arg("useConnectPassive"), cur);
    if (!_frisquetManager.config().useConnect()) {
      v = false;
    }
    _frisquetManager.config().useConnectPassive(v);
  }
  if (_srv.hasArg("useSondeExt")) {
    bool cur = _frisquetManager.config().useSondeExterieure();
    bool v = parseBoolArg(_srv.arg("useSondeExt"), cur);
    _frisquetManager.config().useSondeExterieure(v);
  }
  if (_srv.hasArg("useDS18B20")) {
    bool cur = _frisquetManager.config().useDS18B20();
    bool v = parseBoolArg(_srv.arg("useDS18B20"), cur);
    _frisquetManager.config().useDS18B20(v);
  }
  if (_srv.hasArg("useSondeExistante")) {
    cfg.useSondeExistante(parseBoolArg(_srv.arg("useSondeExistante"), cfg.useSondeExistante()));
  }
    if (_srv.hasArg("useZone1")) {
      bool cur = _frisquetManager.config().useZone1();
      bool v = parseBoolArg(_srv.arg("useZone1"), cur);
      _frisquetManager.config().useZone1(v);
    }
    if (_srv.hasArg("useZone2")) {
      bool cur = _frisquetManager.config().useZone2();
      bool v = parseBoolArg(_srv.arg("useZone2"), cur);
      _frisquetManager.config().useZone2(v);
    }
    if (_srv.hasArg("useZone3")) {
      bool cur = _frisquetManager.config().useZone3();
      bool v = parseBoolArg(_srv.arg("useZone3"), cur);
      _frisquetManager.config().useZone3(v);
    }

  if (_srv.hasArg("useSatelliteZ1")) {
    bool cur = _frisquetManager.config().useSatelliteZ1();
    bool v = parseBoolArg(_srv.arg("useSatelliteZ1"), cur);
    _frisquetManager.config().useSatelliteZ1(v);
  }
  if (_srv.hasArg("useSatelliteZ2")) {
    bool cur = _frisquetManager.config().useSatelliteZ2();
    bool v = parseBoolArg(_srv.arg("useSatelliteZ2"), cur);
    _frisquetManager.config().useSatelliteZ2(v);
  }
  if (_srv.hasArg("useSatelliteZ3")) {
    bool cur = _frisquetManager.config().useSatelliteZ3();
    bool v = parseBoolArg(_srv.arg("useSatelliteZ3"), cur);
    _frisquetManager.config().useSatelliteZ3(v);
  }

  // Satellites virtuels (remplacement du satellite physique)
  if (_srv.hasArg("useSatelliteVirtualZ1")) {
    bool cur = _frisquetManager.config().useSatelliteVirtualZ1();
    bool v = parseBoolArg(_srv.arg("useSatelliteVirtualZ1"), cur);
    _frisquetManager.config().useSatelliteVirtualZ1(v);
  }
  if (_srv.hasArg("useSatelliteVirtualZ2")) {
    bool cur = _frisquetManager.config().useSatelliteVirtualZ2();
    bool v = parseBoolArg(_srv.arg("useSatelliteVirtualZ2"), cur);
    _frisquetManager.config().useSatelliteVirtualZ2(v);
  }
  if (_srv.hasArg("useSatelliteVirtualZ3")) {
    bool cur = _frisquetManager.config().useSatelliteVirtualZ3();
    bool v = parseBoolArg(_srv.arg("useSatelliteVirtualZ3"), cur);
    _frisquetManager.config().useSatelliteVirtualZ3(v);
  }

  if (_srv.hasArg("developerMode")) {
    auto& cfg = _frisquetManager.config();
    cfg.developerMode(parseBoolArg(_srv.arg("developerMode"), cfg.developerMode()));
  }

  if (changePortalPassword) cfg.portalPassword(portalPassword);
#if defined(OFV_SCREEN_V3)
  if (_srv.hasArg("displayEnabled")) cfg.displayEnabled(_srv.arg("displayEnabled") == "true");
  if (_srv.hasArg("displayFlip")) cfg.displayFlip(_srv.arg("displayFlip") == "true");
  if (_srv.hasArg("displayAutoCycle")) cfg.displayAutoCycle(_srv.arg("displayAutoCycle") == "true");
  cfg.displayBrightness(static_cast<uint8_t>(screenBrightness));
  cfg.displayTimeout(screenTimeout);
#endif
  if (hasBandwidth) cfg.radioRxBandwidth(rxBandwidth);
  if (hasBypass) cfg.radioLnaBypass(lnaBypass);
  _frisquetManager.config().save();
  if (changePortalPassword) clearSessions();
  info("[PORTAIL] Configuration enregistrée, redémarrage programmé");

  _srv.send(200, "application/json; charset=utf-8", "{\"ok\":true,\"reboot\":true}");
  scheduleReboot(800);
}

void Portal::handleReboot() {
  _srv.send(200, "application/json", "{\"ok\":true}");
  scheduleReboot(200);
}

void Portal::handleGetLogs() {
  if (s_logsBusy) {
    _srv.send(429, "application/json; charset=utf-8",
              "{\"ok\":false,\"err\":\"Busy\"}");
    return;
  }
  s_logsBusy = true;
  struct BusyGuard {
    ~BusyGuard() { s_logsBusy = false; }
  } guard;

  // ?limit=100 (par défaut)
  size_t limit = 100;
  if (_srv.hasArg("limit")) {
    int v = _srv.arg("limit").toInt();
    if (v > 0) {
      limit = (size_t)v;
    }
  }
  if (limit > kMaxQueryLines) {
    limit = kMaxQueryLines;
  }

  // ?level=INFO / DEBUG / ERROR / RADIO...
  String level;
  if (_srv.hasArg("level")) {
    level = _srv.arg("level");
  }

  if (level.equalsIgnoreCase("RADIO") && !requireDeveloperMode()) return;

  size_t outCount = logs.getLines(s_logLines, kMaxQueryLines, limit,
                                  level.length() > 0 ? level.c_str() : nullptr);

  // Regrouper la syntaxe JSON et les lignes dans des blocs bornés. Chaque
  // sendContent() produit un chunk HTTP et plusieurs écritures réseau.
  char buffer[1024];
  size_t pos = 0;
  auto flush = [&]() {
    if (pos > 0) {
      _srv.sendContent(buffer, pos);
      pos = 0;
    }
  };
  auto append = [&](char c) {
    buffer[pos++] = c;
    if (pos == sizeof(buffer)) flush();
  };
  auto sendEscapedJson = [&](const char* text) {

    for (const char* p = text; p && *p; ++p) {
      const char* esc = nullptr;
      char unicode[7];
      switch (*p) {
        case '\"': esc = "\\\""; break;
        case '\\': esc = "\\\\"; break;
        case '\b': esc = "\\b"; break;
        case '\f': esc = "\\f"; break;
        case '\n': esc = "\\n"; break;
        case '\r': esc = "\\r"; break;
        case '\t': esc = "\\t"; break;
        default:
          if (static_cast<uint8_t>(*p) < 0x20) {
            snprintf(unicode, sizeof(unicode), "\\u%04X", static_cast<uint8_t>(*p));
            esc = unicode;
          }
          break;
      }

      if (!esc) {
        append(*p);
      } else {
        while (*esc) append(*esc++);
      }
    }
  };

  _srv.setContentLength(CONTENT_LENGTH_UNKNOWN);
  _srv.send(200, "application/json; charset=utf-8", "");
  append('[');
  bool first = true;
  for (size_t i = 0; i < outCount; ++i) {
    if (!first) {
      append(',');
    }
    first = false;

    char formatted[Logs::kMaxFormattedLen];
    s_logLines[i].format(formatted, sizeof(formatted));
    append('"');
    sendEscapedJson(formatted);
    append('"');
  }
  append(']');
  flush();
}


void Portal::handleLogsPage() {
  sendStaticAsset(WebAssets::LOGS_HTML);
}

void Portal::handleStatus() {
  bool sta = WiFi.isConnected();
  wifi_mode_t mode = WiFi.getMode();
  bool ap  = (mode == WIFI_AP || mode == WIFI_AP_STA);
  _apRunning = ap;

  IPAddress ip = WiFi.localIP();
  String ssid = sta ? WiFi.SSID() : "";
  long rssi   = sta ? WiFi.RSSI() : 0;
  uint32_t upMs = millis();
  const char* resetReason = resetReasonToString(esp_reset_reason());
  uint32_t freeHeap = ESP.getFreeHeap();
  uint32_t minFreeHeap = ESP.getMinFreeHeap();

  String json = "{";
  json += "\"apRunning\":"     + String(ap  ? "true" : "false") + ",";
  json += "\"staConnected\":"  + String(sta ? "true" : "false") + ",";
  json += "\"ssid\":\""        + jsonEscape(ssid) + "\",";
  json += "\"ip\":\""          + ip.toString() + "\",";
  json += "\"rssi\":"          + String(rssi) + ",";
  json += "\"uptimeMs\":"      + String(upMs) + ",";
  json += "\"uptimeSec\":"     + String(upMs / 1000) + ",";
  json += "\"resetReason\":\"" + jsonEscape(String(resetReason)) + "\",";
  json += "\"freeHeap\":"      + String(freeHeap) + ",";
  json += "\"minFreeHeap\":"   + String(minFreeHeap);
  json += "}";
  _srv.send(200, "application/json; charset=utf-8", json);
}

// Les outils de diagnostic n'émettent qu'au nom d'un module émulé par la
// passerelle. En Connect passif, l'identité et l'association du Connect sont
// celles de la box observée, et celles d'un satellite physique sont apprises en
// l'observant : n'émettre en leur nom ni lecture mémoire, ni trame brute du Connect.
static bool passiveConnect(Config& cfg) {
  return cfg.useConnect() && cfg.useConnectPassive();
}

static void selectDiagnosticSender(FrisquetManager& manager, uint8_t& idExpediteur, uint8_t& idAssociation) {
  idExpediteur = 0x00;
  idAssociation = 0x00;
  if(manager.config().useConnect() && !passiveConnect(manager.config()) && manager.connect().estAssocie()) {
    idExpediteur = ID_CONNECT;
    idAssociation = manager.connect().getIdAssociation();
  } else if(manager.config().useSatelliteZ1() && manager.config().useSatelliteVirtualZ1()
            && manager.satelliteZ1().estAssocie()) {
    idExpediteur = ID_ZONE_1;
    idAssociation = manager.satelliteZ1().getIdAssociation();
  }
}

void Portal::handleSendRadio() {
  if (!requireDeveloperMode()) return;
  if (!requireRadioAvailable()) return;
  if (_srv.method() != HTTP_POST) {
    _srv.send(405, "application/json; charset=utf-8",
              "{\"ok\":false,\"err\":\"Méthode non autorisée\"}");
    return;
  }

  if (!_srv.hasArg("payload")) {
    _srv.send(400, "application/json; charset=utf-8",
              "{\"ok\":false,\"err\":\"Champ 'payload' manquant\"}");
    return;
  }

  String hex = _srv.arg("payload");
  hex.trim();

  if (!hex.length()) {
    _srv.send(400, "application/json; charset=utf-8",
              "{\"ok\":false,\"err\":\"Payload vide\"}");
    return;
  }

  // Validation basique : hex + espaces
  for (size_t i = 0; i < hex.length(); ++i) {
    char c = hex[i];
    if (!( (c >= '0' && c <= '9') ||
           (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F') ||
           c == ' ')) {
      _srv.send(400, "application/json; charset=utf-8",
                "{\"ok\":false,\"err\":\"Payload non hexadécimal\"}");
      return;
    }
  }

  byte payload[100];
  size_t payloadLength = 0;
  if (!hexStringToBufferRaw(hex, payload, sizeof(payload), payloadLength) || payloadLength == 0) {
    _srv.send(400, "application/json; charset=utf-8",
              "{\"ok\":false,\"err\":\"Trame hexadécimale invalide ou trop longue (100 octets maximum)\"}");
    return;
  }

  // Octet 1 de l'en-tête : expéditeur.
  if (passiveConnect(_frisquetManager.config()) && payloadLength > 1 && payload[1] == ID_CONNECT) {
    _srv.send(403, "application/json; charset=utf-8",
              "{\"ok\":false,\"err\":\"Connect passif : émission au nom du Connect refusée\"}");
    return;
  }

  // Ne pas écraser un vrai paquet RX en attente de traitement par loop().
  if (FrisquetRadio::receivedFlag || FrisquetRadio::interruptReceive) {
    _srv.send(409, "application/json; charset=utf-8",
              "{\"ok\":false,\"err\":\"Radio occupée, réessayez après traitement de la réception\"}");
    return;
  }

  auto& radio = _frisquetManager.radio();
  // Le DIO utilisé pour RX signale aussi TxDone : masquer le callback pendant TX.
  FrisquetRadio::interruptReceive = true;
  const int16_t txResult = radio.transmit(payload, payloadLength);
  // transmit() a effacé les IRQ TX. Réactiver le callback AVANT de réarmer RX
  // pour que la première vraie réponse ne soit pas ignorée.
  FrisquetRadio::interruptReceive = false;
  const int16_t rxResult = radio.startReceive();

  // Journaliser après réarmement pour ne pas retarder la reprise de l'écoute.
  logRadio(false, payload, payloadLength);
  if (txResult != RADIOLIB_ERR_NONE) {
    _srv.send(500, "application/json; charset=utf-8",
              "{\"ok\":false,\"err\":\"Échec envoi radio\",\"code\":" + String(txResult) + "}");
    return;
  }
  if (rxResult != RADIOLIB_ERR_NONE) {
    _srv.send(500, "application/json; charset=utf-8",
              "{\"ok\":false,\"err\":\"Trame envoyée, mais échec de reprise de l'écoute radio\",\"code\":" + String(rxResult) + "}");
    return;
  }
  _srv.send(200, "application/json; charset=utf-8", "{\"ok\":true}");
}

bool Portal::requireRadioAvailable(bool cancelPending) {
  // L'association peut interrompre une observation, jamais une mise à jour ou
  // un redémarrage. Appeler après validation de la demande d'association.
  if (cancelPending && !_frisquetManager.radio().suspended())
    _frisquetManager.radio().cancelOperations();
  if (!_frisquetManager.radio().busy()) return true;
  _srv.sendHeader("Retry-After", "1");
  _srv.send(503, "application/json; charset=utf-8",
            "{\"ok\":false,\"err\":\"Radio occupée, réessayez dans un instant\"}");
  return false;
}

bool Portal::requireDeveloperMode() {
  if (_frisquetManager.config().developerMode()) return true;
  _srv.send(403, "application/json; charset=utf-8",
            "{\"ok\":false,\"err\":\"Activez le mode développeur dans les paramètres système.\"}");
  return false;
}

void Portal::handleRadioLogsPage() {
  if (!requireDeveloperMode()) return;
  sendStaticAsset(WebAssets::RADIO_LOGS_HTML);
}

void Portal::handleMemoryRead() {
  if (!requireDeveloperMode()) return;
  if (!requireRadioAvailable()) return;
  if (_srv.method() != HTTP_GET) {
    _srv.send(405, "application/json; charset=utf-8",
              "{\"ok\":false,\"err\":\"Méthode non autorisée\"}");
    return;
  }

  uint16_t start = 0;
  uint16_t len = 16;

  if (_srv.hasArg("start")) {
    if (!parseUint16Hex(_srv.arg("start"), start)) {
      _srv.send(400, "application/json; charset=utf-8",
                "{\"ok\":false,\"err\":\"Paramètre start invalide\"}");
      return;
    }
  }

  if (_srv.hasArg("len")) {
    if (!parseDecimalArg(_srv.arg("len"), 1, 256, len)) {
      _srv.send(400, "application/json; charset=utf-8",
                "{\"ok\":false,\"err\":\"Paramètre len invalide (1..256)\"}");
      return;
    }
  }

  if (start > 0xFFFF - (len - 1)) {
    len = static_cast<uint16_t>(0xFFFF - start + 1);
  }

  uint8_t idExpediteur = 0x00;
  uint8_t idAssociation = 0x00;
  selectDiagnosticSender(_frisquetManager, idExpediteur, idAssociation);
  if (idExpediteur == 0x00) {
    _srv.send(400, "application/json; charset=utf-8",
              "{\"ok\":false,\"err\":\"Aucun module émetteur associé (Connect actif ou satellite Z1 virtuel)\"}");
    return;
  }

  uint16_t errorAddrs[256];
  int16_t errorCodes[256];
  size_t errorCount = 0;

  auto sendUInt = [this](unsigned long v) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%lu", v);
    _srv.sendContent(buf);
  };

  auto sendInt = [this](long v) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%ld", v);
    _srv.sendContent(buf);
  };

  auto sendHex2 = [this](uint8_t v) {
    char buf[3];
    snprintf(buf, sizeof(buf), "%02X", v);
    _srv.sendContent(buf);
  };

  auto sendHex4 = [this](uint16_t v) {
    char buf[5];
    snprintf(buf, sizeof(buf), "%04X", v);
    _srv.sendContent(buf);
  };

  _srv.setContentLength(CONTENT_LENGTH_UNKNOWN);
  _srv.send(200, "application/json; charset=utf-8", "");
  _srv.sendContent("{\"ok\":true,\"start\":");
  sendUInt(start);
  _srv.sendContent(",\"startHex\":\"");
  sendHex4(start);
  _srv.sendContent("\",\"len\":");
  sendUInt(len);
  _srv.sendContent(",\"words\":[");

  for (uint16_t i = 0; i < len; ++i) {
    uint16_t addr = static_cast<uint16_t>(start + i);
    byte resp[32];
    size_t respLen = sizeof(resp);


    int16_t err = _frisquetManager.radio().sendAsk(
        idExpediteur,
        _frisquetManager.config().boilerId(),
        idAssociation,
        ++s_memoryMessageId,
        0x01,
        addr,
        0x0001,
        resp,
        respLen,
      5);

    int value = -1;
    if (err == RADIOLIB_ERR_NONE) {
      size_t headerSize = sizeof(FrisquetRadio::RadioTrameHeader);
      if (respLen > headerSize) {
        uint8_t dataLen = resp[headerSize];
        if (dataLen >= 2 && respLen >= headerSize + 1 + dataLen) {
          uint8_t b1 = resp[headerSize + 1];
          uint8_t b2 = resp[headerSize + 2];
          value = (static_cast<int>(b1) << 8) | static_cast<int>(b2);
        } else if (respLen >= headerSize + 3) {
          uint8_t b1 = resp[respLen - 2];
          uint8_t b2 = resp[respLen - 1];
          value = (static_cast<int>(b1) << 8) | static_cast<int>(b2);
        }
      }
    }

    if (i > 0) {
      _srv.sendContent(",");
    }
    _srv.sendContent("\"");
    if (value >= 0) {
      sendHex4(static_cast<uint16_t>(value));
    } else {
      _srv.sendContent("??");
      if (errorCount < 256) {
        errorAddrs[errorCount] = addr;
        errorCodes[errorCount] = err == RADIOLIB_ERR_NONE ? 1 : err;
        ++errorCount;
      }
    }
    _srv.sendContent("\"");
  }

  _srv.sendContent("],\"errors\":[");
  for (size_t i = 0; i < errorCount; ++i) {
    if (i > 0) {
      _srv.sendContent(",");
    }
    _srv.sendContent("{\"addr\":\"");
    sendHex4(errorAddrs[i]);
    _srv.sendContent("\",\"err\":");
    sendInt(errorCodes[i]);
    _srv.sendContent("}");
  }
  _srv.sendContent("]}");
}

void Portal::handleMemoryScan() {
  if (!requireDeveloperMode()) return;
  if (!requireRadioAvailable()) return;
  if (_srv.method() != HTTP_GET) {
    _srv.send(405, "application/json; charset=utf-8",
              "{\"ok\":false,\"err\":\"Méthode non autorisée\"}");
    return;
  }

  uint16_t start = 0;
  uint16_t maxScan = 128;
  uint16_t step = 1;
  bool stopOnValid = true;

  if (_srv.hasArg("start")) {
    if (!parseUint16Hex(_srv.arg("start"), start)) {
      _srv.send(400, "application/json; charset=utf-8",
                "{\"ok\":false,\"err\":\"Paramètre start invalide\"}");
      return;
    }
  }

  if (_srv.hasArg("max")) {
    if (!parseDecimalArg(_srv.arg("max"), 1, 512, maxScan)) {
      _srv.send(400, "application/json; charset=utf-8",
                "{\"ok\":false,\"err\":\"Paramètre max invalide (1..512)\"}");
      return;
    }
  }

  if (_srv.hasArg("step")) {
    if (!parseDecimalArg(_srv.arg("step"), 1, 256, step)) {
      _srv.send(400, "application/json; charset=utf-8",
                "{\"ok\":false,\"err\":\"Paramètre step invalide (1..256)\"}");
      return;
    }
  }

  if (_srv.hasArg("stopOnValid")) {
    stopOnValid = parseBoolArg(_srv.arg("stopOnValid"), true);
  }

  uint16_t foundAddr = 0;
  uint16_t foundValue = 0;
  bool found = false;
  int16_t lastErr = 0;
  uint16_t scanned = 0;

   uint8_t idExpediteur = 0x00;
   uint8_t idAssociation = 0x00;
    selectDiagnosticSender(_frisquetManager, idExpediteur, idAssociation);
    if(idExpediteur == 0x00) {
      _srv.send(400, "application/json; charset=utf-8",
                "{\"ok\":false,\"err\":\"Aucun module émetteur associé (Connect actif ou satellite Z1 virtuel)\"}");
      return;
    }

  for (uint16_t i = 0; i < maxScan; ++i) {
    uint16_t addr = static_cast<uint16_t>(start + (i * step));
    byte resp[32];
    size_t respLen = sizeof(resp);

    int16_t err = _frisquetManager.radio().sendAsk(
        idExpediteur,
        _frisquetManager.config().boilerId(),
        idAssociation,
        ++s_memoryMessageId,
        0x01,
        addr,
        0x0001,
        resp,
        respLen,
        5);

    ++scanned;
    if (err != RADIOLIB_ERR_NONE) {
      lastErr = err;
      continue;
    }

    if (respLen >= sizeof(FrisquetRadio::RadioTrameHeader)) {
      FrisquetRadio::RadioTrameHeader header;
      memcpy(&header, resp, sizeof(header));
      if (header.type == FrisquetRadio::MessageType::READ) {
        size_t headerSize = sizeof(FrisquetRadio::RadioTrameHeader);
        if (respLen >= headerSize + 3) {
          uint8_t b1 = resp[headerSize + 1];
          uint8_t b2 = resp[headerSize + 2];
          foundValue = (static_cast<uint16_t>(b1) << 8) | b2;
        } else {
          foundValue = 0;
        }
        foundAddr = addr;
        found = true;
        if (stopOnValid) {
          break;
        }
      }
    }
  }

  String json = "{";
  json += "\"ok\":true,";
  char startBuf[5];
  snprintf(startBuf, sizeof(startBuf), "%04X", start);
  json += "\"startHex\":\"" + String(startBuf) + "\",";
  json += "\"scanned\":" + String(scanned) + ",";
  json += "\"found\":" + String(found ? "true" : "false") + ",";
  if (found) {
    char buf[5];
    snprintf(buf, sizeof(buf), "%04X", foundAddr);
    json += "\"addr\":\"" + String(buf) + "\",";
    snprintf(buf, sizeof(buf), "%04X", foundValue);
    json += "\"value\":\"" + String(buf) + "\",";
  }
  json += "\"lastErr\":" + String(lastErr);
  json += "}";
  _srv.send(200, "application/json; charset=utf-8", json);
}

void Portal::handleMemoryPage() {
  if (!requireDeveloperMode()) return;
  sendStaticAsset(WebAssets::MEMORY_HTML);
}

void Portal::handlePairConnect() {
  if (_srv.method() != HTTP_POST) {
    _srv.send(405, "application/json; charset=utf-8",
              "{\"ok\":false,\"err\":\"Méthode non autorisée\"}");
    return;
  }

  if (!_frisquetManager.config().useConnect()) {
    _srv.send(400, "application/json; charset=utf-8",
              "{\"ok\":false,\"err\":\"Connect désactivé dans la configuration\"}");
    return;
  }
  if (_frisquetManager.config().useConnectPassive()) {
    _srv.send(400, "application/json; charset=utf-8",
              "{\"ok\":false,\"err\":\"Mode passif activé : association Connect désactivée\"}");
    return;
  }

  if (!requireRadioAvailable(true)) return;

  info("[PORTAIL] Demande d'association du module Connect");

  bool ok = false;
  // Exemple : si tu as une méthode dédiée
  
  NetworkID networkId;
  uint8_t idAssociation;
  if(_frisquetManager.connect().associer(networkId, idAssociation)) {
      _frisquetManager.connect().setIdAssociation(idAssociation);
      _frisquetManager.radio().setNetworkID(networkId);
      _frisquetManager.config().setNetworkID(networkId);
      _frisquetManager.config().save();
      _frisquetManager.connect().saveConfig();
      info("[PORTAIL] Association réussie.");
      ok = true;
  } else {
    error("[PORTAIL] Échec de l'association.");
  }

  if (ok) {
    _srv.send(200, "application/json; charset=utf-8",
              "{\"ok\":true,\"msg\":\"Association Connect lancée\"}");
  } else {
    _srv.send(500, "application/json; charset=utf-8",
              "{\"ok\":false,\"err\":\"Échec lancement association Connect\"}");
  }
}

void Portal::handlePairSondeExt() {
  if (_srv.method() != HTTP_POST) {
    _srv.send(405, "application/json; charset=utf-8",
              "{\"ok\":false,\"err\":\"Méthode non autorisée\"}");
    return;
  }

  if (!_frisquetManager.config().useSondeExterieure()) {
    _srv.send(400, "application/json; charset=utf-8",
              "{\"ok\":false,\"err\":\"Sonde extérieure désactivée dans la configuration\"}");
    return;
  }

  if (!requireRadioAvailable(true)) return;

  info("[PORTAIL] Demande d'association de la sonde extérieure");

  bool ok = false;
  
  NetworkID networkId;
  uint8_t idAssociation;
  if(_frisquetManager.sondeExterieure().associer(networkId, idAssociation)) {
      _frisquetManager.sondeExterieure().setIdAssociation(idAssociation);
      _frisquetManager.radio().setNetworkID(networkId);
      _frisquetManager.config().setNetworkID(networkId);
      _frisquetManager.config().save();
      _frisquetManager.sondeExterieure().saveConfig();
      info("[PORTAIL] Association réussie.");
      ok = true;
  } else {
    error("[PORTAIL] Échec de l'association.");
  }

  if (ok) {
    _srv.send(200, "application/json; charset=utf-8",
              "{\"ok\":true,\"msg\":\"Association sonde extérieure lancée\"}");
  } else {
    _srv.send(500, "application/json; charset=utf-8",
              "{\"ok\":false,\"err\":\"Échec lancement association sonde extérieure\"}");
  }
}

void Portal::handlePairSatelliteZ1() {
  if (_srv.method() != HTTP_POST) {
    _srv.send(405, "application/json; charset=utf-8",
              "{\"ok\":false,\"err\":\"Méthode non autorisée\"}");
    return;
  }

  if (!_frisquetManager.config().useSatelliteZ1()) {
    _srv.send(400, "application/json; charset=utf-8",
              "{\"ok\":false,\"err\":\"Satellite Z1 désactivé dans la configuration\"}");
    return;
  }

  if (!requireRadioAvailable(true)) return;

  info("[PORTAIL] Demande d'association du Satellite Z1");

  bool ok = false;
  NetworkID networkId;
  uint8_t idAssociation;

  // ⚠️ À ADAPTER selon ton API réelle :
  //   satelliteZ1(), setIdAssociation(), saveConfig()...
  if (_frisquetManager.satelliteZ1().associer(networkId, idAssociation)) {
    _frisquetManager.satelliteZ1().setIdAssociation(idAssociation);
    _frisquetManager.radio().setNetworkID(networkId);
    _frisquetManager.config().setNetworkID(networkId);
    _frisquetManager.config().save();
    _frisquetManager.satelliteZ1().saveConfig();
    info("[PORTAIL] Association Satellite Z1 réussie.");
    ok = true;
  } else {
    error("[PORTAIL] Échec de l'association Satellite Z1.");
  }

  if (ok) {
    _srv.send(200, "application/json; charset=utf-8",
              "{\"ok\":true,\"msg\":\"Association Satellite Z1 lancée\"}");
  } else {
    _srv.send(500, "application/json; charset=utf-8",
              "{\"ok\":false,\"err\":\"Échec lancement association Satellite Z1\"}");
  }
}

void Portal::handlePairSatelliteZ2() {
  if (_srv.method() != HTTP_POST) {
    _srv.send(405, "application/json; charset=utf-8",
              "{\"ok\":false,\"err\":\"Méthode non autorisée\"}");
    return;
  }

  if (!_frisquetManager.config().useSatelliteZ2()) {
    _srv.send(400, "application/json; charset=utf-8",
              "{\"ok\":false,\"err\":\"Satellite Z2 désactivé dans la configuration\"}");
    return;
  }

  if (!requireRadioAvailable(true)) return;

  info("[PORTAIL] Demande d'association du Satellite Z2");

  bool ok = false;
  NetworkID networkId;
  uint8_t idAssociation;

  // ⚠️ À ADAPTER selon ton API réelle :
  if (_frisquetManager.satelliteZ2().associer(networkId, idAssociation)) {
    _frisquetManager.satelliteZ2().setIdAssociation(idAssociation);
    _frisquetManager.radio().setNetworkID(networkId);
    _frisquetManager.config().setNetworkID(networkId);
    _frisquetManager.config().save();
    _frisquetManager.satelliteZ2().saveConfig();
    info("[PORTAIL] Association Satellite Z2 réussie.");
    ok = true;
  } else {
    error("[PORTAIL] Échec de l'association Satellite Z2.");
  }

  if (ok) {
    _srv.send(200, "application/json; charset=utf-8",
              "{\"ok\":true,\"msg\":\"Association Satellite Z2 lancée\"}");
  } else {
    _srv.send(500, "application/json; charset=utf-8",
              "{\"ok\":false,\"err\":\"Échec lancement association Satellite Z2\"}");
  }
}

void Portal::handlePairSatelliteZ3() {
  if (_srv.method() != HTTP_POST) {
    _srv.send(405, "application/json; charset=utf-8",
              "{\"ok\":false,\"err\":\"Méthode non autorisée\"}");
    return;
  }

  if (!_frisquetManager.config().useSatelliteZ3()) {
    _srv.send(400, "application/json; charset=utf-8",
              "{\"ok\":false,\"err\":\"Satellite Z3 désactivé dans la configuration\"}");
    return;
  }

  if (!requireRadioAvailable(true)) return;

  info("[PORTAIL] Demande d'association du Satellite Z3");

  bool ok = false;
  NetworkID networkId;
  uint8_t idAssociation;

  // ⚠️ À ADAPTER selon ton API réelle :
  if (_frisquetManager.satelliteZ3().associer(networkId, idAssociation)) {
    _frisquetManager.satelliteZ3().setIdAssociation(idAssociation);
    _frisquetManager.radio().setNetworkID(networkId);
    _frisquetManager.config().setNetworkID(networkId);
    _frisquetManager.config().save();
    _frisquetManager.satelliteZ3().saveConfig();
    info("[PORTAIL] Association Satellite Z3 réussie.");
    ok = true;
  } else {
    error("[PORTAIL] Échec de l'association Satellite Z3.");
  }

  if (ok) {
    _srv.send(200, "application/json; charset=utf-8",
              "{\"ok\":true,\"msg\":\"Association Satellite Z3 lancée\"}");
  } else {
    _srv.send(500, "application/json; charset=utf-8",
              "{\"ok\":false,\"err\":\"Échec lancement association Satellite Z3\"}");
  }
}

void Portal::handleRecupNetworkId() {
  if (_srv.method() != HTTP_POST) {
    _srv.send(405, "application/json; charset=utf-8",
              "{\"ok\":false,\"err\":\"Méthode non autorisée\"}");
    return;
  }

  if (!requireRadioAvailable(true)) return;

  info("[PORTAIL] Demande de récupération du NetworkID");

  if (_frisquetManager.recupererNetworkID()) {
    const NetworkID& nid = _frisquetManager.config().getNetworkID();
    String json = "{";
    json += "\"ok\":true,";
    json += "\"networkID\":\"" + jsonEscape(networkIdToStr(nid)) + "\",";
    json += "\"msg\":\"NetworkID récupéré\"";
    json += "}";
    _srv.send(200, "application/json; charset=utf-8", json);
  } else {
    _srv.send(500, "application/json; charset=utf-8",
              "{\"ok\":false,\"err\":\"Échec récupération NetworkID\"}");
  }
}


// -------------------- Utils --------------------

bool Portal::scheduleReboot(uint32_t delayMs) {
  _frisquetManager.radio().suspend(FrisquetRadio::Pause::Reboot, true);
  const auto result = xTaskCreatePinnedToCore([](void* d){
    uint32_t ms = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(d));
    vTaskDelay(ms / portTICK_PERIOD_MS);
    ESP.restart();
    vTaskDelete(NULL);
  }, "rebooter", 2048, reinterpret_cast<void*>(static_cast<uintptr_t>(delayMs)), 1, nullptr, ARDUINO_RUNNING_CORE);
  if (result != pdPASS) {
    _frisquetManager.radio().suspend(FrisquetRadio::Pause::Reboot, false);
    error("[PORTAIL] Impossible de créer la tâche de redémarrage");
    return false;
  }
  return true;
}

void Portal::startAp() {
  WiFi.mode(WIFI_AP_STA);
  bool ok;
  if (_apPass.length() >= 8) ok = WiFi.softAP(_apSsid.c_str(), _apPass.c_str());
  else                       ok = WiFi.softAP(_apSsid.c_str()); // open
  _apRunning = ok;
  IPAddress ip = WiFi.softAPIP();
  if (ok) {
    info("[PORTAIL] AP fallback %s (%s) %s",
         _apSsid.c_str(),
         (_apPass.length() >= 8 ? "WPA2" : "OPEN"),
         ip.toString().c_str());
  } else {
    error("[PORTAIL] AP fallback: échec du démarrage");
  }
}

bool Portal::hexStringToBufferRaw(const String& hex, uint8_t* buffer, size_t maxLen, size_t& outLen) {
    outLen = 0;

    String s = hex;
    s.trim();
    s.replace(" ", "");
    s.replace("\n", "");
    s.replace("\t", "");

    if (s.length() == 0) return true;      // OK, buffer vide
    if (s.length() % 2 != 0) return false; // longueur impaire

    size_t needed = s.length() / 2;
    if (needed > maxLen) return false;     // dépasse la taille fournie

    for (size_t i = 0; i < s.length(); i += 2) {
        char c1 = s[i];
        char c2 = s[i+1];

        if (!isxdigit(c1) || !isxdigit(c2)) {
            return false;
        }

        buffer[outLen++] = strtol(s.substring(i, i+2).c_str(), nullptr, 16);
    }

    return true;
}
