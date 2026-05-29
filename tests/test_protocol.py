#!/usr/bin/env python3
"""Test samsung_aqv C++ protocol against the JSON lookup table (457 vectors)."""
import json
import subprocess
import sys
import os

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
JSON_PATH = os.path.join(REPO, "tests", "samsung_aqv18nscn.json")
ENCODE_BIN = os.path.join(REPO, "tests", "encode_cli")
ENCODE_SRC = os.path.join(REPO, "tests", "encode_cli.cpp")
DECODE_BIN = os.path.join(REPO, "tests", "decode_cli")
DECODE_SRC = os.path.join(REPO, "tests", "decode_cli.cpp")


def build():
    """Compile the CLI encoder and decoder."""
    for src, bin in [(ENCODE_SRC, ENCODE_BIN), (DECODE_SRC, DECODE_BIN)]:
        r = subprocess.run(
            ["g++", "-std=c++17", "-O2", "-o", bin, src],
            capture_output=True, text=True
        )
        if r.returncode != 0:
            print(f"Build failed ({src}):\n{r.stderr}")
            sys.exit(1)


def encode(mode, fan, swing, temp):
    """Call the C++ encoder and return the Pronto string."""
    r = subprocess.run(
        [ENCODE_BIN, mode, fan, swing, str(temp)],
        capture_output=True, text=True
    )
    return r.stdout.strip()


def decode(pronto):
    """Call the C++ decoder and return parsed fields."""
    r = subprocess.run(
        [DECODE_BIN, pronto],
        capture_output=True, text=True
    )
    # Output format: mode fan swing temp (or "off")
    return r.stdout.strip()


def test_encode(data):
    """Test all JSON entries against encoder. Returns (passed, failed)."""
    passed = failed = 0

    # OFF
    expected_off = data["commands"]["off"].strip('"')
    got = encode("off", "", "", "0")
    if got == expected_off:
        passed += 1
    else:
        failed += 1
        print(f"❌ OFF")

    # ON: normal modes
    for mode in ["cool", "heat", "heat_cool", "dry"]:
        for fan in data["commands"][mode]:
            for swing in data["commands"][mode][fan]:
                for temp, pronto in data["commands"][mode][fan][swing].items():
                    expected = pronto.strip('"')
                    got = encode(mode, fan, swing, temp)
                    if got == expected:
                        passed += 1
                    else:
                        failed += 1
                        if failed <= 5:
                            print(f"❌ {mode}/{fan}/{swing}/{temp}°C")

    # ON: fan_only
    for fan in data["commands"]["fan_only"]:
        for swing, pronto in data["commands"]["fan_only"][fan].items():
            expected = pronto.strip('"')
            got = encode("fan_only", fan, swing, "24")
            if got == expected:
                passed += 1
            else:
                failed += 1
                if failed <= 5:
                    print(f"❌ fan_only/{fan}/{swing}")

    return passed, failed


def test_roundtrip():
    """Encode → decode for every JSON entry. Returns (passed, failed)."""
    passed = failed = 0

    with open(JSON_PATH) as f:
        data = json.load(f)

    # OFF
    pronto = encode("off", "", "", "0")
    result = decode(pronto)
    if result == "off":
        passed += 1
    else:
        failed += 1
        print(f"❌ roundtrip OFF: got '{result}'")

    # Normal modes
    for mode in ["cool", "heat", "heat_cool", "dry"]:
        for fan in data["commands"][mode]:
            for swing in data["commands"][mode][fan]:
                for temp in data["commands"][mode][fan][swing]:
                    pronto = encode(mode, fan, swing, temp)
                    result = decode(pronto)
                    # After fallbacks
                    exp_fan = fan
                    if mode == "dry":
                        exp_fan = "auto"
                    expected = f"{mode} {exp_fan} {swing} {temp}"
                    if result == expected:
                        passed += 1
                    else:
                        failed += 1
                        if failed <= 5:
                            print(f"❌ roundtrip {mode}/{fan}/{swing}/{temp}: got '{result}' exp '{expected}'")

    # fan_only
    for fan in data["commands"]["fan_only"]:
        for swing in data["commands"]["fan_only"][fan]:
            pronto = encode("fan_only", fan, swing, "24")
            result = decode(pronto)
            expected = f"fan_only {fan} {swing} 24"
            if result == expected:
                passed += 1
            else:
                failed += 1
                if failed <= 5:
                    print(f"❌ roundtrip fan_only/{fan}/{swing}: got '{result}' exp '{expected}'")

    return passed, failed


def test_fan_fallbacks():
    """Verify resolve_fan produces correct fallbacks. Returns (passed, failed)."""
    passed = failed = 0

    cases = [
        # (mode, input_fan, expected_fan)
        ("dry", "high", "auto"),
        ("dry", "low", "auto"),
        ("dry", "quiet", "auto"),
        ("fan_only", "auto", "low"),
        ("fan_only", "quiet", "low"),
        ("fan_only", "medium", "medium"),
        ("heat_cool", "quiet", "auto"),
        ("heat_cool", "high", "high"),
        ("cool", "quiet", "quiet"),  # no fallback
        ("cool", "auto", "auto"),    # no fallback
    ]

    for mode, in_fan, exp_fan in cases:
        pronto = encode(mode, in_fan, "on", "24")
        result = decode(pronto)
        got_fan = result.split()[1] if result != "off" else ""
        if got_fan == exp_fan:
            passed += 1
        else:
            failed += 1
            print(f"❌ fallback {mode}/{in_fan}: got '{got_fan}' exp '{exp_fan}'")

    return passed, failed


def main():
    build()

    with open(JSON_PATH) as f:
        data = json.load(f)

    total_passed = total_failed = 0

    print("=== Encode vs JSON (457 vectors) ===")
    p, f = test_encode(data)
    total_passed += p; total_failed += f
    print(f"{p}/{p+f} passed")

    print("\n=== Decode roundtrip ===")
    p, f = test_roundtrip()
    total_passed += p; total_failed += f
    print(f"{p}/{p+f} passed")

    print("\n=== Fan fallbacks ===")
    p, f = test_fan_fallbacks()
    total_passed += p; total_failed += f
    print(f"{p}/{p+f} passed")

    print(f"\n{'='*40}")
    print(f"TOTAL: {total_passed}/{total_passed+total_failed} passed", end="")
    if total_failed:
        print(f" ({total_failed} failed)")
    else:
        print(" ✅")
    sys.exit(0 if total_failed == 0 else 1)


if __name__ == "__main__":
    main()
