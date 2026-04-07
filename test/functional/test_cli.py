"""
Functional tests for GBXminer CLI argument parsing.

This module tests command-line argument parsing and validation
by running the actual miner binary with various arguments.
"""

import pytest
import subprocess
import sys
from pathlib import Path


class TestCLIHelpVersion:
    """Test help and version output."""

    def test_help_output(self, miner_binary):
        """Test that --help outputs usage information."""
        if not miner_binary.exists():
            pytest.skip("Miner binary not built")

        result = subprocess.run(
            [str(miner_binary), "--help"],
            capture_output=True,
            text=True,
            timeout=10
        )
        assert result.returncode == 0
        output = result.stdout.lower()
        assert "usage" in output or "gbxminer" in output

    def test_short_help_output(self, miner_binary):
        """Test that -h outputs usage information."""
        if not miner_binary.exists():
            pytest.skip("Miner binary not built")

        result = subprocess.run(
            [str(miner_binary), "-h"],
            capture_output=True,
            text=True,
            timeout=10
        )
        assert result.returncode == 0
        output = result.stdout.lower()
        assert "usage" in output or "gbxminer" in output

    def test_version_output(self, miner_binary):
        """Test that --version outputs version information."""
        if not miner_binary.exists():
            pytest.skip("Miner binary not built")

        result = subprocess.run(
            [str(miner_binary), "--version"],
            capture_output=True,
            text=True,
            timeout=10
        )
        assert result.returncode == 0
        assert "gbxminer" in result.stdout.lower()

    def test_short_version_output(self, miner_binary):
        """Test that -V outputs version information."""
        if not miner_binary.exists():
            pytest.skip("Miner binary not built")

        result = subprocess.run(
            [str(miner_binary), "-V"],
            capture_output=True,
            text=True,
            timeout=10
        )
        assert result.returncode == 0
        assert "gbxminer" in result.stdout.lower()


class TestCLIAlgorithmOptions:
    """Test algorithm selection options."""

    @pytest.mark.parametrize("algo", [
        "neoscrypt", "x11", "lyra2v2", "blake", "blake2b",
        "sha256d", "scrypt", "cryptonight", "equihash",
    ])
    def test_algo_option_valid(self, algo, miner_binary):
        """Test valid algorithm selection."""
        if not miner_binary.exists():
            pytest.skip("Miner binary not built")

        # Use benchmark mode with time limit to test algo selection
        result = subprocess.run(
            [str(miner_binary), "-a", algo, "--benchmark", "--time-limit=1"],
            capture_output=True,
            text=True,
            timeout=30
        )
        # Should not fail due to unknown algorithm
        # (may fail due to no GPU, but not due to algo)
        if result.returncode != 0:
            assert "unknown algorithm" not in result.stderr.lower()

    @pytest.mark.parametrize("algo", [
        "longoptionname",
        "--algo",
    ])
    def test_algo_option_invalid(self, algo, miner_binary):
        """Test invalid algorithm names are rejected."""
        if not miner_binary.exists():
            pytest.skip("Miner binary not built")

        result = subprocess.run(
            [str(miner_binary), "-a", algo, "--benchmark", "--time-limit=1"],
            capture_output=True,
            text=True,
            timeout=10
        )
        # Should fail or show error about unknown algorithm
        assert result.returncode != 0 or "unknown algorithm" in result.stderr.lower()

    def test_algo_long_form(self, miner_binary):
        """Test --algo long form option."""
        if not miner_binary.exists():
            pytest.skip("Miner binary not built")

        result = subprocess.run(
            [str(miner_binary), "--algo", "neoscrypt", "--benchmark", "--time-limit=1"],
            capture_output=True,
            text=True,
            timeout=30
        )
        if result.returncode != 0:
            assert "unknown algorithm" not in result.stderr.lower()


