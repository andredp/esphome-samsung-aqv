#include <cstdio>
#include <cstring>
#include "../components/samsung_aqv/protocol.h"
using namespace esphome::samsung_aqv;

int main(int argc, char *argv[]) {
  if (argc < 5) { fprintf(stderr, "Usage: encode_cli <mode> <fan> <swing> <temp>\n"); return 1; }

  const char *mode_s = argv[1];
  const char *fan_s = argv[2];
  const char *swing_s = argv[3];
  int temp = atoi(argv[4]);

  if (strcmp(mode_s, "off") == 0) {
    printf("%s\n", encode_off().c_str());
    return 0;
  }

  Mode mode;
  if (strcmp(mode_s, "cool") == 0) mode = MODE_COOL;
  else if (strcmp(mode_s, "heat") == 0) mode = MODE_HEAT;
  else if (strcmp(mode_s, "dry") == 0) mode = MODE_DRY;
  else if (strcmp(mode_s, "fan_only") == 0) mode = MODE_FAN_ONLY;
  else if (strcmp(mode_s, "heat_cool") == 0) mode = MODE_HEAT_COOL;
  else { fprintf(stderr, "Unknown mode: %s\n", mode_s); return 1; }

  Fan fan;
  if (strcmp(fan_s, "auto") == 0) fan = FAN_AUTO;
  else if (strcmp(fan_s, "quiet") == 0) fan = FAN_QUIET;
  else if (strcmp(fan_s, "low") == 0) fan = FAN_LOW;
  else if (strcmp(fan_s, "medium") == 0) fan = FAN_MEDIUM;
  else if (strcmp(fan_s, "high") == 0) fan = FAN_HIGH;
  else { fprintf(stderr, "Unknown fan: %s\n", fan_s); return 1; }

  Swing swing = (strcmp(swing_s, "on") == 0) ? SWING_ON : SWING_OFF;

  printf("%s\n", encode_on(temp, mode, fan, swing).c_str());
  return 0;
}
