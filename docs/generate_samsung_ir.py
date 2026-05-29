#!/usr/bin/env python3
# DEPRECATED: Replaced by esphome-samsung-aqv C++ component (github.com/andredp/esphome-samsung-aqv).
# Known bug: quiet fan flag is at bit 41 here but should be bit 45 (fixed in C++ version).
# Kept for reference only — do not use to regenerate the JSON.
"""
Samsung AQV18NSCN IR Code Generator for SmartIR (ESPHome controller)
Reverse-engineered from ARH-466 remote captures on AQV18NSCN (Living Room).
Compatible with entire Samsung AQV family including AQV09NSAX (Main Bedroom).
"""
import json

MARK, SPACE_0, SPACE_1 = 490, 590, 1550
HEADER_MARK, HEADER_SPACE = 3000, 9000
INTER_BURST, TAIL_SPACE = 2500, 100000

# Pronto hex constants (from captured remote - Samsung AQV18NSCN)
P_FREQ = 0x006D
P_HEADER_MARK = 0x00BC       # long header (off command burst 1, fan_only)
P_HEADER_MARK_SHORT = 0x006F # short header (on commands)
P_HEADER_SPACE = 0x015F
P_INTER_MARK = 0x0071
P_INTER_SPACE = 0x015E
P_MARK = 0x0011
P_SPACE_0 = 0x0018
P_SPACE_1 = 0x003E
P_TAIL = 0x0181

def bits_to_raw(bits):
    raw = [HEADER_MARK, -HEADER_SPACE]
    for b in bits:
        raw.append(MARK)
        raw.append(-SPACE_1 if b == '1' else -SPACE_0)
    return raw

def bits_to_pronto_pairs(bits):
    pairs = []
    for b in bits:
        pairs.extend([P_MARK, P_SPACE_1 if b == '1' else P_SPACE_0])
    return pairs

def rev(val, n):
    return int(f"{val:0{n}b}"[::-1], 2)

def checksum(after_bits):
    return f"{rev(33 - after_bits.count('1'), 5):05b}"

# Burst 1: only differs for low fan (bit 45 = 1, checksum adjusts)
BURST1_BASE = list('010000000100100111110000000000000000000000000000000011111')

def make_burst1(fan):
    b = BURST1_BASE.copy()
    if fan == 'quiet':
        b[41] = '1'
    # Recalculate burst 1 checksum (bits 12-16, same formula)
    after = ''.join(b[17:])
    ones = after.count('1')
    chk = f"{rev(33 - ones, 5):05b}"
    b[12:17] = list(chk)
    return ''.join(b)

# Burst 2 bit layout:
# 0-11:  constant prefix
# 12-16: 5-bit checksum (LSB-first)
# 17-35: constant
# 36-39: temperature (LSB-first, val = temp-16)
# 40:    constant 1
# 41-43: fan speed bits
# 44-46: mode bits
# 47-56: constant suffix

FAN_BITS = {         # bits 41, 42, 43
    'auto':   '000',
    'quiet':  '000',  # same as auto in burst 2; burst 1 differentiates
    'low':    '010',
    'medium': '001',
    'high':   '101',
}

MODE_BITS = {        # bits 44, 45, 46
    'cool':      '100',
    'heat':      '001',
    'dry':       '010',
    'fan_only':  '110',  # from Fan Max capture: bits 44-46 = 110
    'heat_cool': '011',
}

def make_burst2(temp, mode, fan, swing='on'):
    prefix = '100000000100'
    s = '0' if swing == 'on' else '1'
    after = '111' + s + '1' + s + '1100000000000'
    after += f"{rev(temp - 16, 4):04b}"
    after += '1'
    after += FAN_BITS.get(fan, '000')
    after += MODE_BITS.get(mode, '100')
    after += '0000011111'
    return prefix + checksum(after) + after

def make_off():
    b1 = '01000000010011011111000000000000000000000000000000000011'
    b2 = '10000000010000011111000000000000000011000000101100000000'
    b3 = '10000000010001001111111110000000000010101000001000000011'
    raw = bits_to_raw(b1)
    raw[-1] = -INTER_BURST
    raw += bits_to_raw(b2)
    raw[-1] = -INTER_BURST
    raw += bits_to_raw(b3)
    raw[-1] = -TAIL_SPACE
    return raw

def make_on(temp, mode, fan='auto', swing='on'):
    raw = bits_to_raw(make_burst1(fan))
    raw[-1] = -INTER_BURST
    raw += bits_to_raw(make_burst2(temp, mode, fan, swing))
    raw[-1] = -TAIL_SPACE
    return raw

def pronto_hex(pairs, n_bursts):
    """Format Pronto pairs into hex string. All data in burst1 sequence (no repeat)."""
    n_pairs = len(pairs) // 2
    header = [0x0000, P_FREQ, n_pairs, 0x0000]
    return ' '.join(f'{v:04X}' for v in header + pairs)

def make_off_pronto():
    b1 = '01000000010011011111000000000000000000000000000000000011'
    b2 = '10000000010000011111000000000000000011000000101100000000'
    b3 = '10000000010001001111111110000000000010101000001000000011'
    pairs = [P_HEADER_MARK, P_HEADER_SPACE] + bits_to_pronto_pairs(b1) + [P_MARK, P_INTER_SPACE]
    pairs += [P_INTER_MARK, P_INTER_SPACE] + bits_to_pronto_pairs(b2) + [P_MARK, P_INTER_SPACE]
    pairs += [P_INTER_MARK, P_INTER_SPACE] + bits_to_pronto_pairs(b3) + [P_MARK, P_TAIL]
    return pronto_hex(pairs, 3)

