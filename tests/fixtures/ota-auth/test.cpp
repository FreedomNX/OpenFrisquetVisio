#include "radio-fixture.h"
void delay(uint32_t) {}
void error(const char *, ...) {}
#include "OTA.h"

struct Config {
    String password;
    const String &portalPassword() { return password; }
};
struct NetworkManager {
    String hostname() { return "openfrisquetvisio"; }
};
struct App {
    OTA _ota;
    Config _cfg;
    NetworkManager _networkManager;
    FrisquetRadio _radio;
    void initOta();
};
#include "app-ota-methods.inc"

enum HTTPMethod { HTTP_POST };
enum UploadStatus { UPLOAD_FILE_START, UPLOAD_FILE_WRITE, UPLOAD_FILE_END, UPLOAD_FILE_ABORTED };
struct HTTPUpload {
    UploadStatus status = UPLOAD_FILE_START;
    String filename = "firmware.bin";
    unsigned totalSize = 4, currentSize = 4;
    byte buf[4]{};
};
struct WebServer {
    HTTPUpload data;
    int status = 0;
    std::function<void()> finish, chunk;
    void on(const char *, HTTPMethod, std::function<void()> f, std::function<void()> u = {}) {
        finish = f; chunk = u;
    }
    HTTPUpload &upload() { return data; }
    void send(int s, const char *, const String &) { status = s; }
};
constexpr unsigned UPDATE_SIZE_UNKNOWN = 0;
int Serial = 0;
struct UpdateStub {
    unsigned begins = 0, writes = 0, ends = 0, aborts = 0;
    bool beginOk = true, writeOk = true, endOk = true, failed = false;
    bool begin(unsigned, int) { ++begins; failed = !beginOk; return beginOk; }
    unsigned write(byte *, unsigned n) { ++writes; failed = !writeOk; return writeOk ? n : 0; }
    bool end(bool) { ++ends; failed = !endOk; return endOk; }
    void abort() { ++aborts; }
    bool hasError() { return failed; }
    void printError(int) {}
} Update;
constexpr int pdPASS = 1, portTICK_PERIOD_MS = 1, ARDUINO_RUNNING_CORE = 1;
bool taskOk = true;
unsigned tasks = 0;
void vTaskDelay(uint32_t) {}
void vTaskDelete(void *) {}
struct { void restart() {} } ESP;
int xTaskCreatePinnedToCore(void (*)(void *), const char *, int, void *, int, void *, int) {
    ++tasks;
    return taskOk ? pdPASS : 0;
}
struct Manager {
    FrisquetRadio r;
    FrisquetRadio &radio() { return r; }
};
struct Portal {
    WebServer _srv;
    Manager _frisquetManager;
    bool authenticated = true;
    bool _httpUpdateActive = false;
    String _lastUpdateError;
    bool isAuthenticated() { return authenticated; }
    bool requireAuthentication() {
        if (!authenticated) _srv.status = 401;
        return authenticated;
    }
    void onAuthenticated(const char *, HTTPMethod, void (Portal::*)(), void (Portal::*)() = nullptr);
    void cancelHttpUpdate();
    void handleUpdate();
    void handleUpdateUpload();
    bool scheduleReboot(uint32_t);
    Portal() {
        Update = UpdateStub{}; tasks = 0; taskOk = true;
        onAuthenticated("/api/update", HTTP_POST, &Portal::handleUpdate, &Portal::handleUpdateUpload);
    }
    void upload(UploadStatus s) { _srv.data.status = s; _srv.chunk(); }
};
#include "portal-ota-methods.inc"
using R = FrisquetRadio;
void reserve(R &r) {
    R::RadioTrameHeader h;
    h.idExpediteur = 0x7e; h.idDestinataire = 0x80;
    assert(r.beginObservation(8, h));
}
int main() {
    for (const char *password : {"admin", "a'b$;\"&"}) {
        ArduinoOTA = ArduinoOTAStub{};
        App app;
        app._cfg.password = password;
        app.initOta(); app._ota.loop();
        assert(ArduinoOTA.started && ArduinoOTA.hostname == "openfrisquetvisio");
        assert(ArduinoOTA.passwordAtBegin == password && ArduinoOTA.handled == 1);
        reserve(app._radio);
        ArduinoOTA.start();
        assert(app._radio.busy() && !app._radio.ownsObservation(8));
        ArduinoOTA.end();
        assert(!app._radio.busy());
        ArduinoOTA.start(); ArduinoOTA.failure(1);
        assert(!app._radio.busy());
        app._radio.suspend(R::Pause::HttpOta, true);
        ArduinoOTA.start(); ArduinoOTA.end();
        assert(app._radio.busy());
        app._radio.suspend(R::Pause::HttpOta, false);
        assert(!app._radio.busy());
    }
    // Requête entièrement refusée : aucune écriture ni pause étrangère modifiée.
    for (auto reason : {R::Pause::ArduinoOta, R::Pause::Reboot, R::Pause::HttpOta}) {
        Portal p;
        p.authenticated = false;
        p._frisquetManager.r.suspend(reason, true);
        for (auto s : {UPLOAD_FILE_START, UPLOAD_FILE_WRITE, UPLOAD_FILE_END}) p.upload(s);
        p._srv.finish();
        assert(p._srv.status == 401 && !Update.begins && !Update.writes && !Update.ends && !Update.aborts);
        assert(p._frisquetManager.r.busy());
    }
    // Expiration à chaque frontière, y compris après la dernière écriture flash.
    for (unsigned boundary = 0; boundary < 3; ++boundary) {
        Portal p;
        reserve(p._frisquetManager.r);
        p.upload(UPLOAD_FILE_START);
        assert(p._frisquetManager.r.busy() && !p._frisquetManager.r.ownsObservation(8));
        if (boundary > 0) p.upload(UPLOAD_FILE_WRITE);
        if (boundary > 1) p.upload(UPLOAD_FILE_END);
        const auto writes = Update.writes, ends = Update.ends;
        p.authenticated = false;
        if (boundary == 0) p.upload(UPLOAD_FILE_WRITE);
        if (boundary == 1) p.upload(UPLOAD_FILE_END);
        p._srv.finish();
        assert(p._srv.status == 401 && Update.aborts == 1 && !tasks);
        assert(Update.writes == writes && Update.ends == ends && !p._frisquetManager.r.busy());
        // Même une authentification redevenue valide ne reprend pas cet upload.
        p.authenticated = true;
        p.upload(UPLOAD_FILE_WRITE); p.upload(UPLOAD_FILE_END); p._srv.finish();
        assert(Update.writes == writes && Update.ends == ends && !tasks && p._srv.status == 500);
    }
    // Le parseur peut signaler une interruption après l'expiration de session.
    for (bool written : {false, true}) {
        Portal p;
        p.upload(UPLOAD_FILE_START);
        if (written) p.upload(UPLOAD_FILE_WRITE);
        p.authenticated = false;
        p.upload(UPLOAD_FILE_ABORTED);
        assert(Update.aborts == 1 && !p._httpUpdateActive);
        assert(!p._frisquetManager.r.busy() && !tasks);
        p.upload(UPLOAD_FILE_ABORTED);
        p._srv.finish();
        assert(p._srv.status == 401 && Update.aborts == 1);
        assert(Update.writes == unsigned(written) && !Update.ends && !tasks);
    }
    // Un POST sans fichier ne doit ni redémarrer ni perturber une observation.
    {
        Portal p;
        reserve(p._frisquetManager.r);
        p._srv.finish();
        assert(p._srv.status == 500 && !tasks);
        assert(!Update.begins && !Update.writes && !Update.ends && !Update.aborts);
        assert(p._frisquetManager.r.ownsObservation(8));
    }
    for (unsigned failure = 0; failure < 4; ++failure) {
        Portal p;
        Update.beginOk = failure != 0; Update.writeOk = failure != 1; Update.endOk = failure != 2;
        p.upload(UPLOAD_FILE_START); p.upload(UPLOAD_FILE_WRITE);
        p.upload(failure == 3 ? UPLOAD_FILE_ABORTED : UPLOAD_FILE_END);
        p._srv.finish();
        assert(p._srv.status == 500 && !p._frisquetManager.r.busy() && !tasks && Update.aborts == 1);
    }
    for (bool success : {false, true}) {
        Portal p;
        taskOk = success;
        p.upload(UPLOAD_FILE_START); p.upload(UPLOAD_FILE_WRITE); p.upload(UPLOAD_FILE_END);
        p._srv.finish();
        assert(p._srv.status == (success ? 200 : 500) && tasks == 1);
        assert(p._frisquetManager.r.busy() == success);
        p._frisquetManager.r.suspend(R::Pause::Reboot, false);
        assert(!p._frisquetManager.r.busy());
    }
    {
        Portal p;
        p.upload(UPLOAD_FILE_START);
        p._frisquetManager.r.suspend(R::Pause::ArduinoOta, true);
        p.upload(UPLOAD_FILE_ABORTED);
        assert(p._frisquetManager.r.busy());
        taskOk = false;
        assert(!p.scheduleReboot(800) && p._frisquetManager.r.busy());
        p._frisquetManager.r.suspend(R::Pause::ArduinoOta, false);
        assert(!p._frisquetManager.r.busy());
    }
    puts("OK : authentification OTA, callbacks HTTP/Arduino et pauses jusqu'au redémarrage");
}
