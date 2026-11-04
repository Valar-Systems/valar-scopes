// Blipscope web renderer: STUBS for the 2.1" board's buzzer and IMU (src/board/board_s3_touch21.cpp drives them
// through a TCA9554 I2C expander the page does not have). Linked only into the s3-21 build; every other
// SKU's Board.h defines these inline. The buzzer stays silent and the IMU reports no reading, which is
// the firmware's own answer when the IMU read fails.
#include "Board.h"

#if defined(BLIPSCOPE_VARIANT_S3_21) || defined(BLIPSCOPE_VARIANT_S3_146)
void board::BuzzerChirp(uint16_t) {}
void board::BuzzerUpdate() {}
bool board::ImuRead(Imu&) { return false; }
void board::DisplayFlush(LGFX&) {}
#endif
