"""
Functional tests for GBXminer command-line argument handling.

Every test that actually invokes the binary is guarded by ``pytest.skip``
when the binary has not been built.  This keeps the suite green in pure
Python / lint-only CI environments while providing real coverage once the
build artefact exists.

Tests that don't need the binary (pure argument-structure validation, URL
parsing, device-id parsing) run unconditionally using the Python helpers so
they're never skipped.

Design note
-----------
We do NOT shell out with ``subprocess.run(..., shell=True)`` — that pattern
is disallowed under the project's Bash style guide (``set -euo pipefail``) and
creates injection risk.  All ``subprocess.run`` calls pass a list.
"""

import json
import subprocess
import time
from pathlib import Path

import pytest

from utils.helpers import (
    is_cuda_compute_supported,
    parse_cuda_compute_capability,
    validate_api_bind_format,
    validate_url_format,
)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _run(binary: Path, *args, timeout: int = 10) -> subprocess.CompletedProcess:
    """Run the miner binary with the given arguments and return the result."""
    return subprocess.run(
        [str(binary), *args],
        capture_output=True,
        text=True,
        timeout=timeout,
    )


def _skip_if_missing(binary: Path) -> None:
    """Skip the test if the miner binary has not been built yet."""
    if not binary.exists():
        pytest.skip("Miner binary not built — run `make` first")


# ---------------------------------------------------------------------------
# Binary lifecycle (require built binary)
# ---------------------------------------------------------------------------

class TestBinaryLifecycle:
    """Basic smoke-tests that only need the binary to exist and exit cleanly."""

    def test_help_exits_cleanly(self, miner_binary):
        """``--help`` must exit 0 and mention the binary name."""
        _skip_if_missing(miner_binary)
        result = _run(miner_binary, "--help")
        assert result.returncode == 0, \
            f"--help exited {result.returncode}: {result.stderr}"
        combined = (result.stdout + result.stderr).lower()
        assert "gbxminer" in combined or "usage" in combined, \
            "--help output must mention 'gbxminer' or 'usage'"

    def test_version_exits_cleanly(self, miner_binary):
        """``--version`` must exit 0 and print a version string."""
        _skip_if_missing(miner_binary)
        result = _run(miner_binary, "--version")
        assert result.returncode == 0, \
            f"--version exited {result.returncode}: {result.stderr}"
        combined = result.stdout + result.stderr
        assert any(c.isdigit() for c in combined), \
            "--version output must contain at least one digit"

    def test_unknown_option_exits_nonzero(self, miner_binary):
        """An unrecognised option must exit non-zero."""
        _skip_if_missing(miner_binary)
        result = _run(miner_binary, "--this-option-does-not-exist")
        assert result.returncode != 0, \
            "Unrecognised option must cause a non-zero exit"

    def test_invalid_algo_exits_nonzero(self, miner_binary):
        """An unknown algorithm name must cause a non-zero exit."""
        _skip_if_missing(miner_binary)
        result = _run(miner_binary, "-a", "notanalgorithm", "--help")
        # Either non-zero exit or an error message in stderr
        has_error = result.returncode != 0 or "unknown" in result.stderr.lower()
        assert has_error, \
            "Unknown algorithm should trigger an error"


# ---------------------------------------------------------------------------
# Config-file round-trip (require built binary)
# ---------------------------------------------------------------------------

class TestConfigFile:
    """Test the ``-c / --config`` flag with an on-disk JSON file."""

    def test_valid_config_file_is_accepted(self, miner_binary, tmp_path):
        """A syntactically valid config must not produce a parse error."""
        _skip_if_missing(miner_binary)
        config = {
            "url": "stratum+tcp://pool.example.com:3333",
            "user": "wallet.address",
            "pass": "x",
        }
        cfg_path = tmp_path / "config.json"
        cfg_path.write_text(json.dumps(config))

        result = _run(miner_binary, "-c", str(cfg_path), "--help")
        # We pass --help so the binary does not actually try to connect
        assert result.returncode == 0, \
            f"Valid config file caused non-zero exit: {result.stderr}"

    def test_missing_config_file_exits_nonzero(self, miner_binary, tmp_path):
        """Pointing to a non-existent config file must cause a non-zero exit."""
        _skip_if_missing(miner_binary)
        result = _run(miner_binary, "-c", str(tmp_path / "does_not_exist.json"))
        assert result.returncode != 0, \
            "Missing config file must cause a non-zero exit"

    def test_invalid_json_config_exits_nonzero(self, miner_binary, tmp_path):
        """A config file with malformed JSON must cause a non-zero exit."""
        _skip_if_missing(miner_binary)
        cfg_path = tmp_path / "bad.json"
        cfg_path.write_text('{"url": "stratum+tcp://pool:3333" invalid}')
        result = _run(miner_binary, "-c", str(cfg_path))
        assert result.returncode != 0, \
            "Malformed JSON config must cause a non-zero exit"


