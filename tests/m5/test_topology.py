#!/usr/bin/env python3

import socket
import unittest
from unittest import mock

import topology


class TopologyTest(unittest.TestCase):
    def test_allocations_are_dynamic_and_distinct(self) -> None:
        values = topology.allocate_topology()
        self.assertEqual(len(values), 5)
        self.assertEqual(len(set(values.values())), len(values))
        self.assertNotIn(topology.FIXED_M4_PORT, values.values())
        for port in values.values():
            self.assertGreater(port, 0)
            self.assertLessEqual(port, 65535)

    def test_shared_port_was_available_to_tcp_and_udp(self) -> None:
        # The allocation helper closes its probes before returning. Rebind the
        # selected port immediately to validate both transport sockets expected
        # by Caddy's shared listener contract.
        port = topology.reserve_shared_ipv4_port()
        tcp = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            tcp.bind(("127.0.0.1", port))
            udp.bind(("127.0.0.1", port))
        finally:
            tcp.close()
            udp.close()

    def test_duplicate_allocation_retries_the_complete_topology(self) -> None:
        with mock.patch.object(
            topology, "reserve_shared_ipv4_port", side_effect=[10000, 20000]
        ), mock.patch.object(
            topology,
            "reserve_udp_port",
            side_effect=[10001, 10002, 10003, 10000, 20001, 20002, 20003, 20004],
        ):
            values = topology.allocate_topology()
        self.assertEqual(values["proxy"], 20000)
        self.assertEqual(len(set(values.values())), len(values))


if __name__ == "__main__":
    unittest.main()
