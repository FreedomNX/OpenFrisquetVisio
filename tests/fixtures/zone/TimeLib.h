#pragma once
#include <ctime>

extern time_t heureChaudiere;
extern bool heureConnue;
enum timeStatus_t { timeNotSet, timeSet };
inline timeStatus_t timeStatus() { return heureConnue ? timeSet : timeNotSet; }
inline time_t now() { return heureChaudiere; }
inline int year(time_t t) { return gmtime(&t)->tm_year + 1900; }
inline int month(time_t t) { return gmtime(&t)->tm_mon + 1; }
inline int day(time_t t) { return gmtime(&t)->tm_mday; }
inline int weekday(time_t t) { return gmtime(&t)->tm_wday + 1; }
inline int hour(time_t t) { return gmtime(&t)->tm_hour; }
inline int minute(time_t t) { return gmtime(&t)->tm_min; }
inline int second(time_t t) { return gmtime(&t)->tm_sec; }
void setTime(int, int, int, int, int, int);
