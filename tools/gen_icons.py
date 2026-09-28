#!/usr/bin/env python3
"""Generates firmware/include/icons.h from the 6x6 artwork below.

Edit the drawings ('#' = LED on, '.' = off) and run:
    python3 tools/gen_icons.py
Icons with several frames are animated (see ANIMS at the end).
"""
import pathlib

ART = {
    # name: (comment or None, 6 rows of 6 chars)
    'DIAL_RING': ("Round dial outline (used by the dynamic clock icons).",
                  [".####.", "#....#", "#....#", "#....#", "#....#", ".####."]),
    'CLOCK': ("Static clock face with hands.",
              [".####.", "#.#..#", "#.#..#", "#.##.#", "#....#", ".####."]),
    'CALENDAR': ("Calendar page; the 'today' mark blinks.",
                 [".#..#.", "######", "#....#", "#.##.#", "#....#", "######"]),
    'CALENDAR_B': (None, [".#..#.", "######", "#....#", "#....#", "#....#", "######"]),
    'ENVELOPE': ("Envelope: closed / opening.",
                 ["######", "##..##", "#.##.#", "#....#", "#....#", "######"]),
    'ENVELOPE_B': (None, ["..##..", ".#..#.", "######", "#....#", "#....#", "######"]),
    'BELL_L': ("Bell swinging (alarm).",
               ["..#...", ".###..", ".###..", ".###..", "#####.", "..#..."]),
    'BELL_R': (None, ["...#..", "..###.", "..###.", "..###.", ".#####", "...#.."]),
    'WIFI': ("Wi-Fi: arcs growing.", [".####.", "#....#", "..##..", ".#..#.", "......", "..##.."]),
    'WIFI_1': (None, ["......", "......", "......", "......", "......", "..##.."]),
    'WIFI_2': (None, ["......", "......", "..##..", ".#..#.", "......", "..##.."]),
    'WIFI_OFF': ("No internet: Wi-Fi crossed out.",
                 ["#####.", "##...#", "..##..", ".#.##.", "....#.", "..##.#"]),
    'NOTE': ("Musical note, bouncing.", ["...##.", "...#.#", "...#..", "...#..", ".###..", ".###.."]),
    'NOTE_B': (None, ["...##.", "...#.#", "...#..", ".###..", ".###..", "......"]),
    'ALERT': ("Emergency: warning triangle (blinks).",
              ["..##..", ".#..#.", ".#..#.", "##..##", "######", "##..##"]),
    'BLANK': (None, ["......", "......", "......", "......", "......", "......"]),
    'GEAR': ("Gear (settings).", [".#..#.", ".####.", "##..##", "##..##", ".####.", ".#..#."]),
    # weather
    'SUN': ("Sun with rotating rays (clear day, UV).",
            ["#.##.#", ".####.", "######", "######", ".####.", "#.##.#"]),
    'SUN_B': (None, [".#..#.", "#.##.#", ".####.", ".####.", "#.##.#", ".#..#."]),
    'MOON': ("Moon with a twinkling star (clear night).",
             ["..###.", ".##...", "##..#.", "##....", ".##...", "..###."]),
    'MOON_B': (None, ["..###.", ".##...", "##....", "##....", ".##..#", "..###."]),
    'PARTLY': ("Partly cloudy: sun rays blink behind the cloud.",
               ["#...#.", "...###", ".##.#.", "####..", "######", "......"]),
    'PARTLY_B': (None, ["......", "...###", ".##.#.", "####.#", "######", "......"]),
    'CLOUD': ("Cloud drifting.", ["......", "..##..", ".####.", "#####.", "#####.", "......"]),
    'CLOUD_B': (None, ["......", "...##.", "..####", ".#####", ".#####", "......"]),
    'RAIN': ("Rain falling (3 frames).",
             ["..##..", ".####.", "######", "#...#.", "..#...", "....#."]),
    'RAIN_B': (None, ["..##..", ".####.", "######", ".#...#", "#...#.", "..#..."]),
    'RAIN_C': (None, ["..##..", ".####.", "######", "..#...", ".#...#", "#...#."]),
    'STORM': ("Thunderstorm: lightning flashes.",
              ["..##..", ".####.", "######", "...#..", "..#...", ".#...."]),
    'STORM_B': (None, ["..##..", ".####.", "######", "......", "......", "......"]),
    'FOG': ("Fog drifting.", ["......", "#####.", "......", ".#####", "......", "#####."]),
    'FOG_B': (None, ["......", ".#####", "......", "#####.", "......", ".#####"]),
    'UMBRELLA': ("Umbrella with drops falling.",
                 ["..##..", ".####.", "######", "#..#..", "...#.#", "..##.."]),
    'UMBRELLA_B': (None, ["..##..", ".####.", "######", "...#.#", "#..#..", "..##.."]),
    'SUNRISE': ("Sunrise: arrow rising.", ["..##..", ".#..#.", "......", "..##..", ".####.", "######"]),
    'SUNRISE_B': (None, [".#..#.", "......", "......", "..##..", ".####.", "######"]),
    'SUNSET': ("Sunset: arrow sinking.", [".#..#.", "..##..", "......", "..##..", ".####.", "######"]),
    'SUNSET_B': (None, ["......", ".#..#.", "..##..", "..##..", ".####.", "######"]),
    # currencies and change arrows
    'DOLLAR': ("Currencies and crypto.", ["..#...", ".####.", "#.#...", ".###..", "..#.#.", "####.."]),
    'EURO': (None, ["..####", ".#....", "####..", ".#....", "####..", "..####"]),
    'POUND': (None, ["..##..", ".#..#.", ".#....", "####..", ".#....", "######"]),
    'BITCOIN': (None, [".#.#..", "#####.", ".#...#", ".####.", ".#...#", "#####."]),
    'ETHER': (None, ["..##..", ".####.", "######", "......", ".####.", "..##.."]),
    'ARROW_UP': ("Daily change: arrow bouncing up / down.",
                 ["..##..", ".####.", "######", "..##..", "..##..", "..##.."]),
    'ARROW_UP_B': (None, [".####.", "######", "..##..", "..##..", "..##..", "......"]),
    'ARROW_DOWN': (None, ["..##..", "..##..", "..##..", "######", ".####.", "..##.."]),
    'ARROW_DOWN_B': (None, ["......", "..##..", "..##..", "..##..", "######", ".####."]),
}

