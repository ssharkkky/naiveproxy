#!/usr/bin/env python3

import json
import pathlib
import unittest


CONTRACT = pathlib.Path(__file__).with_name("contract.json")
NETWORK_PROFILES = pathlib.Path(__file__).with_name("network_profiles.json")
SOAK_TIERS = pathlib.Path(__file__).with_name("soak_tiers.json")
ASAN_ARGS = pathlib.Path(__file__).with_name("asan_args.gn")
PLATFORM_QUALIFICATION = pathlib.Path(__file__).with_name(
    "platform_qualification.json"
)
G1_SHIPPED_CEILING = pathlib.Path(__file__).with_name("g1_shipped_ceiling.sh")
G1_FINALIZE = pathlib.Path(__file__).with_name("g1_finalize.sh")


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

    def test_sanitizer_configuration_is_fail_closed(self) -> None:
        configuration = ASAN_ARGS.read_text(encoding="utf-8")
        self.assertIn("is_asan = true", configuration)
        self.assertIn("is_ubsan = true", configuration)
        self.assertIn("is_ubsan_no_recover = true", configuration)
        self.assertIn('target_cpu = "arm64"', configuration)

    def test_g4_fuzz_budget_is_frozen(self) -> None:
        budget = self.contract["g4_fuzz_budget"]
        self.assertEqual(budget["client_release_seeds"], [20260720, 9298, 1928])
        self.assertEqual(budget["client_release_iterations_per_seed"], 1000000)
        self.assertEqual(budget["client_sanitizer_seed"], 20260720)
        self.assertEqual(budget["client_sanitizer_iterations"], 1000000)
        self.assertEqual(budget["client_lifecycle_seed"], "0x4d3655494645")
        self.assertEqual(budget["client_lifecycle_iterations"], 2000)
        self.assertEqual(budget["server_fuzz_seconds_per_target"], 30)
        self.assertEqual(budget["server_race_count"], 1)

    def test_g5_platform_records_fail_closed(self) -> None:
        document = json.loads(PLATFORM_QUALIFICATION.read_text(encoding="utf-8"))
        self.assertEqual(document["schema"], 1)
        expected = [item["id"] for item in self.contract["platforms"]]
        self.assertEqual(document["required_platforms"], expected)
        self.assertEqual(
            set(document["allowed_states"]),
            set(self.contract["release_policy"]["allowed_states"]),
        )
        records = document["platforms"]
        self.assertEqual([record["id"] for record in records], expected)
        required = document["required_verified_fields"]
        for record in records:
            self.assertIn(record["state"], document["allowed_states"])
            if record["state"] == "verified":
                for field in required:
                    self.assertTrue(record[field], (record["id"], field))
            else:
                self.assertNotEqual(record["state"], "verified")
        self.assertIn("payload", document["forbidden_evidence_fields"])
        self.assertIn("credential", document["forbidden_evidence_fields"])

    def test_g1b2_shipped_runner_preserves_default_verifier_and_cleanup(self) -> None:
        runner = G1_SHIPPED_CEILING.read_text(encoding="utf-8")
        self.assertIn('naive_bin="$repo_dir/src/out/Release/naive"', runner)
        self.assertIn("M6_G1B2_UNTRUSTED_CERT_REJECTED_OK", runner)
        self.assertIn("M6_G1B2_NEGATIVE_ONLY_OK", runner)
        self.assertIn("security add-trusted-cert", runner)
        self.assertGreaterEqual(runner.count("security remove-trusted-cert"), 2)
        self.assertIn("temporary G1b2 root remained trusted", runner)
        self.assertNotIn("MockCertVerifier", runner)
        self.assertNotIn("ignore-certificate", runner.lower())

    def test_g1d_finalize_requires_three_complete_regression_runs(self) -> None:
        runner = G1_FINALIZE.read_text(encoding="utf-8")
        self.assertIn('while [ "$run" -le 3 ]', runner)
        self.assertIn("g1_shipped_ceiling.sh", runner)
        self.assertIn("g1_live_ceiling.sh", runner)
        self.assertIn("socks5_udp_m3.sh", runner)
        self.assertIn("tests/basic.sh", runner)
        self.assertIn('"$go_bin" test -count=1 ./...', runner)
        self.assertIn("./modules/caddyhttp", runner)
        self.assertIn("M6_G1_PAYLOAD_PMTU_OK", runner)


if __name__ == "__main__":
    unittest.main()
