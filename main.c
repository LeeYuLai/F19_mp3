#include "FreeRTOS.h"
#include "board_io.h"
#include "cli_handlers.h"
#include "common_macros.h"
#include "delay.h"
#include "display.h"
#include "ff.h"
#include "gpio.h"
#include "gpio_isr.h"
#include "mp3_decoder.h"
#include "queue.h"
#include "semphr.h"
#include "sj2_cli.h"
#include "string.h"
#include "task.h"
#include "uart.h"
#include "uart_printf.h"

#include "ff.h"

static char playlist[64][32];
static uint8_t counter = 0;
static void display_task(void *params);

static void blink_task(void *params);
static void uart_task(void *params);
typedef char songname[32];

xQueueHandle Q_songname = NULL;
xQueueHandle Q_songname_buttom = NULL;
xQueueHandle Q_songdata;

static gpio_s ex_sw, ex_led;

static bool play = true;
static bool song_change = false;
static bool volume_change = false;
uint8_t volume, counter_display, played;
uint16_t value;
static bool paused;
static bool select;

const char *get_filename_ext(const char *filename) {

  const char *dot = strrchr(filename, '.');

  if (!dot || dot == filename)

    return "";

  return dot + 1;
}

static void display_task(void *p) {

  DIR directory;

  FILINFO fno;

  lcd_init();

  vTaskDelay(5);

  FRESULT o_result = f_opendir(&directory, "");

  vTaskDelay(1000);

  if (FR_OK == o_result) {

    FRESULT f_result = f_readdir(&directory, &fno);

    while ('\0' != fno.fname[0]) {

      f_result = f_readdir(&directory, &fno);

      if (FR_OK == f_result)

        if (!strcmp(get_filename_ext(fno.fname), "mp3")) {

          if (fno.fname[0] != '.' && fno.fname[1] != '_') {

            strcpy(playlist[counter], fno.fname);

            counter++;
          }
        }
    }
  }

  FRESULT c_result = f_closedir(&directory);

  vTaskDelay(1);

  // print_str(playlist[0]);

  while (1) {
    clear();
    vTaskDelay(1);
    home();
    vTaskDelay(1);
    print_str(playlist[counter_display]);
    // xQueueSend(Q_songname_buttom, &playlist[counter_display], portMAX_DELAY);
    move_cursor(1);
    for (int i = 0; i < played; i++) {
      print_str("*");
    }
    display();
    vTaskDelay(1000); // todo replace with queue for new things to display
    if (select) {
      xQueueSend(Q_songname_buttom, playlist[counter_display], portMAX_DELAY);
      vTaskDelay(100);
      select = false;
      // paused = false;
    }
  }
}

void play_pause_isr(void) { play = !play; }

void select_isr(void) { select = true; }

void vol_up_isr(void) {

  volume_change = true;

  if (volume - 10 < 16) {
    volume = 16;
  } else {
    volume -= 10;
  }
}

void vol_down_isr(void) {

  volume_change = true;

  if (volume + 10 > 116) {
    volume = 116;
  } else {
    volume += 10;
  }
}

void next_isr(void) {

  song_change = true;

  if (++counter_display >= counter)

    counter_display = 0;
}

void prev_isr(void) {

  song_change = true;

  if (--counter_display > counter)

    counter_display = counter - 1;
}

void mp3_reader_task(void *p) {
  songname name;
  UINT byte_read = 0;

  while (1) {
    vTaskDelay(1000);
    xQueueReceive(Q_songname_buttom, &name[0], portMAX_DELAY);

    FIL file;
    FRESULT o_result = f_open(&file, name, FA_READ);
    if (FR_OK == o_result) {
      uint32_t size;
      uint32_t chunck = 0;
      played = 0;
      size = f_size(&file);
      chunck = size / 16;

      do {
        char read_buffer[512] = {0};
        FRESULT r_result;

        if (size > 511) {
          r_result = f_read(&file, read_buffer, 512, &byte_read);
        } else {
          r_result = f_read(&file, read_buffer, size, &byte_read);
        }

        if (FR_OK == r_result) {

          xQueueSend(Q_songdata, &read_buffer[0], portMAX_DELAY);

          size = size - byte_read;

          played = 16 - (size / chunck);

        } else {
          printf("Failed to read the file, %s\n", name);
        }

        if (song_change) {
          song_change = false;
          break;
        }

        if (volume_change) {
          uint32_t clock_khz = 12;
          ssp2__initialize(clock_khz);
          volume_control(volume, volume);
          clock_khz = 3000;
          ssp2__initialize(clock_khz);
          volume_change = false;
        }

      } while (size > 0);
    } else {
      printf("Failed to open the file, %s\n", name);
    }

    f_close(&file);
    vTaskDelay(500);
  }
}

void spi_send_to_mp3_decoder();
void mp3_player_task(void *p) {
  char play_buffer[512];

  while (1) {
    xQueueReceive(Q_songdata, &play_buffer[0], portMAX_DELAY);
    send_data(play_buffer, 512);

    while (!play) {

      vTaskDelay(100);
    }
  }
}

int main(void) {
  Q_songname_buttom = xQueueCreate(1, sizeof(songname));
  Q_songdata = xQueueCreate(1, 512);

  volume = 56;
  paused = false;

  NVIC_EnableIRQ(GPIO_IRQn);
  gpio__attach_interrupt(2, 5, GPIO_INTR__RISING_EDGE, play_pause_isr);
  gpio__attach_interrupt(2, 6, GPIO_INTR__RISING_EDGE, select_isr);
  gpio__attach_interrupt(2, 7, GPIO_INTR__RISING_EDGE, prev_isr);
  gpio__attach_interrupt(2, 8, GPIO_INTR__RISING_EDGE, next_isr);
  gpio__attach_interrupt(2, 9, GPIO_INTR__RISING_EDGE, vol_up_isr);
  gpio__attach_interrupt(0, 16, GPIO_INTR__RISING_EDGE, vol_down_isr);

  mp3_initialization();

  xTaskCreate(display_task, "display", 512, NULL, 3, NULL);
  xTaskCreate(mp3_player_task, "player", 512, NULL, 2, NULL);
  xTaskCreate(mp3_reader_task, "reader", 512, NULL, 1, NULL);

  sj2_cli__init();

  puts("Starting RTOS");
  vTaskStartScheduler();

  return 0;
}
