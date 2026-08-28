#pragma once

#include <cstdint>

#include "driver/rmt_tx.h"

namespace ledmanager {

class Led {
public:
    explicit Led(gpio_num_t gpio = GPIO_NUM_48);
    ~Led();

    Led(const Led&) = delete;
    Led& operator=(const Led&) = delete;

    void on(uint8_t red, uint8_t green, uint8_t blue);
    void off();

private:
    rmt_channel_handle_t channel_ = nullptr;
    rmt_encoder_handle_t encoder_ = nullptr;
};

}  // namespace ledmanager
