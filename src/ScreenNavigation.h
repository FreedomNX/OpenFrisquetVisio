#pragma once
#include <stdint.h>

// Navigation sans attente bloquante. Les intervalles restent valides au passage de millis() par zéro.
class ScreenNavigation {
public:
    enum class Action { None, Next, Home, ToggleCycle, Wake };
    void begin(uint32_t now, bool pressed, bool cycle) {
        _raw = _pressed = pressed;
        _ignoreRelease = pressed; // PRG maintenu au démarrage n'est pas une commande.
        _changed = _down = _activity = _pageSince = now;
        _cycle = cycle;
    }
    Action poll(uint32_t now, bool rawPressed) {
        if (rawPressed != _raw) { _raw = rawPressed; _changed = now; }
        if (_raw != _pressed && now - _changed >= 30) {
            _pressed = _raw;
            _activity = _pageSince = now;
            if (_pressed) {
                _down = now;
                _long = false;
                _doublePress = _click && now - _released <= 350;
                _wakePress = !_awake;
                _awake = true;
                _activity = _pageSince = now;
                if (_wakePress) { _click = false; return Action::Wake; }
            } else if (_ignoreRelease) {
                _ignoreRelease = false;
            } else if (!_long && !_wakePress) {
                _activity = _pageSince = now;
                if (_doublePress) {
                    _click = false;
                    return Action::Home;
                }
                _click = true;
                _released = now;
            }
        }
        if (_raw && _pressed && !_ignoreRelease && !_wakePress && !_long && now - _down >= 2000) {
            _long = true;
            _click = false;
            _cycle = !_cycle;
            _activity = _pageSince = now;
            return Action::ToggleCycle;
        }
        if (_click && !_pressed && now - _released > 350) {
            _click = false;
            return Action::Next;
        }
        return Action::None;
    }
    bool shouldSleep(uint32_t now, uint16_t seconds) const {
        return _awake && !_pressed && !_click && seconds && now - _activity >= uint32_t(seconds) * 1000;
    }
    bool cycleDue(uint32_t now) const {
        return _awake && _cycle && !_pressed && !_click && now - _pageSince >= 8000;
    }
    void pageChanged(uint32_t now) { _pageSince = now; }
    void sleep() { _awake = false; _click = false; }
    bool awake() const { return _awake; }
    bool cycling() const { return _cycle; }
private:
    bool _raw = false, _pressed = false, _long = false, _click = false;
    bool _ignoreRelease = false, _wakePress = false, _doublePress = false, _awake = true, _cycle = false;
    uint32_t _changed = 0, _down = 0, _released = 0, _activity = 0, _pageSince = 0;
};
