/*
 * ESP32-C3 main: starts the SECD VM.
 * The VM bytecode is merged into the app image (see components/secd/secd_bytecode.cpp).
 */
#include <stdio.h>

extern "C" int secd_start(void);

extern "C" void app_main(void) {
    (void)secd_start();
}