#!/usr/bin/env python3

import json
import pathlib
import unittest


CONTRACT = pathlib.Path(__file__).with_name("contract.json")
NETWORK_PROFILES = pathlib.Path(__file__).with_name("network_profiles.json")
SOAK_TIERS = pathlib.Path(__file__).with_name("soak_tiers.json")


class M6ContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.contract = json.loads(CONTRACT.read_text(encoding="utf-8"))

    def test_schema_and_gate_order(self) -> None:
        self.assertEqual(self.contract["schema"], 1)
        gates = self.contract["gates"]
        self.assertEqual([gate["id"] for gate in gates], [f"G{i}" for i in range(7)])
        markers = [gate["marker"] for gate in gates]
        self.assertEqual(len(markers), len(set(markers)))
        self.assertEqual(markers[-1], self.contract["final_marker"])
        markers = self.contract["intermediate_markers"]
        self.assertEqual(len(markers), len(set(markers)))
        self.assertIn("M6_G1_LIVE_CEILING_UNIT_OK", markers)
        self.assertIn("M6_G1B_LIVE_CEILING_OK", markers)
        self.assertIn("M6_G1C_PMTU_OK", markers)

    def test_frozen_protocol_has_no_fallback_or_replay(self) -> None:
        protocol = self.contract["protocol"]
        self.assertEqual(protocol["context_id"], 0)
        self.assertTrue(protocol["all_quic_proxy_chain_required"])
        self.assertFalse(protocol["datagram_replay_allowed"])
        self.assertFalse(protocol["udp_payload_padding_v1"])
        self.assertEqual(
            set(protocol["forbidden_transports"]),
            {
                "UoT",
                "UDP-over-stream",
                "Capsule data path",
                "private framing",
                "second QUIC stack",
            },
        )

    def test_release_policy_is_fail_closed(self) -> None:
        policy = self.contract["release_policy"]
        self.assertEqual(
            policy["allowed_states"], ["verified", "not run", "blocked", "failed"]
        )
        self.assertFalse(policy["open_blocker_allowed"])
        self.assertFalse(policy["open_high_allowed"])
        self.assertFalse(policy["open_medium_allowed"])
        self.assertTrue(policy["low_requires_owner_and_mitigation"])

    def test_platform_claims_are_separate_and_pending_are_not_passes(self) -> None:
        platforms = {item["id"]: item for item in self.contract["platforms"]}
        self.assertEqual(
            set(platforms),
            {"macos-arm64", "linux-x64", "windows-x64", "android-arm64"},
        )
        self.assertTrue(all(item["required"] for item in platforms.values()))
        self.assertEqual(platforms["macos-arm64"]["g0_state"], "verified")
        for platform in ("linux-x64", "windows-x64", "android-arm64"):
            self.assertEqual(platforms[platform]["g0_state"], "not run")

    def test_environment_and_artifact_contract(self) -> None:
        environment = self.contract["environment"]
        self.assertEqual(environment["g0_host"], "macos-arm64")
        self.assertFalse(environment["privileged_impairment_required"])
        self.assertIn("naive", environment["required_release_targets"])
        self.assertIn(".pcap", self.contract["forbidden_artifact_suffixes"])
        self.assertIn(".key", self.contract["forbidden_artifact_suffixes"])
        self.assertEqual(
            set(self.contract["duration_tiers"]),
            {"smoke", "qualification", "extended"},
        )

    def test_payload_candidate_matches_ipv6_minimum_contract(self) -> None:
        payload = self.contract["g1_payload_candidate"]
        self.assertEqual(payload["measured_live_ceiling_bytes"], 1314)
        self.assertEqual(payload["candidate_safe_payload_bytes"], 1200)
        self.assertEqual(payload["ipv6_minimum_pmtu_bytes"], 1280)
        self.assertEqual(payload["ipv6_udp_payload_ceiling_bytes"], 1232)
        self.assertLessEqual(
            payload["candidate_safe_payload_bytes"],
            payload["ipv6_udp_payload_ceiling_bytes"],
        )
        self.assertIn("pending", payload["status"])

    def test_network_profiles_are_named_seeded_and_privacy_bounded(self) -> None:
        document = json.loads(NETWORK_PROFILES.read_text(encoding="utf-8"))
        self.assertEqual(document["schema"], 1)
        profiles = document["profiles"]
        self.assertEqual(
            [profile["id"] for profile in profiles],
            ["delay", "loss", "reorder", "bandwidth", "combined"],
        )
        self.assertEqual(len({profile["seed"] for profile in profiles}), 5)
        for profile in profiles:
            self.assertGreater(profile["seed"], 0)
        self.assertTrue(
            set(document["committed_evidence_fields"]).isdisjoint(
                document["forbidden_evidence_fields"]
            )
        )
        self.assertIn("payload", document["forbidden_evidence_fields"])

    def test_soak_tiers_preserve_real_qualification_duration_and_limits(self) -> None:
        document = json.loads(SOAK_TIERS.read_text(encoding="utf-8"))
        self.assertEqual(document["schema"], 1)
        self.assertEqual(document["tiers"]["smoke"], 60)
        self.assertGreaterEqual(document["tiers"]["qualification"], 3600)
        self.assertGreater(
            document["tiers"]["extended"], document["tiers"]["qualification"]
        )
        self.assertEqual(document["client_association_cap"], 256)
        self.assertEqual(document["server_per_client_cap"], 32)
        self.assertLessEqual(document["wave_size"], 32)


if __name__ == "__main__":
    unittest.main()
