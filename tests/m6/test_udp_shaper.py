#!/usr/bin/env python3

import pathlib
import tempfile
import types
import unittest

from udp_shaper import ImpairmentPolicy, read_ceiling


def arguments(**overrides):
    values = {
        "seed": 7,
        "loss_percent": 0,
        "reorder_percent": 0,
        "delay_ms": 0,
        "jitter_ms": 0,
        "reorder_delay_ms": 30,
        "bandwidth_kbps": 0,
    }
    values.update(overrides)
    return types.SimpleNamespace(**values)


class ImpairmentPolicyTest(unittest.TestCase):
    def test_same_seed_replays_identical_decisions(self):
        args = arguments(
            loss_percent=15,
            reorder_percent=20,
            delay_ms=10,
            jitter_ms=5,
            bandwidth_kbps=512,
        )
        first = ImpairmentPolicy(args)
        second = ImpairmentPolicy(args)
        first_results = [first.decide("c2s", 100 + index, 1.0) for index in range(50)]
        second_results = [second.decide("c2s", 100 + index, 1.0) for index in range(50)]
        self.assertEqual(first_results, second_results)
        self.assertTrue(any(result[0] for result in first_results))
        self.assertTrue(any(result[2] == "reorder" for result in first_results))

    def test_bandwidth_serializes_each_direction_independently(self):
        policy = ImpairmentPolicy(arguments(bandwidth_kbps=8))
        first = policy.decide("c2s", 1000, 5.0)
        second = policy.decide("c2s", 1000, 5.0)
        reverse = policy.decide("s2c", 1000, 5.0)
        self.assertEqual(first, (False, 6.0, "bandwidth"))
        self.assertEqual(second, (False, 7.0, "bandwidth"))
        self.assertEqual(reverse, (False, 6.0, "bandwidth"))

    def test_delay_and_reorder_are_non_negative(self):
        policy = ImpairmentPolicy(
            arguments(reorder_percent=100, delay_ms=1, jitter_ms=10)
        )
        drop, send_at, reason = policy.decide("c2s", 10, 2.0)
        self.assertFalse(drop)
        self.assertGreaterEqual(send_at, 2.03)
        self.assertEqual(reason, "reorder")

    def test_ceiling_file_is_fail_open_for_invalid_control_data(self):
        with tempfile.TemporaryDirectory() as root:
            path = pathlib.Path(root, "ceiling")
            self.assertEqual(read_ceiling(path), 0)
            path.write_text("1232\n", encoding="ascii")
            self.assertEqual(read_ceiling(path), 1232)
            path.write_text("invalid\n", encoding="ascii")
            self.assertEqual(read_ceiling(path), 0)


if __name__ == "__main__":
    unittest.main()
