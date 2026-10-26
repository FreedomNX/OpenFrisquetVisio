#include "OFVNetworkManager.h"

void OFVNetworkManager::begin(const Options& opts) {
  _opts = opts;

  // Mode station only
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(_opts.wifiSleep);
  WiFi.setAutoReconnect(true);

#ifdef ESP_ARDUINO_VERSION_MAJOR
  // Hostname (ESP32)
  WiFi.setHostname(_opts.hostname.c_str());
#endif

  if (_opts.useStaticIp) {
    WiFi.config(_opts.localIp, _opts.gateway, _opts.subnet, _opts.dns1, _opts.dns2);
  } else {
    // DHCP par défaut
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
  }

  attachEvents();
  startConnect();
}

void OFVNetworkManager::attachEvents() {
  // Un seul callback global, on switch par type d’événement
  _evAny = WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info){
    switch (event) {

      case ARDUINO_EVENT_WIFI_STA_CONNECTED: {
        // Associé au point d'accès (pas d'IP encore)
        break;
      }

      case ARDUINO_EVENT_WIFI_STA_GOT_IP: {
        _tConnectStart = 0;
        _tNextAttempt = 0;
        _failCount = 0;
        _everConnected = true;

        if (_onConnected) _onConnected();
        break;
      }

      case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
        // Raison de déconnexion
        uint8_t r = info.wifi_sta_disconnected.reason;
        String reason = reasonToString(r);

        if (_onDisconnected) _onDisconnected(reason);

        if (_opts.autoReconnect) {
          scheduleReconnect();
        }
        break;
      }

      default: break;
    }
  });
}

void OFVNetworkManager::startConnect() {
  if (_opts.ssid.isEmpty()) return;

  // Petite protection : si déjà connecté, ne rien faire
  if (WiFi.isConnected()) return;

  // Début tentative
  _tConnectStart = millis();
  info("[WIFI] Connexion au WiFi...");
  WiFi.begin(_opts.ssid.c_str(), _opts.password.c_str());

  uint8_t status = WiFi.waitForConnectResult(10000);
  if(status == WL_CONNECTED) {
    info("[WiFi] Connexion établie.");
  } else {
    info("[WiFi] Échec de la connexion.");
  }
}

void OFVNetworkManager::scheduleReconnect() {
  // Couper proprement avant tentative
  WiFi.disconnect(true, false);

  // Calcule next attempt (backoff exponentiel borné + jitter)
  uint32_t delayMs = computeBackoffMs();
  _tNextAttempt = millis() + delayMs;

  // Incrémente le compteur d'échec (borné)
  if (_failCount < 10) _failCount++;
}

uint32_t OFVNetworkManager::computeBackoffMs() const {
  // backoff = min(max, min * 2^failCount) + jitter(0..1s)
  uint32_t base = _opts.reconnectMinMs;
  uint32_t cap  = _opts.reconnectMaxMs;
  uint32_t exp  = base << (_failCount > 8 ? 8 : _failCount); // clamp pour éviter overflow
  if (exp < base) exp = cap; // overflow safety
  uint32_t backoff = exp > cap ? cap : exp;

  uint32_t jitter = random(0, 1000); // 0..999 ms
  return backoff + jitter;
}

void OFVNetworkManager::loop() {
  // Si connecté, rien à faire ici
  if (WiFi.isConnected()) return;

  // Si une tentative est en cours trop longue, on replanifie
  if (_tConnectStart && (millis() - _tConnectStart > _opts.firstConnectTimeoutMs)) {
    scheduleReconnect();
    _tConnectStart = 0;
  }

  // Tentative planifiée ?
  if (_tNextAttempt && (int32_t)(millis() - _tNextAttempt) >= 0) {
    _tNextAttempt = 0;
    startConnect();
  }
}

void OFVNetworkManager::requestReconnectNow() {
  // Force la re-tentative immédiate
  WiFi.disconnect(true, false);
  _tNextAttempt = 0;
  _tConnectStart = 0;
  _failCount = 0;
  startConnect();
}

String OFVNetworkManager::reasonToString(uint8_t reason) {
  // Principaux codes (esp_wifi_types.h)
  switch (reason) {
    case WIFI_REASON_UNSPECIFIED: return "unspecified";
    case WIFI_REASON_AUTH_EXPIRE: return "auth expire";
    case WIFI_REASON_AUTH_LEAVE: return "auth leave";
    case WIFI_REASON_ASSOC_EXPIRE: return "assoc expire";
    case WIFI_REASON_ASSOC_TOOMANY: return "too many associations";
    case WIFI_REASON_NOT_AUTHED: return "not authed";
    case WIFI_REASON_NOT_ASSOCED: return "not associated";
    case WIFI_REASON_ASSOC_LEAVE: return "assoc leave";
    case WIFI_REASON_ASSOC_NOT_AUTHED: return "assoc not authed";
    case WIFI_REASON_DISASSOC_PWRCAP_BAD: return "disassoc powercap bad";
    case WIFI_REASON_DISASSOC_SUPCHAN_BAD: return "disassoc supchan bad";
    case WIFI_REASON_IE_INVALID: return "ie invalid";
    case WIFI_REASON_MIC_FAILURE: return "mic failure";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: return "4-way timeout";
    case WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT: return "group key timeout";
    case WIFI_REASON_IE_IN_4WAY_DIFFERS: return "ie differs";
    case WIFI_REASON_GROUP_CIPHER_INVALID: return "group cipher invalid";
    case WIFI_REASON_PAIRWISE_CIPHER_INVALID: return "pairwise cipher invalid";
    case WIFI_REASON_AKMP_INVALID: return "akmp invalid";
    case WIFI_REASON_UNSUPP_RSN_IE_VERSION: return "rsn ie version";
    case WIFI_REASON_INVALID_RSN_IE_CAP: return "invalid rsn ie cap";
    case WIFI_REASON_802_1X_AUTH_FAILED: return "802.1x auth failed";
    case WIFI_REASON_CIPHER_SUITE_REJECTED: return "cipher suite rejected";
    case WIFI_REASON_INVALID_PMKID: return "invalid pmkid";
    case WIFI_REASON_BEACON_TIMEOUT: return "beacon timeout";
    case WIFI_REASON_NO_AP_FOUND: return "no ap found";
    case WIFI_REASON_AUTH_FAIL: return "auth fail";
    case WIFI_REASON_ASSOC_FAIL: return "assoc fail";
    case WIFI_REASON_HANDSHAKE_TIMEOUT: return "handshake timeout";
    default: {
      char buf[16]; snprintf(buf, sizeof(buf), "code %u", reason);
      return String(buf);
    }
  }
}
