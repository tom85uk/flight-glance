#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "config.h"

/** Round GC9A01 on VSPI (GPIO 18/23) with its own CS/DC/RST. */
class LGFX : public lgfx::LGFX_Device {
  lgfx::Bus_SPI _bus;
  lgfx::Panel_GC9A01 _panel;

public:
  LGFX() {
    {
      auto cfg = _bus.config();
      cfg.spi_host = SPI3_HOST;  // VSPI: SCK=18 MOSI=23
      cfg.freq_write = config::kDisplaySpiWriteHz;
      cfg.pin_sclk = static_cast<int>(config::kDisplayPinSclk);
      cfg.pin_mosi = static_cast<int>(config::kDisplayPinMosi);
      cfg.pin_miso = -1;
      cfg.pin_dc = static_cast<int>(config::kDisplayPinDc);
      cfg.dma_channel = 0;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs = static_cast<int>(config::kDisplayPinCs);
      cfg.pin_rst = static_cast<int>(config::kDisplayPinRst);
      cfg.invert = config::kDisplayInvert;
      cfg.rgb_order = config::kDisplayRgbOrder;
      cfg.bus_shared = false;
      _panel.config(cfg);
    }
    setPanel(&_panel);
  }
};

/** 1.44" ST7735S side card on HSPI (GPIO 25/26), separate from the radar. */
class LGFX_Side : public lgfx::LGFX_Device {
  lgfx::Bus_SPI _bus;
  lgfx::Panel_ST7735S _panel;

public:
  LGFX_Side() {
    {
      auto cfg = _bus.config();
      cfg.spi_host = SPI2_HOST;  // HSPI
      cfg.freq_write = config::kSideSpiWriteHz;
      cfg.pin_sclk = static_cast<int>(config::kSidePinSclk);
      cfg.pin_mosi = static_cast<int>(config::kSidePinMosi);
      cfg.pin_miso = -1;
      cfg.pin_dc = static_cast<int>(config::kSidePinDc);
      cfg.dma_channel = 0;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs = static_cast<int>(config::kSidePinCs);
      cfg.pin_rst = static_cast<int>(config::kSidePinRst);
      cfg.memory_width = 132;
      cfg.memory_height = 132;
      cfg.panel_width = config::kSideWidth;
      cfg.panel_height = config::kSideHeight;
      cfg.offset_x = config::kSideOffsetX;
      cfg.offset_y = config::kSideOffsetY;
      cfg.invert = false;
      cfg.rgb_order = false;
      cfg.bus_shared = false;
      _panel.config(cfg);
    }
    setPanel(&_panel);
  }
};
