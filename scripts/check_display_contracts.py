"""Offline geometry/row-window contract checks, not an on-board visual test."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


class DisplayContracts(unittest.TestCase):
    def test_rounder_handles_both_axes(self):
        source = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
        for field, mask in (("x1", "& ~3"), ("x2", "| 3"),
                            ("y1", "& ~3"), ("y2", "| 3")):
            self.assertRegex(source, rf"area->{field}\s*=.*{re.escape(mask)}")

    def test_all_partial_buffer_slices_are_aligned(self):
        # Equivalent to LVGL 8.2 get_max_row() with the 4-row rounder, across
        # every aligned width/height and all three buffer fallback sizes.
        checked = 0
        for lines in (8, 16, 32):
            capacity = 480 * lines
            for width in range(4, 481, 4):
                for height in range(4, 481, 4):
                    rows = min(capacity // width, height) // 4 * 4
                    self.assertGreaterEqual(rows, 4)
                    for y in range(0, height, rows):
                        chunk = min(rows, height - y)
                        self.assertEqual(y % 4, 0)
                        self.assertEqual(chunk % 4, 0)
                        self.assertLessEqual(width * chunk, capacity)
                    checked += 1
        self.assertEqual(checked, 43200)

    def test_original_two_eye_layout_is_in_bounds(self):
        self.assertEqual(168 % 4, 0)
        self.assertEqual(164 % 4, 0)
        self.assertLessEqual(168 + 164, 480)
        self.assertEqual(480 - 70 - 125, 285)
        self.assertEqual(2 + 160 + 2, 164)

    def test_game_geometry_at_480(self):
        source = (ROOT / "src/game2048.c").read_text(encoding="utf-8")
        gap = int(re.search(r"#define GAME2048_BOARD_GAP (\d+)", source)[1])
        available = min(480 - 64, 480 - 96)
        tile = (available - gap * 5) // 4
        board = tile * 4 + gap * 5
        self.assertEqual(board, 384)
        self.assertEqual((480 - board) // 2, 48)
        self.assertLessEqual(80 + board, 480)
        self.assertLessEqual(gap + 3 * (tile + gap) + tile, board - gap)

    def test_sleep_preferences_and_gpio_wake(self):
        source = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
        for key in ("autosleep", "idlesec"):
            self.assertGreaterEqual(source.count(f'"{key}"'), 2)
        for name in ("INT_N_PIN", "RIGHT_PIN", "LEFT_PIN", "PUSH_PIN"):
            self.assertIn(f"(gpio_num_t){name}", source)
        self.assertIn("saved_power_output", source)
        self.assertIn("const bool bleStopped = !deviceBleRadioRunning", source)
        self.assertNotIn("restart_ble_after_wake", source)

    def test_ota_slots_and_assets_do_not_overlap(self):
        import csv
        partition = (ROOT / "my.csv").read_text(encoding="utf-8")
        rows = list(csv.reader(line for line in partition.splitlines()
                               if line.strip() and not line.lstrip().startswith("#")))
        entries = {row[0].strip(): (row[1].strip(), row[2].strip(),
                   int(row[3].strip(), 0), int(row[4].strip(), 0)) for row in rows}
        for name in ("ota_0", "ota_1", "assets_a", "assets_b", "otadata"):
            self.assertIn(name, entries)
        for name in ("ota_0", "ota_1"):
            self.assertEqual(entries[name][0], "app")
            self.assertEqual(entries[name][2] % 0x10000, 0)
            self.assertGreaterEqual(entries[name][3], 3 * 1024 * 1024)
        ranges = sorted((entry[2], entry[2] + entry[3], name)
                        for name, entry in entries.items())
        for previous, following in zip(ranges, ranges[1:]):
            self.assertLessEqual(previous[1], following[0])
        self.assertLessEqual(ranges[-1][1], 16 * 1024 * 1024)


if __name__ == "__main__":
    unittest.main()
