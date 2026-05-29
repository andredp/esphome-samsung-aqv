#include <cstdio>
#include <string>
#include "../components/samsung_aqv/protocol.h"
using namespace samsung_aqv;

int main(int argc, char *argv[]) {
  if (argc < 2) { fprintf(stderr, "Usage: decode_cli <pronto_string>\n"); return 1; }

  // Concatenate all args (pronto may be split by shell)
  std::string pronto;
  for (int i = 1; i < argc; i++) {
    if (i > 1) pronto += ' ';
    pronto += argv[i];
  }

  auto d = decode_pronto(pronto);
  if (!d.valid) { printf("invalid\n"); return 1; }
  if (d.is_off) { printf("off\n"); return 0; }

  const char *mode_s[] = {"cool", "heat", "dry", "fan_only", "heat_cool"};
  const char *fan_s[] = {"auto", "quiet", "low", "medium", "high"};
  const char *swing_s[] = {"on", "off"};

  printf("%s %s %s %d\n", mode_s[d.mode], fan_s[d.fan], swing_s[d.swing], d.temp);
  return 0;
}