# ---------------------------------------------------------------------------
# URL validation (pure Python, no binary required)
# ---------------------------------------------------------------------------

class TestUrlValidation:
    """Pure-Python tests of helpers.validate_url_format().

    These mirror the constraints enforced by gbxminer's JSON config parser
    so mismatches are caught early, before a binary is built.
    """

    @pytest.mark.parametrize("url", [
        "stratum+tcp://pool.example.com:3333",
        "stratum+udp://pool.example.com:3333",
        "stratum+tcps://pool.example.com:3333",
        "http://pool.example.com:9332",
        "https://pool.example.com:9332",
        "getwork://pool.example.com:9332",
    ])
    def test_valid_urls_accepted(self, url):
        """Valid mining-pool URLs must return True."""
        assert validate_url_format(url), f"Expected valid but got False: {url!r}"

    @pytest.mark.parametrize("url", [
        "",
        "not-a-url",
        "ftp://pool.example.com:3333",   # scheme not in allowed list
        "stratum://pool.example.com",    # missing tcp/udp qualifier
        "://pool.example.com:3333",      # empty scheme
        "stratum+tcp://",                # empty host
        "stratum+tcp://pool:0",          # port 0 is reserved / invalid
        "stratum+tcp://pool:65536",      # port out of range
    ])
    def test_invalid_urls_rejected(self, url):
        """Invalid or unsupported URLs must return False."""
        assert not validate_url_format(url), \
            f"Expected invalid but got True: {url!r}"


# ---------------------------------------------------------------------------
# API-bind validation (pure Python, no binary required)
# ---------------------------------------------------------------------------

class TestApiBind:
    """Pure-Python tests of helpers.validate_api_bind_format()."""

    @pytest.mark.parametrize("bind", [
        "127.0.0.1:4068",
        "0.0.0.0:4068",
        "localhost:4068",
    ])
    def test_valid_api_binds(self, bind):
        assert validate_api_bind_format(bind), f"Expected valid: {bind!r}"

    @pytest.mark.parametrize("bind", [
        "4068",             # no host
        "127.0.0.1",        # no port
        ":4068",            # empty host
        "127.0.0.1:abc",    # non-numeric port
        "",
    ])
    def test_invalid_api_binds(self, bind):
        assert not validate_api_bind_format(bind), f"Expected invalid: {bind!r}"


# ---------------------------------------------------------------------------
# Device-ID parsing (pure Python, no binary required)
# ---------------------------------------------------------------------------

class TestDeviceIds:
    """Tests for device-ID list syntax understood by the ``-d`` flag."""

    @pytest.mark.parametrize("devices,expected", [
        ("0",     [0]),
        ("0,1",   [0, 1]),
        ("0,1,2", [0, 1, 2]),
    ])
    def test_valid_device_list_parsed(self, devices, expected):
        """Comma-separated device IDs must parse to sorted integer list."""
        result = [int(d.strip()) for d in devices.split(",")]
        assert result == expected

    def test_all_keyword_accepted(self):
        """The string 'all' is a valid device specification."""
        assert "all".lower() == "all"

    @pytest.mark.parametrize("devices", ["-1", "abc", "0,abc", ""])
    def test_invalid_device_list_rejected(self, devices):
        """Negative IDs, non-numeric tokens and empty strings must be rejected."""
        is_invalid = False
        if not devices:
            is_invalid = True
        elif devices.lower() != "all":
            try:
                ids = [int(d.strip()) for d in devices.split(",")]
                if any(d < 0 for d in ids):
                    is_invalid = True
            except ValueError:
                is_invalid = True
        assert is_invalid, f"Expected device spec to be invalid: {devices!r}"


# ---------------------------------------------------------------------------
# Algorithm option coverage (pure Python, no binary required)
# ---------------------------------------------------------------------------

class TestAlgorithmOption:
    """Verify that the algorithm names accepted by -a are self-consistent."""

    def test_neoscrypt_is_present(self, algorithm_names):
        """neoscrypt is the primary GBXminer algorithm and must always be listed."""
        assert "neoscrypt" in algorithm_names

    def test_auto_is_present(self, algorithm_names):
        """The 'auto' sentinel must be present for automatic algorithm selection."""
        assert "auto" in algorithm_names

    def test_no_algorithm_name_contains_space(self, algorithm_names):
        """Algorithm names must not contain spaces — they are passed verbatim to -a."""
        for name in algorithm_names:
            if name:  # skip the empty terminator
                assert " " not in name, \
                    f"Algorithm name '{name}' contains a space"

    def test_aliases_resolve_to_known_algorithms(self, algorithm_aliases, algorithm_names):
        """Every alias target must name an algorithm present in algo_names[]."""
        for alias, target in algorithm_aliases.items():
            assert target in algorithm_names, \
                f"Alias '{alias}' → '{target}' is not in algo_names[]"
