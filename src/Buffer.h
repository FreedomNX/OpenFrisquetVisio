#pragma once

#include <Board.h>

class Buffer {
    protected:
        Buffer(byte* buff, size_t length = 0) : _buff(buff), _length(length) {}

        uint32_t _cursor = 0;
        byte* _buff;
        size_t _length = 0;

    public:
        size_t getLength() { return _length; }
        size_t getCursor() { return _cursor; }
};

class WriteBuffer : public Buffer {
    public:
        WriteBuffer(byte* buff) : Buffer(buff) {}

        void putBytes(byte* data, size_t length) { 
            memcpy(&_buff[_cursor], data, length);
            _cursor += length;
            _length += length;
        };

        void putByte(byte data) { putBytes(&data, sizeof(data)); }
        void putFloat(float data) { putBytes((byte*)&data, sizeof(data)); }
        void putUInt8(uint8_t data) { putBytes((byte*)&data, sizeof(data)); }
        void putUInt16(uint16_t data) { putBytes((byte*)&data, sizeof(data)); }
};

class ReadBuffer : public Buffer {
    public: 
        ReadBuffer(byte* buff, size_t length) : Buffer(buff, length) {}

        void getBytes(byte* data, size_t length) { 
            if(length > remainingLength()) {
                length = remainingLength();
            }
            memcpy(data, &_buff[_cursor], length);
            _cursor += length;
        };

        byte* getBytes(size_t length) { 
            if(length > remainingLength()) {
                length = remainingLength();
            }

            byte* addr = &_buff[_cursor];
            _cursor += length;
            
            return addr;
        };

        size_t remainingLength () {
            return _length - _cursor;
        }

        byte getByte() { 
            byte data;
            getBytes(&data, sizeof(data));
            return data; 
        }
        float getFloat() { 
            float data;
            getBytes((byte*)&data, sizeof(data));
            return data; 
        }
        uint8_t getUInt8() { 
            uint8_t data;
            getBytes((byte*)&data, sizeof(data));
            return data; 
        }
        uint16_t getUInt16() { 
            uint16_t data;
            getBytes((byte*)&data, sizeof(data));
            return data; 
        }
};