#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include <Preferences.h>
#include "Config.h"
#include "Logs.h"
#include "FrisquetManager.h"

// Portail Web de configuration + logs (adapté à ton Config)
class Portal {
public:
  explicit Portal(FrisquetManager& frisquetManager, uint16_t port = 80);

  // startApFallbackIfNoWifi = monte un AP "ESP32-Setup" si pas de Wi-Fi actif
  void begin(bool startApFallbackIfNoWifi = false);
  void startAp();
  void loop();

private:
  WebServer _srv;
  FrisquetManager& _frisquetManager;

  // AP fallback
  bool _apRunning = false;
  String _apSsid = "OpenFrisquetVisio-Setup";
  String _apPass = "frisquetconfig";

  static constexpr uint32_t SESSION_DURATION_MS = 12UL * 60 * 60 * 1000;
  struct Session {
    char token[65] = {};
    uint64_t started = 0;
  };
  Session _sessions[4];
  uint8_t _failedLogins = 0;
  bool _loginBlocked = false;
  uint32_t _loginBlockedAt = 0;

  // Handlers
  void handleLoginPage();
  void handleLogin();
  void handleLogout();
  void handleIndex();            // GET /
  void handleSettingsPage();     // GET /settings
  void handleLogo();             // GET /logo.png
  void handlePing();             // GET /api/ping
  void handleDashboard();        // GET /api/dashboard
  void handleProgrammationPage();
  void handleGetProgrammation();
  void handlePostProgrammation();
  void handleGetConfig();        // GET /api/config
  void handlePostConfig();       // POST /api/config
  void handleReboot();           // POST /api/reboot
  void handleGetLogs();          // GET /api/logs
  void handleLogsPage();         // GET /logs
  void handleClearLogs();        // GET /logs/clear
  void handleMemoryRead();       // GET /api/memory
  void handleMemoryScan();       // GET /api/memory/scan
  void handleMemoryPage();       // GET /memory
  void handleStatus();
  void handleRadioLogsPage();
  void handleSendRadio();
  void handleUpdate();           // POST /api/update (finish)
  void handleUpdateUpload();     // POST /api/update (upload)
  void handlePairConnect();
  void handlePairSondeExt();
  void handlePairSatelliteZ1();
  void handlePairSatelliteZ2();
  void handlePairSatelliteZ3();
  void handleRecupNetworkId();


  // Utils
  bool isAuthenticated();
  bool requireAuthentication();
  String sessionCookie();
  void createSession();
  void clearSessions();
  void sendLoginPage(int status, const char* message);
  void onAuthenticated(const char* uri, HTTPMethod method, void (Portal::*handler)(),
                       void (Portal::*uploadHandler)() = nullptr);
  bool requireDeveloperMode();
  static String html();
  static String settingsHtml();
  static String programmationHtml();
  static String logsHtml();
  static String memoryHtml();
  String logsRadioHtml();
  void scheduleReboot(uint32_t delayMs = 800);
  bool hexStringToBufferRaw(const String& hex, uint8_t* buffer, size_t maxLen, size_t& outLen);

  // AP

  String _lastUpdateError;
};