# Animated icons: name -> (frames, milliseconds per frame)
ANIMS = {
    'A_CALENDAR': (['CALENDAR', 'CALENDAR_B'], 600),
    'A_ENVELOPE': (['ENVELOPE', 'ENVELOPE_B'], 500),
    'A_BELL': (['BELL_L', 'BELL_R'], 250),
    'A_WIFI': (['WIFI_1', 'WIFI_2', 'WIFI'], 400),
    'A_NOTE': (['NOTE', 'NOTE_B'], 350),
    'A_SUN': (['SUN', 'SUN_B'], 500),
    'A_MOON': (['MOON', 'MOON_B'], 700),
    'A_PARTLY': (['PARTLY', 'PARTLY_B'], 600),
    'A_CLOUD': (['CLOUD', 'CLOUD_B'], 900),
    'A_RAIN': (['RAIN', 'RAIN_B', 'RAIN_C'], 250),
    'A_STORM': (['STORM', 'STORM_B', 'STORM', 'STORM_B', 'STORM_B', 'STORM_B'], 150),
    'A_FOG': (['FOG', 'FOG_B'], 800),
    'A_UMBRELLA': (['UMBRELLA', 'UMBRELLA_B'], 350),
    'A_SUNRISE': (['SUNRISE', 'SUNRISE_B'], 400),
    'A_SUNSET': (['SUNSET', 'SUNSET_B'], 400),
    'A_ALERT': (['ALERT', 'BLANK'], 300),
    'A_UP': (['ARROW_UP', 'ARROW_UP_B'], 300),
    'A_DOWN': (['ARROW_DOWN', 'ARROW_DOWN_B'], 300),
}


def row_byte(r):
    return int(r.replace('#', '1').replace('.', '0'), 2) << 1


def main():
    out = ['#pragma once', '#include <Arduino.h>', '',
           '// GENERATED by tools/gen_icons.py — edit the drawings there, not here.',
           '// Icons: 6x6 artwork centered in an 8x8 cell (1-pixel empty border).',
           '// One byte per row (top to bottom); bit 7 = leftmost column.',
           'namespace icons {', '', 'using Icon = uint8_t[8];', '']
    for name, (comment, rows) in ART.items():
        assert len(rows) == 6 and all(len(r) == 6 for r in rows), name
        if comment:
            out.append('// ' + comment)
        out.append(f'constexpr Icon {name} = {{')
        lines = ['0b00000000'] + ['0b' + format(row_byte(r), '08b') for r in rows] + ['0b00000000']
        out += ['  ' + l + ',' for l in lines]
        out += ['};', '']
    out += ['// Animated icons: a list of frames and the time each frame stays.',
            'struct Anim {', '  const uint8_t* const* frames;', '  uint8_t count;', '  uint16_t periodMs;', '};', '']
    for name, (frames, period) in ANIMS.items():
        arr = name + '_FRAMES'
        out.append(f'static const uint8_t* const {arr}[] = {{{", ".join(frames)}}};')
        out.append(f'constexpr Anim {name} = {{{arr}, {len(frames)}, {period}}};')
    out += ['', '// Current frame of an animation (first frame when animations are off).',
            'inline const uint8_t* frame(const Anim& a, bool animate) {',
            '  if (!animate || a.count < 2) return a.frames[0];',
            '  return a.frames[(millis() / a.periodMs) % a.count];',
            '}', '', '}  // namespace icons', '']
    path = pathlib.Path(__file__).resolve().parent.parent / 'firmware' / 'include' / 'icons.h'
    path.write_text('\n'.join(out))
    print(f'wrote {path}')


if __name__ == '__main__':
    main()
