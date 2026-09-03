#pragma once

#include <cstdint>

namespace ledmanager {

class Led {
public:
    explicit Led(int gpio = 48);
    ~Led();

    Led(const Led&) = delete;
    Led& operator=(const Led&) = delete;

    void on(uint8_t red, uint8_t green, uint8_t blue);
    void off();

private:
    void* channel_ = nullptr;
    void* encoder_ = nullptr;
};

}  // namespace ledmanager
