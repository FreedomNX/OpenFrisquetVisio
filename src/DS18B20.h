#pragma once

#include <Board.h>
#include <OneWire.h>
#include <DallasTemperature.h>


class DS18B20 {

    public: 
        DS18B20();
        bool init(uint8_t pin);
        bool getTemperature(float& temperature);
        bool isReady();
        void setOffset(uint8_t offset);

    private:
        OneWire oneWire;
        DallasTemperature ds18b20;
        DeviceAddress dsAddr;
        bool _isReady = false;
        uint8_t offset = 0;
};