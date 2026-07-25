#!/usr/bin/env python3

import json
import os
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
G4_RUNNER = pathlib.Path(__file__).with_name("g4_sanitizer_fuzz.sh")
G5_MACOS_RUNNER = pathlib.Path(__file__).with_name("g5_macos_qualification.sh")
G5_LINUX_RUNNER = pathlib.Path(__file__).with_name("g5_linux_qualification.sh")
G5_WINDOWS_RUNNER = pathlib.Path(__file__).with_name("g5_windows_qualification.sh")
G5_ANDROID_BUILD = pathlib.Path(__file__).with_name("g5_android_build.sh")
M6_CADDYFILE = pathlib.Path(__file__).with_name("Caddyfile")
G5_CROSS_PLATFORM = pathlib.Path(__file__).with_name(
    "g5_cross_platform_interop.sh"
)
G5_LIMA = pathlib.Path(__file__).with_name("lima-g5f.yaml")
M5_SHIPPED_PRODUCT = pathlib.Path(__file__).parents[1] / "m5" / "g5_production_binary.sh"
M5_WINDOWS_TRUSTED_LEAF = (
    pathlib.Path(__file__).parents[1] / "m5" / "windows_trusted_leaf.ps1"
)
G5_WINDOWS_TRUST_PREFLIGHT = pathlib.Path(__file__).with_name(
    "g5_windows_trust_preflight.sh"
)
G5_WORKFLOW = pathlib.Path(__file__).parents[2] / ".github" / "workflows" / "m6-platform-qualification.yml"
CADDY_DIR = pathlib.Path(
    os.environ.get("M6_CADDY_DIR", "/path/to/caddy-naive-udp-m4")
)


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

    def test_m6_server_runtime_fix_is_pinned(self) -> None:
        self.assertEqual(
            self.contract["inputs"]["forwardproxy_m6_runtime"],
            "baa7f2dd0845aa4cb55e39b4cc67c9b6a59b6285",
        )
        g0 = (CONTRACT.parent / "g0_contract.sh").read_text(encoding="utf-8")
        self.assertIn("expected_forwardproxy_m6_runtime", g0)
        self.assertIn("baa7f2dd0845aa4cb55e39b4cc67c9b6a59b6285", g0)
        self.assertEqual(
            self.contract["inputs"]["forwardproxy_m6_qualification"],
            "f14924cdedc93c28a2b92c8120538ea5beee28fb",
        )
        self.assertIn("expected_forwardproxy_m6_qualification", g0)
        self.assertIn("f14924cdedc93c28a2b92c8120538ea5beee28fb", g0)

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

    def test_g4_server_race_uses_current_caddy_worktree(self) -> None:
        runner = G4_RUNNER.read_text(encoding="utf-8")
        self.assertIn("caddy_dir=", runner)
        self.assertIn("dd9a89c11194dcb806d845233995ef040f096464", runner)
        self.assertIn('"$go_bin" mod edit -modfile="$tmp_dir/go.mod"', runner)
        self.assertIn('-modfile="$tmp_dir/go.mod" -race', runner)

    def test_caddy_tls_module_race_fix_is_pinned(self) -> None:
        source = (CADDY_DIR / "modules/caddytls/tls.go").read_text(
            encoding="utf-8"
        )
        self.assertIn("caddy.RegisterModule(new(TLS))", source)
        self.assertIn("func (*TLS) CaddyModule() caddy.ModuleInfo", source)
        self.assertNotIn("func (TLS) CaddyModule() caddy.ModuleInfo", source)

    def test_g1b2_shipped_runner_preserves_default_verifier_and_cleanup(self) -> None:
        runner = G1_SHIPPED_CEILING.read_text(encoding="utf-8")
        self.assertIn('naive_bin="$repo_dir/src/out/Release/naive"', runner)
        self.assertIn("M6_G1B2_UNTRUSTED_CERT_REJECTED_OK", runner)
        self.assertIn("M6_G1B2_NEGATIVE_ONLY_OK", runner)
        self.assertIn("security add-trusted-cert", runner)
        self.assertGreaterEqual(runner.count("security remove-trusted-cert"), 2)
        self.assertIn("temporary G1b2 root remained trusted", runner)
        self.assertIn("root certificate remained in keychain", runner)
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

    def test_g5b_macos_runner_is_explicit_and_fail_closed(self) -> None:
        runner = G5_MACOS_RUNNER.read_text(encoding="utf-8")
        self.assertIn('test "$(uname -s)" = Darwin', runner)
        self.assertIn('test "$(uname -m)" = arm64', runner)
        self.assertIn("M6_G5_NEGATIVE_ONLY", runner)
        self.assertIn('test "${M6_G5_TEMPORARY_TRUST_AUTHORIZED:-0}" = 1', runner)
        self.assertIn("M5_G5_STOP_AFTER_NEGATIVE=1", runner)
        self.assertIn("M6_G5B_MACOS_ARM64_OK", runner)

        product = M5_SHIPPED_PRODUCT.read_text(encoding="utf-8")
        self.assertIn("M5_EXPECTED_FORWARDPROXY", product)
        self.assertIn("M5_EXPECTED_CADDY", product)
        self.assertIn("M5_EXPECTED_CLIENT", product)
        self.assertIn("f14924cdedc93c28a2b92c8120538ea5beee28fb", product)
        self.assertIn("cce894a8a0e987eb1722cf99729499bdaba6c38d", product)
        self.assertIn("ca_fingerprint_sha256", product)
        self.assertIn("-fingerprint -sha256", product)
        self.assertIn("SHA-256 hash: $ca_fingerprint_sha256", product)
        self.assertLess(
            product.index("trust_installed=1\n      security add-trusted-cert"),
            product.index("M5_G5_TRUST_CONFIRMATION_PENDING"),
        )

    def test_g5_desktop_trust_and_linux_ci_are_pinned(self) -> None:
        product = M5_SHIPPED_PRODUCT.read_text(encoding="utf-8")
        windows_trust = M5_WINDOWS_TRUSTED_LEAF.read_text(encoding="utf-8")
        self.assertIn("SSL_CERT_FILE", product)
        self.assertIn("X509Store", windows_trust)
        self.assertIn("StoreLocation]::LocalMachine", windows_trust)
        self.assertIn('"TrustedPeople"', windows_trust)
        self.assertNotIn('"Root"', windows_trust)
        self.assertIn("must be self-signed", windows_trust)
        self.assertIn("run_windows_trust_store Install", product)
        self.assertIn("run_windows_trust_store Check", product)
        self.assertIn("run_windows_trust_store Remove", product)
        self.assertNotIn("certutil", product.lower())
        self.assertIn("M5_G5_TRUST_CLEANUP_OK", product)

        runner = G5_LINUX_RUNNER.read_text(encoding="utf-8")
        self.assertIn('test "$(uname -s)" = Linux', runner)
        self.assertIn("x86_64|amd64", runner)
        self.assertIn("M6_G5C_LINUX_X64_OK", runner)
        self.assertIn("M6_G2_REPETITIONS=1", runner)
        self.assertIn("M6_G3_TIER=smoke", runner)
        self.assertIn(
            "auto_https disable_redirects",
            M6_CADDYFILE.read_text(encoding="utf-8"),
        )

        workflow = G5_WORKFLOW.read_text(encoding="utf-8")
        self.assertIn("runs-on: ubuntu-22.04", workflow)
        self.assertIn("f14924cdedc93c28a2b92c8120538ea5beee28fb", workflow)
        self.assertIn("dd9a89c11194dcb806d845233995ef040f096464", workflow)
        self.assertIn("g5_linux_qualification.sh", workflow)

        windows = G5_WINDOWS_RUNNER.read_text(encoding="utf-8")
        self.assertIn("MINGW*|MSYS*|CYGWIN*", windows)
        self.assertIn("M6_G5D_WINDOWS_X64_OK", windows)
        self.assertIn("M6_G5D_WINDOWS_SHIPPED_CLIENT_OK", windows)
        self.assertNotIn("naive_socks5_udp_test.exe", windows)
        self.assertIn("M5_G4_CONTROL_CLOSE_OK", windows)
        self.assertIn("M6_G5D_PRODUCT_PHASE", windows)
        self.assertIn("M6_G5D_PRODUCT_TIMEOUT", windows)
        product = M5_SHIPPED_PRODUCT.read_text(encoding="utf-8")
        self.assertIn("M5_G5_PHASE_FILE", product)
        self.assertIn("temporary-trust-install", product)
        self.assertIn("temporary-trust-store-check", product)
        self.assertIn("h3_target_host=localhost", product)
        self.assertIn('--target-host="$h3_target_host"', product)
        self.assertIn('>"$tmp_dir/h3-probe.out" 2>&1', product)
        self.assertNotIn("windows_command_pid", product)
        self.assertIn("taskkill.exe /PID", windows)
        self.assertIn("runs-on: windows-2022", workflow)
        self.assertIn("g5_windows_qualification.sh", workflow)
        self.assertIn("windows-x64-preflight", workflow)
        self.assertIn("windows-x64-forwardproxy-tests", workflow)
        self.assertIn("Windows x64 G5d preflight", workflow)
        self.assertIn("M6_G5D_WINDOWS_PREFLIGHT_ONLY_OK", workflow)
        self.assertIn("9b40eeb5cede209143bba47fce3b05060d7e1bce", workflow)
        self.assertIn("M6_G5D_WINDOWS_FORWARDPROXY_TESTS_OK", workflow)
        windows_job = workflow.split("  windows-x64:", 1)[1].split(
            "  android-arm64-build:", 1
        )[0]
        preflight = G5_WINDOWS_TRUST_PREFLIGHT.read_text(encoding="utf-8")
        self.assertIn("M6_G5D_WINDOWS_TRUST_PREFLIGHT_OK", preflight)
        self.assertIn("test_windows_udp_errors.py", preflight)
        self.assertIn("M6_G5D_WINDOWS_UDP_ERROR_SEMANTICS_OK", preflight)
        self.assertIn("windows_trusted_leaf.ps1", preflight)
        self.assertIn("Preflight Windows runtime semantics", windows_job)
        self.assertIn("timeout-minutes: 3", windows_job)
        self.assertIn("test -x out/Release/naive.exe", windows_job)
        self.assertNotIn("naive_socks5_udp_test", windows_job)

        android = G5_ANDROID_BUILD.read_text(encoding="utf-8")
        self.assertIn("Machine:.*AArch64", android)
        self.assertIn("M6_G5E_ANDROID_ARM64_BUILD_READY", android)
        self.assertIn("android-arm64-build", workflow)
        self.assertIn("g5_android_build.sh", workflow)

    def test_g5f_cross_platform_gate_is_pinned(self) -> None:
        runner = G5_CROSS_PLATFORM.read_text(encoding="utf-8")
        lima = G5_LIMA.read_text(encoding="utf-8")
        self.assertEqual(self.contract["inputs"]["g5f_lima"], "2.1.1")
        self.assertEqual(self.contract["inputs"]["g5f_alpine"], "3.23.3")
        self.assertIn("limactl version 2.1.1", runner)
        self.assertIn("M6_G5F_MACOS_CLIENT_LINUX_SERVER_OK", runner)
        self.assertIn("M6_G5F_HTTP3_APPLICATION_OK", runner)
        self.assertIn("M6_G5F_PRIVACY_OK", runner)
        self.assertIn("alpine-3.23.3-aarch64", lima)
        self.assertIn(
            "7a3cdfaefb0cbf3bb6824cd6ae80d6a3e0b0e367609e5fc50c5f714374d31e8d",
            lima,
        )


if __name__ == "__main__":
    unittest.main()
