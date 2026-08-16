/*
 * ESP32-S3 main: starts the SECD VM.
 * The VM bytecode is appended to the flash image after the app and located at
 * runtime by scanning past the image end (see components/secd/secd_start.cpp).
 */
#include <stdio.h>

extern "C" int secd_start(void);

extern "C" void app_main(void) {
    (void)secd_start();
}