// Stripped down ST7789 graphics driver in C for Pimoroni Pic Display 2 (320x240)
// Derived from Pimoroni st7789.cpp graphics driver
//
#include "limits.h"
#include "hardware/spi.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include <math.h>
#include "fgraphics.h"

static int width;
static int height;

#define SWRESET 0x01
#define TEOFF 0x34
#define TEON 0x35
#define MADCTL 0x36
#define COLMOD 0x3A
#define GCTRL 0xB7
#define VCOMS 0xBB
#define LCMCTRL 0xC0
#define VDVVRHEN 0xC2
#define VRHS 0xC3
#define VDVS 0xC4
#define FRCTRL2 0xC6
#define PWCTRL1 0xD0
#define PORCTRL 0xB2
#define GMCTRP1 0xE0
#define GMCTRN1 0xE1
#define INVOFF 0x20
#define SLPOUT 0x11
#define DISPON 0x29
#define GAMSET 0x26
#define DISPOFF 0x28
#define RAMWR 0x2C
#define INVON 0x21
#define CASET 0x2A
#define RASET 0x2B
#define PWMFRSEL 0xCC



 #define ROW_ORDER    0b10000000
 #define   COL_ORDER    0b01000000
 #define   SWAP_XY      0b00100000  // AKA "MV"
 #define   SCAN_ORDER   0b00010000
 #define   RGB_BGR      0b00001000
 #define   HORIZ_ORDER  0b00000100


#define SPI_DEFAULT_INSTANCE spi0
#define SPI_BG_FRONT_CS 17
#define SPI_DEFAULT_SCK 18
#define SPI_DEFAULT_MOSI 19
#define PIN_UNUSED INT_MAX
#define SPI_DEFAULT_DC 16
#define SPI_BG_FRONT_PWM 20

//{PIMORONI_SPI_DEFAULT_INSTANCE, SPI_BG_FRONT_CS, SPI_DEFAULT_SCK, SPI_DEFAULT_MOSI, PIN_UNUSED, SPI_DEFAULT_DC, SPI_BG_FRONT_PWM};
// struct SPIPins
// {
//   spi_inst_t *spi; // PIMORONI_SPI_DEFAULT_INSTANCE
//   uint cs;         // SPI_BG_FRONT_CS
//   uint sck;        // SPI_DEFAULT_SCK
//   uint mosi;       // SPI_DEFAULT_MOSI
//   uint miso;       // PIN_UNUSED
//   uint dc;         // SPI_DEFAULT_DC
//   uint bl;         // SPI_BG_FRONT_PWM
// };

static uint cs = SPI_BG_FRONT_CS;
static uint dc = SPI_DEFAULT_DC;
static uint wr_sck = SPI_DEFAULT_SCK;
static uint bl = SPI_BG_FRONT_PWM;
static uint d0 = SPI_DEFAULT_MOSI;
static spi_inst_t *spi = SPI_DEFAULT_INSTANCE;
static bool rond = false;
static int rotation = ROTATE_0; // 90 - 180 - 270
static uint st_dma;
static const uint32_t SPI_BAUD = 62500000;

static uint8_t madctl;
static  uint16_t caset[2] = {0, 0};
static  uint16_t raset[2] = {0, 0};

static void command(uint8_t command, size_t len, const char *data)
{
  gpio_put(dc, 0); // command mode
  gpio_put(cs, 0);
  spi_write_blocking(spi, &command, 1);
  if (data)
  {
    gpio_put(dc, 1); // data mode
    spi_write_blocking(spi, (const uint8_t *)data, len);
  }
  gpio_put(cs, 1);
}



static void configure_display(int rotate) {

    bool rotate180 = rotate == ROTATE_180 || rotate == ROTATE_90;

    if(rotate == ROTATE_90 || rotate == ROTATE_270) {
      int tmp = width;
      width = height;
      height = tmp;    
    }

if(width == 240 && height == 240) {
      int row_offset = rond ? 40 : 80;
      int col_offset = 0;
    
      switch(rotate) {
        case ROTATE_90:
          if (!rond) row_offset = 0;
          caset[0] = row_offset;
          caset[1] = width + row_offset - 1;
          raset[0] = col_offset;
          raset[1] = width + col_offset - 1;

          madctl = HORIZ_ORDER | COL_ORDER | SWAP_XY;
          break;
        case ROTATE_180:
          caset[0] = col_offset;
          caset[1] = width + col_offset - 1;
          raset[0] = row_offset;
          raset[1] = width + row_offset - 1;

          madctl = HORIZ_ORDER | COL_ORDER | ROW_ORDER;
          break;
        case ROTATE_270:
          caset[0] = row_offset;
          caset[1] = width + row_offset - 1;
          raset[0] = col_offset;
          raset[1] = width + col_offset - 1;

          madctl = ROW_ORDER | SWAP_XY;
          break;
        default: // ROTATE_0 (and for any smart-alec who tries to rotate 45 degrees or something...)
          if (!rond) row_offset = 0;
          caset[0] = col_offset;
          caset[1] = width + col_offset - 1;
          raset[0] = row_offset;
          raset[1] = width + row_offset - 1;

          madctl = HORIZ_ORDER;
          break;
      }
    }
    // Pico Display 2.0
    if(width == 320 && height == 240) {
      caset[0] = 0;
      caset[1] = 319;
      raset[0] = 0;
      raset[1] = 239;
      madctl = rotate180 ? ROW_ORDER : COL_ORDER;
      madctl |= SWAP_XY | SCAN_ORDER;
    }

    // Pico Display 2.0 at 90 degree rotation
    if(width == 240 && height == 320) {
      caset[0] = 0;
      caset[1] = 239;
      raset[0] = 0;
      raset[1] = 319;
      madctl = rotate180 ? (COL_ORDER | ROW_ORDER) : 0;
    }

    // Byte swap the 16bit rows/cols values
    caset[0] = __builtin_bswap16(caset[0]);
    caset[1] = __builtin_bswap16(caset[1]);
    raset[0] = __builtin_bswap16(raset[0]);
    raset[1] = __builtin_bswap16(raset[1]);

    command(CASET,  4, (char *)caset);
    command(RASET,  4, (char *)raset);
    command(MADCTL, 1, (char *)&madctl);
}

