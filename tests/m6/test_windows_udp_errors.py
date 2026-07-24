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


class ExpectNoUdpTest(unittest.TestCase):
    def test_timeout_is_no_packet(self):
        sock = FakeSocket(socket.timeout())
        expect_no_udp(sock, "timeout")
        self.assertEqual(sock.timeout, 0.25)

    def test_connection_reset_is_allowed_for_closed_relay(self):
        sock = FakeSocket(ConnectionResetError())
        expect_no_udp(sock, "closed relay", allow_connection_refused=True)

    def test_connection_reset_requires_explicit_allowance(self):
        with self.assertRaises(ConnectionResetError):
            expect_no_udp(FakeSocket(ConnectionResetError()), "unexpected reset")

    def test_other_connection_error_is_not_hidden(self):
        with self.assertRaises(ConnectionAbortedError):
            expect_no_udp(
                FakeSocket(ConnectionAbortedError()),
                "other connection error",
                allow_connection_refused=True,
            )

    def test_packet_still_fails(self):
        with self.assertRaisesRegex(AssertionError, "unexpected UDP packet"):
            expect_no_udp(FakeSocket((b"payload", ("127.0.0.1", 9))), "packet")

    @unittest.skipUnless(sys.platform == "win32", "requires native Windows")
    def test_native_windows_closed_udp_port(self):
        reset = None
        for _ in range(5):
            closed = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            closed.bind(("127.0.0.1", 0))
            address = closed.getsockname()
            closed.close()

            probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            try:
                probe.bind(("127.0.0.1", 0))
                probe.settimeout(1)
                probe.sendto(b"closed-relay-probe", address)
                probe.recvfrom(65535)
            except ConnectionResetError as error:
                reset = error
                break
            except socket.timeout:
                pass
            finally:
                probe.close()

        self.assertIsNotNone(reset, "closed Windows UDP port produced no reset")
        expect_no_udp(
            FakeSocket(reset),
            "native Windows closed relay",
            allow_connection_refused=True,
        )


if __name__ == "__main__":
    unittest.main()
