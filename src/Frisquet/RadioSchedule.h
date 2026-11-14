#pragma once
#include <TimeLib.h>
#include <Arduino.h>

// Créneaux basés sur l'heure chaudière, ou sur le démarrage avant synchronisation.
// Une tentative par créneau, même en cas d'échec ; pas de rattrapage en rafale.
class RadioSchedule {
public:
    static uint32_t clockSeconds() {
        return timeStatus() == timeNotSet ? millis() / 1000UL : ::now();
    }

    static bool quietWindow(uint32_t seconds) {
        const uint32_t phase = seconds % 600;
        // Protéger les satellites (:00:30, :00:54, :01:18) avec une marge.
        return phase < 150 || phase >= 570;
    }

    bool due(uint32_t period, uint32_t offset) {
        return dueAt(clockSeconds(), millis(), period, offset);
    }

    bool dueAt(uint32_t seconds, uint32_t uptime, uint32_t period, uint32_t offset) {
        if (quietWindow(seconds)) return false;
        return dueWindowAt(seconds, uptime, period, offset, 10);
    }

    static bool satelliteWindow(uint32_t seconds, uint8_t zone) {
        if (zone < 1 || zone > 3) return false;
        const uint32_t offset = 30 + (zone - 1) * 24;
        const uint32_t phase = seconds % 600;
        return phase >= offset && phase < offset + 24;
    }

    bool dueSatellite(uint8_t zone) {
        return dueSatelliteAt(clockSeconds(), millis(), zone);
    }

    bool dueSatelliteAt(uint32_t seconds, uint32_t uptime, uint8_t zone) {
        if (!satelliteWindow(seconds, zone)) return false;
        return dueWindowAt(seconds, uptime, 600, 30 + (zone - 1) * 24, 24);
    }

private:
    bool dueWindowAt(uint32_t seconds, uint32_t uptime, uint32_t period, uint32_t offset, uint32_t width) {
        if (period == 0 || offset >= period) return false;
        const uint32_t phase = seconds % period;
        if (phase < offset || phase - offset >= width) return false;
        // Empêcher une deuxième tentative dans le créneau et les doublons lors
        // d'une correction de l'heure. La soustraction résiste au wrap de millis().
        if (_attempted && (seconds / period == _lastCycle ||
            uptime - _lastAttempt < (period - width) * 1000UL)) return false;
        _attempted = true;
        _lastAttempt = uptime;
        _lastCycle = seconds / period;
        return true;
    }

    bool _attempted = false;
    uint32_t _lastAttempt = 0;
    uint32_t _lastCycle = 0;
};
