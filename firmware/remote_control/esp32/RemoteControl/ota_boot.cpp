// Defer the Arduino core's default early OTA acceptance until setup/loop work.
extern "C" bool verifyRollbackLater(void) { return true; }
