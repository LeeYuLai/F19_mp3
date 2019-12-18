#pragma once

#include <stdint.h>
#include <stdlib.h>

#include "board_io.h"
#include "gpio.h"
#include "lpc40xx.h"
#include "ssp2.h"

// VS1053 R/W Opcode
#define VS1053_SCI_READ 0x03
#define VS1053_SCI_WRITE 0x02

// VS1053 Registors
#define VS1053_REG_MODE 0x00
#define VS1053_REG_STATUS 0x01
#define VS1053_REG_BASS 0x02
#define VS1053_REG_CLOCKF 0x03
#define VS1053_REG_VOLUME 0x0B

uint16_t volume_default, volume_counter, new_volume;

static gpio_s DREQ, CS, SDCS, XDCS, RESET;

void mp3_initialization() {

  volume_counter = 10;

  uint32_t max_clock_khz = 3000;
  ssp2__initialize(max_clock_khz);

  gpio__construct_with_function(1, 0, 4); // SCK
  gpio__construct_with_function(1, 1, 4); // MOSI
  gpio__construct_with_function(1, 4, 4); // MISO

  DREQ = gpio__construct_as_input(2, 0); // DREQ
  CS = gpio__construct_as_output(2, 1);  // CS
  XDCS = gpio__construct_as_output(2, 2);  // XDCS
  RESET = gpio__construct_as_output(2, 4); // RESET


  gpio__set(CS);
  gpio__set(XDCS);


  gpio__set(RESET);
  gpio__reset(RESET);
  gpio__set(RESET);


  volume_control(56, 56);
  decoder_w_reg(VS1053_REG_BASS, 0x7A00);
  decoder_w_reg(VS1053_REG_CLOCKF, 0XE000);
}

void send_data(uint8_t data[], int size) {

  int counter = 0;

  gpio__reset(XDCS);
  while (counter < size) {

    while (!gpio__get(DREQ))
      ;
    for (int i = 0; i < 32; i++) {
      ssp2__exchange_byte(data[counter++]);
    }

  }

  gpio__set(XDCS);
  while (!gpio__get(DREQ))
    ;

}

void decoder_w_reg(uint8_t address, uint16_t data) {
  uint8_t buffer[2] = {0};
  buffer[0] = data;
  buffer[1] = data >> 8;
  while (!gpio__get(DREQ))
    ;
  gpio__reset(CS);
  ssp2__exchange_byte(VS1053_SCI_WRITE);
  ssp2__exchange_byte(address);
  ssp2__exchange_byte(buffer[1]);
  ssp2__exchange_byte(buffer[0]);
  gpio__set(CS);
  while (!gpio__get(DREQ))
    ;
}

uint16_t decoder_r_reg(uint8_t address) {
  uint16_t buffer = 0;
  while (!gpio__get(DREQ))
    ;
  gpio__reset(CS);
  ssp2__exchange_byte(VS1053_SCI_READ);
  ssp2__exchange_byte(address);
  while (!gpio__get(DREQ))
    ;

  buffer = ssp2__exchange_byte(0xAA);
  buffer = buffer << 8;
  while (!gpio__get(DREQ))
    ;
  buffer = buffer + ssp2__exchange_byte(0xAA);
  while (!gpio__get(DREQ))
    ;
  gpio__set(CS);

  return buffer;
}

void volume_control(uint8_t l_vol, uint8_t r_vol) {
  uint16_t new_volume;
  new_volume = l_vol;
  new_volume <<= 8;
  new_volume |= r_vol;
  decoder_w_reg(VS1053_REG_VOLUME, new_volume);
}