void fupdate(const char *graphicsbuffer) {
    uint8_t cmd = RAMWR;
    command(cmd, width * height * sizeof(uint16_t), graphicsbuffer); 
}
void fset_backlight(uint8_t brightness) {
    // gamma correct the provided 0-255 brightness value onto a
    // 0-65535 range for the pwm counter
    float gamma = 2.8;
    uint16_t value = (uint16_t)(pow((float)(brightness) / 255.0f, gamma) * 65535.0f + 0.5f);
    pwm_set_gpio_level(bl, value);
  }
void __not_in_flash_func(fstartframe)() {

  uint8_t cmd = RAMWR;
  gpio_put(dc, 0); // command mode
  gpio_put(cs, 0);
  spi_write_blocking(spi, &cmd, 1);
  gpio_put(dc, 1); // data mode
}

void __not_in_flash_func(fendframe)() {
   //dma_channel_wait_for_finish_blocking(st_dma);
   gpio_put(cs, 1);
}

void __not_in_flash_func(fwritescanline)(size_t len, const char *line) {
  spi_write_blocking(spi, (const uint8_t *)line, len);
}

void finitgraphics(int withrotation, int w, int h)
{
  rotation = withrotation;
  width = w;
  height = h;
  // configure spi interface and pins
  spi_init(spi, SPI_BAUD);
  // spi_set_format(spi, 16,SPI_CPOL_0,SPI_CPHA_0,SPI_MSB_FIRST);
  gpio_set_function(wr_sck, GPIO_FUNC_SPI);
  gpio_set_function(d0, GPIO_FUNC_SPI);

  st_dma = dma_claim_unused_channel(true);
  dma_channel_config config = dma_channel_get_default_config(st_dma);
  channel_config_set_transfer_data_size(&config, DMA_SIZE_8);
  channel_config_set_bswap(&config, false);
  channel_config_set_dreq(&config, spi_get_dreq(spi, true));
  dma_channel_configure(st_dma, &config, &spi_get_hw(spi)->dr, NULL, 0, false);

  gpio_set_function(dc, GPIO_FUNC_SIO);
  gpio_set_dir(dc, GPIO_OUT);

  gpio_set_function(cs, GPIO_FUNC_SIO);
  gpio_set_dir(cs, GPIO_OUT);

  // if a backlight pin is provided then set it up for
  // pwm control
  if (bl != PIN_UNUSED)
  {
    pwm_config cfg = pwm_get_default_config();
    pwm_set_wrap(pwm_gpio_to_slice_num(bl), 65535);
    pwm_init(pwm_gpio_to_slice_num(bl), &cfg, true);
    gpio_set_function(bl, GPIO_FUNC_PWM);
    fset_backlight(0); // Turn backlight off initially to avoid nasty surprises
  }

  command(SWRESET, 0 , NULL);

  sleep_ms(150);

  // Common init
  command(TEON, 0, NULL);              // enable frame sync signal if used
  command(COLMOD, 1, "\x05"); // 16 bits per pixel

  command(PORCTRL, 5, "\x0c\x0c\x00\x33\x33");
  command(LCMCTRL, 1, "\x2c");
  command(VDVVRHEN, 1, "\x01");
  command(VRHS, 1, "\x12");
  command(VDVS, 1, "\x20");
  command(PWCTRL1, 2, "\xa4\xa1");
  command(FRCTRL2, 1, "\x0f");

  if (width == 240 && height == 240)
  {
    command(GCTRL, 1, "\x14");
    command(VCOMS, 1, "\x37");
    command(GMCTRP1, 14, "\xD0\x04\x0D\x11\x13\x2B\x3F\x54\x4C\x18\x0D\x0B\x1F\x23");
    command(GMCTRN1, 14, "\xD0\x04\x0C\x11\x13\x2C\x3F\x44\x51\x2F\x1F\x1F\x20\x23");
  }

  if (width == 320 && height == 240)
  {
    command(GCTRL, 1, "\x35");
    command(VCOMS, 1, "\x1f");
    command(0xd6, 1, "\xa1"); // ???
    command(GMCTRP1, 14, "\xD0\x08\x11\x08\x0C\x15\x39\x33\x50\x36\x13\x14\x29\x2D");
    command(GMCTRN1, 14, "\xD0\x08\x10\x08\x06\x06\x39\x44\x51\x0B\x16\x14\x2F\x31");
  }

  command(INVON, 0, NULL);  // set inversion mode
  command(SLPOUT, 0, NULL); // leave sleep mode
  command(DISPON, 0, NULL); // turn display on

  sleep_ms(100);

  configure_display(rotation);

  if (bl != PIN_UNUSED)
  {
    // update(); // Send the new buffer to the display to clear any previous content
    sleep_ms(50);       // Wait for the update to apply
    fset_backlight(255); // Turn backlight on now surprises have passed
  }
}