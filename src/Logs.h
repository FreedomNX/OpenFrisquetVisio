#pragma once

#include <Board.h>
#include <TimeLib.h>

class Logs {
public:
  static constexpr size_t kMaxLines = LOGS_MAX_LINES;
  static constexpr size_t kMaxLevelLen = 8;
  static constexpr size_t kMaxMessageLen = 192;
  static constexpr size_t kMaxFormattedLen = 256;

  explicit Logs(size_t maxLogSize = kMaxLines)
      : _capacity(maxLogSize <= kMaxLines ? maxLogSize : kMaxLines) {}

  struct Line {
    Line() : time(0) {
      level[0] = '\0';
      message[0] = '\0';
    }

    void set(const char* levelIn, const char* messageIn, time_t t);
    bool levelEquals(const char* other) const;
    void format(char* out, size_t outSize) const;

    char level[kMaxLevelLen];
    char message[kMaxMessageLen];
    time_t time;
  };

  void clear();

  // Ajouter un log avec un niveau
  void addLog(const char* level, const char* message);
  void addLog(const String& level, const String& message) {
    addLog(level.c_str(), message.c_str());
  }

  // Ajouter un log formaté
  void addLogf(const char* level, const char* fmt, ...);

  size_t getLogCount(const char* level = nullptr);

  size_t getLines(Line* out, size_t outCapacity, size_t limit = kMaxLines,
                  const char* level = nullptr);

private:
  struct BusyGuard {
    explicit BusyGuard(Logs& owner) : _owner(owner) {
      while (_owner._busy) {
        delay(1);
      }
      _owner._busy = true;
    }
    ~BusyGuard() { _owner._busy = false; }

    Logs& _owner;
  };

  static void copyTruncate(char* dest, size_t destSize, const char* src);

  size_t _capacity = 0;
  size_t _count = 0;
  size_t _head = 0;
  Line _entries[kMaxLines];
  volatile bool _busy = false;
};

extern Logs logs;

void debug(const String& message);
void debug(const char* fmt, ...);
void logRadio(bool rx, const byte* payload, size_t length);
void info(const String& message);
void info(const char* fmt, ...);
void error(const String& message);
void error(const char* fmt, ...);
void warning(const String& message);
void warrning(const char* fmt, ...);
