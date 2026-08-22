#include "hal/hal.h"
#include <string.h>
#include <stdlib.h>
extern "C" {
void hal_init(void) {}
void *hal_malloc(size_t s) { return malloc(s); }
void *hal_realloc(void *p, size_t s) { return realloc(p, s); }
void hal_free(void *p) { free(p); }
uint32_t hal_millis(void) { return 0; }
void hal_delay(uint32_t) {}
void hal_sleep(uint32_t) {}
int hal_gpio_init(uint8_t, uint8_t) { return 0; }
int hal_gpio_write(uint8_t, uint8_t) { return 0; }
int hal_gpio_read(uint8_t) { return 0; }
void hal_serial_init(uint32_t) {}
void hal_serial_write(uint8_t) {}
void hal_serial_write_bytes(const uint8_t *, size_t) {}
uint8_t hal_serial_read(void) { return 0; }
int hal_serial_available(void) { return 0; }
void hal_print(const char *) {}
void hal_println(const char *) {}
void hal_print_int(int32_t) {}
void hal_wave_play(int, int, const uint16_t *, int) {}
int hal_i2c_init(uint8_t, uint8_t, uint32_t) { return -1; }
int hal_i2c_write(uint8_t, uint8_t, const uint8_t *, size_t) { return -1; }
int hal_i2c_read(uint8_t, uint8_t, uint8_t *, size_t) { return -1; }
int hal_i2c_write_read(uint8_t, uint8_t, const uint8_t *, size_t, uint8_t *, size_t) { return -1; }
uint32_t hal_flash_size(void) { return 0; }
int hal_flash_read(uint32_t a, uint8_t *b, size_t l) { memset(b,0,l); (void)a; return 0; }
int hal_flash_write(uint32_t, const uint8_t *, size_t) { return 0; }
int hal_flash_erase(uint32_t, size_t) { return 0; }
void hal_reset(void) {}
}