class TestCLIUrlOptions:
    """Test URL option handling."""

    @pytest.mark.parametrize("url", [
        "stratum+tcp://pool.example.com:3333",
        "stratum+udp://pool.example.com:3333",
        "http://pool.example.com:9332",
        "https://pool.example.com:9332",
    ])
    def test_url_option_valid_formats(self, url, miner_binary):
        """Test valid URL format options."""
        if not miner_binary.exists():
            pytest.skip("Miner binary not built")

        # Test with benchmark mode - URL won't be used but will be parsed
        result = subprocess.run(
            [str(miner_binary), "-o", url, "-u", "test", "-p", "test",
             "--benchmark", "--time-limit=1"],
            capture_output=True,
            text=True,
            timeout=10
        )
        # Should not fail due to URL parsing
        if result.returncode != 0:
            assert "invalid url" not in result.stderr.lower()

    def test_url_long_form(self, miner_binary):
        """Test --url long form option."""
        if not miner_binary.exists():
            pytest.skip("Miner binary not built")

        result = subprocess.run(
            [str(miner_binary), "--url", "stratum+tcp://pool.example.com:3333",
             "-u", "test", "-p", "test", "--benchmark", "--time-limit=1"],
            capture_output=True,
            text=True,
            timeout=10
        )
        if result.returncode != 0:
            assert "invalid url" not in result.stderr.lower()


class TestCLIDeviceOptions:
    """Test device selection options."""

    @pytest.mark.parametrize("devices", ["0", "0,1", "all"])
    def test_device_option_valid(self, devices, miner_binary):
        """Test valid device selection options."""
        if not miner_binary.exists():
            pytest.skip("Miner binary not built")

        result = subprocess.run(
            [str(miner_binary), "-d", devices, "--help"],
            capture_output=True,
            text=True,
            timeout=10
        )
        # Should not fail due to device parsing
        assert result.returncode == 0

    def test_device_long_form(self, miner_binary):
        """Test --devices long form option."""
        if not miner_binary.exists():
            pytest.skip("Miner binary not built")

        result = subprocess.run(
            [str(miner_binary), "--devices", "0", "--help"],
            capture_output=True,
            text=True,
            timeout=10
        )
        assert result.returncode == 0


class TestCLIIntensityOptions:
    """Test intensity option handling."""

    @pytest.mark.parametrize("intensity", ["0", "10", "20", "30"])
    def test_intensity_option_valid(self, intensity, miner_binary):
        """Test valid intensity values."""
        if not miner_binary.exists():
            pytest.skip("Miner binary not built")

        result = subprocess.run(
            [str(miner_binary), "-l", intensity, "--help"],
            capture_output=True,
            text=True,
            timeout=10
        )
        assert result.returncode == 0

    def test_intensity_long_form(self, miner_binary):
        """Test --intensity long form option."""
        if not miner_binary.exists():
            pytest.skip("Miner binary not built")

        result = subprocess.run(
            [str(miner_binary), "--intensity", "20", "--help"],
            capture_output=True,
            text=True,
            timeout=10
        )
        assert result.returncode == 0


class TestCLIConfigFileOption:
    """Test configuration file option."""

    def test_config_file_option(self, miner_binary, tmp_path):
        """Test configuration file option."""
        if not miner_binary.exists():
            pytest.skip("Miner binary not built")

        config_file = tmp_path / "config.json"
        config_file.write_text(
            '{"url": "stratum+tcp://pool:3333", "user": "u", "pass": "p"}'
        )

        result = subprocess.run(
            [str(miner_binary), "-c", str(config_file), "--help"],
            capture_output=True,
            text=True,
            timeout=10
        )
        # Should not error on config file parsing
        assert result.returncode == 0

    def test_config_file_long_form(self, miner_binary, tmp_path):
        """Test --config long form option."""
        if not miner_binary.exists():
            pytest.skip("Miner binary not built")

        config_file = tmp_path / "config.json"
        config_file.write_text(
            '{"url": "stratum+tcp://pool:3333", "user": "u", "pass": "p"}'
        )

        result = subprocess.run(
            [str(miner_binary), "--config", str(config_file), "--help"],
            capture_output=True,
            text=True,
            timeout=10
        )
        assert result.returncode == 0

    def test_config_file_not_found(self, miner_binary, tmp_path):
        """Test handling of missing config file."""
        if not miner_binary.exists():
            pytest.skip("Miner binary not built")

        missing_file = tmp_path / "nonexistent.json"

        result = subprocess.run(
            [str(miner_binary), "-c", str(missing_file)],
            capture_output=True,
            text=True,
            timeout=10
        )
        # Should fail with file not found error
        assert result.returncode != 0