def make_on_pronto(temp, mode, fan='auto', swing='on'):
    hdr = P_HEADER_MARK if mode == 'fan_only' else P_HEADER_MARK_SHORT
    pairs = [hdr, P_HEADER_SPACE] + bits_to_pronto_pairs(make_burst1(fan)) + [P_MARK, P_INTER_SPACE]
    pairs += [P_INTER_MARK, P_INTER_SPACE] + bits_to_pronto_pairs(make_burst2(temp, mode, fan, swing)) + [P_MARK, P_TAIL]
    return pronto_hex(pairs, 2)

def verify():
    captured_b1 = {
        'auto': '010000000100100111110000000000000000000000000000000011111',
        'quiet':  '010000000100000111110000000000000000000001000000000011111',
    }
    captured_b2 = {
        ("cool", 25, "auto"): "100000000100010011110101100000000000100110001000000011111",
        ("cool", 24, "auto"): "100000000100110011110101100000000000000110001000000011111",
        ("cool", 23, "auto"): "100000000100100011110101100000000000111010001000000011111",
        ("cool", 22, "auto"): "100000000100010011110101100000000000011010001000000011111",
        ("cool", 21, "auto"): "100000000100010011110101100000000000101010001000000011111",
        ("cool", 20, "auto"): "100000000100110011110101100000000000001010001000000011111",
        ("heat", 24, "auto"): "100000000100110011110101100000000000000110000010000011111",
        ("dry",  24, "auto"): "100000000100110011110101100000000000000110000100000011111",
        ("cool", 24, "low"):      "100000000100010011110101100000000000000110101000000011111",
        ("cool", 24, "medium"):   "100000000100010011110101100000000000000110011000000011111",
        ("cool", 24, "high"):     "100000000100100011110101100000000000000111011000000011111",
        ("cool", 24, "quiet"):    "100000000100110011110101100000000000000110001000000011111",
    }
    # Flap stopped verification
    captured_b2_stop = {
        ("cool", 24, "auto", "off"): "100000000100100011111111100000000000000110001000000011111",
    }

    ok = True
    print("=== Burst 1 verification ===")
    for fan, expected in captured_b1.items():
        gen = make_burst1(fan)
        match = gen == expected
        print(f"  {'✅' if match else '❌'} {fan} fan")
        if not match:
            ok = False
            for i, (g, e) in enumerate(zip(gen, expected)):
                if g != e: print(f"      Bit {i}: gen={g} exp={e}")

    print("\n=== Burst 2 verification ===")
    for (mode, temp, fan), expected in captured_b2.items():
        gen = make_burst2(temp, mode, fan)
        match = gen == expected
        print(f"  {'✅' if match else '❌'} {mode} {temp}°C {fan}")
        if not match:
            ok = False
            for i, (g, e) in enumerate(zip(gen, expected)):
                if g != e: print(f"      Bit {i}: gen={g} exp={e}")

    print("\n=== Burst 2 swing verification ===")
    for (mode, temp, fan, swing), expected in captured_b2_stop.items():
        gen = make_burst2(temp, mode, fan, swing)
        match = gen == expected
        print(f"  {'✅' if match else '❌'} {mode} {temp}°C {fan} flap={'moving' if swing=='on' else 'stopped'}")
        if not match:
            ok = False
            for i, (g, e) in enumerate(zip(gen, expected)):
                if g != e: print(f"      Bit {i}: gen={g} exp={e}")

    return ok

def generate_smartir():
    fans = ["auto", "quiet", "low", "medium", "high"]
    modes = ["cool", "heat", "heat_cool", "dry", "fan_only"]
    fan_only_fans = ["low", "medium", "high"]  # no auto, no quiet for fan_only
    dry_fans = ["auto"]  # dry forces auto fan
    heat_cool_fans = ["auto", "low", "medium", "high"]  # no quiet in auto mode

    data = {
        "manufacturer": "Samsung",
        "supportedModels": ["AQV18NSCN", "AQV09NSAX"],
        "supportedController": "ESPHome",
        "commandsEncoding": "Raw",
        "minTemperature": 16.0,
        "maxTemperature": 30.0,
        "precision": 1.0,
        "operationModes": modes,
        "fanModes": fans,
        "swingModes": ["off", "on"],
        "commands": {"off": json.dumps(make_off_pronto())}
    }

    count = 1
    for mode in modes:
        data["commands"][mode] = {}
        mode_fans = fan_only_fans if mode == "fan_only" else dry_fans if mode == "dry" else heat_cool_fans if mode == "heat_cool" else fans
        for fan in mode_fans:
            if mode == "fan_only":
                data["commands"][mode][fan] = {}
                for swing_name, swing_val in [("off", "off"), ("on", "on")]:
                    data["commands"][mode][fan][swing_name] = json.dumps(make_on_pronto(24, mode, fan, swing_val))
                    count += 1
            else:
                data["commands"][mode][fan] = {}
                for swing_name, swing_val in [("off", "off"), ("on", "on")]:
                    data["commands"][mode][fan][swing_name] = {}
                    for temp in range(16, 31):
                        data["commands"][mode][fan][swing_name][str(temp)] = json.dumps(make_on_pronto(temp, mode, fan, swing_val))
                        count += 1
    return data, count

if __name__ == "__main__":
    if verify():
        print("\n✅ All verified! Generating SmartIR JSON...")
        data, count = generate_smartir()
        with open("samsung_aqv18nscn.json", "w") as f:
            json.dump(data, f, indent=2)
        print(f"Generated {count} codes -> samsung_aqv18nscn.json")
    else:
        print("\n❌ Verification failed!")
