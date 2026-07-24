#!/usr/bin/env python3

import pathlib
import socket
import sys
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from socks5_udp_m2 import expect_no_udp


class FakeSocket:
    def __init__(self, result):
        self.result = result
        self.timeout = None

    def settimeout(self, timeout):
        self.timeout = timeout

    def recvfrom(self, _size):
        if isinstance(self.result, BaseException):
            raise self.result
        return self.result


class WindowsConnectionReset(ConnectionResetError):
    winerror = 10054


class OtherConnectionReset(ConnectionResetError):
    winerror = 10053


class ExpectNoUdpTest(unittest.TestCase):
    def test_timeout_is_no_packet(self):
        sock = FakeSocket(socket.timeout())
        expect_no_udp(sock, "timeout")
        self.assertEqual(sock.timeout, 0.25)

    def test_windows_port_unreachable_is_allowed_for_closed_relay(self):
        sock = FakeSocket(WindowsConnectionReset())
        expect_no_udp(sock, "closed relay", allow_connection_refused=True)

    def test_windows_port_unreachable_requires_explicit_allowance(self):
        with self.assertRaises(WindowsConnectionReset):
            expect_no_udp(FakeSocket(WindowsConnectionReset()), "unexpected reset")

    def test_other_reset_is_not_hidden(self):
        with self.assertRaises(OtherConnectionReset):
            expect_no_udp(
                FakeSocket(OtherConnectionReset()),
                "other reset",
                allow_connection_refused=True,
            )

    def test_packet_still_fails(self):
        with self.assertRaisesRegex(AssertionError, "unexpected UDP packet"):
            expect_no_udp(FakeSocket((b"payload", ("127.0.0.1", 9))), "packet")


if __name__ == "__main__":
    unittest.main()