class TestCLIBenchmarkMode:
    """Test benchmark mode options."""

    def test_benchmark_mode(self, miner_binary):
        """Test benchmark mode activation."""
        if not miner_binary.exists():
            pytest.skip("Miner binary not built")

        result = subprocess.run(
            [str(miner_binary), "--benchmark", "-a", "neoscrypt", "--time-limit=1"],
            capture_output=True,
            text=True,
            timeout=30
        )
        # Should run in benchmark mode (may fail without GPU, but not due to args)
        if result.returncode != 0:
            # Check it's not an argument error
            assert "unknown option" not in result.stderr.lower()
            assert "invalid argument" not in result.stderr.lower()

    def test_time_limit_option(self, miner_binary):
        """Test time limit option."""
        if not miner_binary.exists():
            pytest.skip("Miner binary not built")

        result = subprocess.run(
            [str(miner_binary), "--benchmark", "-a", "neoscrypt", "--time-limit=2"],
            capture_output=True,
            text=True,
            timeout=30
        )
        if result.returncode != 0:
            assert "unknown option" not in result.stderr.lower()


class TestCLIAPIOptions:
    """Test API bind options."""

    @pytest.mark.parametrize("bind", [
        "127.0.0.1:4068",
        "0.0.0.0:4068",
        "localhost:4068",
    ])
    def test_api_bind_valid(self, bind, miner_binary):
        """Test valid API bind options."""
        if not miner_binary.exists():
            pytest.skip("Miner binary not built")

        result = subprocess.run(
            [str(miner_binary), "-b", bind, "--help"],
            capture_output=True,
            text=True,
            timeout=10
        )
        assert result.returncode == 0

    def test_api_bind_long_form(self, miner_binary):
        """Test --api-bind long form option."""
        if not miner_binary.exists():
            pytest.skip("Miner binary not built")

        result = subprocess.run(
            [str(miner_binary), "--api-bind", "127.0.0.1:4068", "--help"],
            capture_output=True,
            text=True,
            timeout=10
        )
        assert result.returncode == 0


class TestCLIDebugOptions:
    """Test debug option flags."""

    def test_debug_short_form(self, miner_binary):
        """Test -D debug option."""
        if not miner_binary.exists():
            pytest.skip("Miner binary not built")

        result = subprocess.run(
            [str(miner_binary), "-D", "--help"],
            capture_output=True,
            text=True,
            timeout=10
        )
        assert result.returncode == 0

    def test_debug_long_form(self, miner_binary):
        """Test --debug option."""
        if not miner_binary.exists():
            pytest.skip("Miner binary not built")

        result = subprocess.run(
            [str(miner_binary), "--debug", "--help"],
            capture_output=True,
            text=True,
            timeout=10
        )
        assert result.returncode == 0


class TestCLIInvalidOptions:
    """Test invalid option handling."""

    def test_invalid_option(self, miner_binary):
        """Test that invalid options are rejected."""
        if not miner_binary.exists():
            pytest.skip("Miner binary not built")

        result = subprocess.run(
            [str(miner_binary), "--invalid-option-xyz"],
            capture_output=True,
            text=True,
            timeout=10
        )
        assert result.returncode != 0

    def test_unknown_option(self, miner_binary):
        """Test that unknown options are rejected."""
        if not miner_binary.exists():
            pytest.skip("Miner binary not built")

        result = subprocess.run(
            [str(miner_binary), "-z"],
            capture_output=True,
            text=True,
            timeout=10
        )
        assert result.returncode != 0
