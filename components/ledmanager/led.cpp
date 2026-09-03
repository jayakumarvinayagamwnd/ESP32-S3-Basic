#include "led.hpp"

#include "driver/rmt_tx.h"
#include "esp_err.h"

namespace {

constexpr uint32_t RMT_RESOLUTION_HZ = 10 * 1000 * 1000;

}  // namespace

namespace ledmanager {

Led::Led(int gpio) {
    rmt_tx_channel_config_t channel_config = {};
    channel_config.gpio_num = static_cast<gpio_num_t>(gpio);
    channel_config.clk_src = RMT_CLK_SRC_DEFAULT;
    channel_config.resolution_hz = RMT_RESOLUTION_HZ;
    channel_config.mem_block_symbols = 64;
    channel_config.trans_queue_depth = 4;
    channel_config.flags.invert_out = false;
    channel_config.flags.with_dma = false;
    rmt_channel_handle_t channelHandle = nullptr;
    ESP_ERROR_CHECK(rmt_new_tx_channel(&channel_config, &channelHandle));
    channel_ = channelHandle;

    rmt_bytes_encoder_config_t encoder_config = {};
    encoder_config.bit0.level0 = 1;
    encoder_config.bit0.duration0 = 4;
    encoder_config.bit0.level1 = 0;
    encoder_config.bit0.duration1 = 9;
    encoder_config.bit1.level0 = 1;
    encoder_config.bit1.duration0 = 8;
    encoder_config.bit1.level1 = 0;
    encoder_config.bit1.duration1 = 5;
    encoder_config.flags.msb_first = true;
    rmt_encoder_handle_t encoderHandle = nullptr;
    ESP_ERROR_CHECK(rmt_new_bytes_encoder(&encoder_config, &encoderHandle));
    encoder_ = encoderHandle;
    ESP_ERROR_CHECK(rmt_enable(static_cast<rmt_channel_handle_t>(channel_)));
}

Led::~Led() {
    auto channelHandle = static_cast<rmt_channel_handle_t>(channel_);
    auto encoderHandle = static_cast<rmt_encoder_handle_t>(encoder_);

    if (channel_ != nullptr) {
        ESP_ERROR_CHECK(rmt_disable(channelHandle));
    }
    if (encoder_ != nullptr) {
        ESP_ERROR_CHECK(rmt_del_encoder(encoderHandle));
    }
    if (channel_ != nullptr) {
        ESP_ERROR_CHECK(rmt_del_channel(channelHandle));
    }
}

void Led::on(uint8_t red, uint8_t green, uint8_t blue) {
    // WS2812 LEDs use GRB byte order.
    const uint8_t colour[] = {green, red, blue};
    rmt_transmit_config_t transmit_config = {};
    transmit_config.loop_count = 0;

    ESP_ERROR_CHECK(rmt_transmit(static_cast<rmt_channel_handle_t>(channel_),
                                 static_cast<rmt_encoder_handle_t>(encoder_),
                                 colour, sizeof(colour),
                                 &transmit_config));
    ESP_ERROR_CHECK(rmt_tx_wait_all_done(static_cast<rmt_channel_handle_t>(channel_), -1));
}

void Led::off() {
    on(0, 0, 0);
}

}  // namespace ledmanager
