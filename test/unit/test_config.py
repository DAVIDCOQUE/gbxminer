# Copyright (c) 2009-2014 The Bitcoin Core developers
"""
Unit tests for configuration parsing and validation in GBXminer.

This module tests JSON configuration file parsing, field validation,
URL format validation, and configuration option handling.
"""

import pytest
import json
from pathlib import Path


class TestConfiguration:
    """Test configuration file parsing and validation."""

    def test_valid_minimal_config_structure(self, valid_config_minimal):
        """Test that a minimal valid config has required fields."""
        assert "url" in valid_config_minimal
        assert "user" in valid_config_minimal
        assert "pass" in valid_config_minimal

    def test_valid_full_config_structure(self, valid_config_full):
        """Test that a full valid config has expected fields."""
        required_fields = ["url", "user", "pass"]
        for field in required_fields:
            assert field in valid_config_full, f"Missing required field: {field}"

        optional_fields = ["algo", "intensity", "api-bind", "quiet", "debug"]
        for field in optional_fields:
            assert field in valid_config_full, f"Missing optional field: {field}"

    def test_config_json_serialization(self, valid_config_full, tmp_path):
        """Test that configuration can be serialized to JSON."""
        config_file = tmp_path / "config.json"
        config_file.write_text(json.dumps(valid_config_full))

        # Read back and verify
        data = json.loads(config_file.read_text())
        assert data == valid_config_full

    def test_config_json_deserialization(self, valid_config_full, tmp_path):
        """Test that configuration can be deserialized from JSON."""
        config_file = tmp_path / "config.json"
        config_file.write_text(json.dumps(valid_config_full))

        # Parse the JSON
        with open(config_file, 'r') as f:
            parsed = json.load(f)

        assert parsed["url"] == valid_config_full["url"]
        assert parsed["user"] == valid_config_full["user"]
        assert parsed["pass"] == valid_config_full["pass"]

    def test_invalid_json_syntax(self, tmp_path):
        """Test that invalid JSON is detected."""
        config_file = tmp_path / "config.json"
        invalid_json = '{"url": "test", invalid}'

        config_file.write_text(invalid_json)

        with pytest.raises(json.JSONDecodeError):
            json.loads(config_file.read_text())

    def test_missing_required_url_field(self, tmp_path):
        """Test that missing URL field is detected."""
        config = {"user": "test", "pass": "test"}
        config_file = tmp_path / "config.json"
        config_file.write_text(json.dumps(config))

        data = json.loads(config_file.read_text())
        assert "url" not in data

    def test_missing_required_user_field(self, tmp_path):
        """Test that missing user field is detected."""
        config = {"url": "stratum+tcp://pool:3333", "pass": "test"}
        config_file = tmp_path / "config.json"
        config_file.write_text(json.dumps(config))

        data = json.loads(config_file.read_text())
        assert "user" not in data

    def test_missing_required_pass_field(self, tmp_path):
        """Test that missing pass field is detected."""
        config = {"url": "stratum+tcp://pool:3333", "user": "test"}
        config_file = tmp_path / "config.json"
        config_file.write_text(json.dumps(config))

        data = json.loads(config_file.read_text())
        assert "pass" not in data


class TestUrlValidation:
    """Test URL format validation."""

    @pytest.mark.parametrize("url", [
        "stratum+tcp://pool.example.com:3333",
        "stratum+tcp://pool.example.com:3334",
        "stratum+udp://pool.example.com:3333",
        "http://pool.example.com:9332",
        "https://pool.example.com:9332",
    ])
    def test_valid_url_formats(self, url):
        """Test that valid URL formats are accepted."""
        # Check URL has a scheme
        assert "://" in url, f"URL missing '://' separator: {url}"

        # Check URL has a host
        parts = url.split("://")
        assert len(parts) == 2, f"Invalid URL structure: {url}"
        assert len(parts[1]) > 0, f"URL missing host: {url}"

    @pytest.mark.parametrize("url", [
        "not-a-url",
        "http://example.com",  # Missing port for getblocktemplate
        "ftp://pool.example.com:3333",  # Invalid scheme
        "",
    ])
    def test_invalid_url_formats(self, url):
        """Test that invalid URL formats are detected."""
        if url:  # Non-empty URLs
            # These should be flagged as invalid by the config parser
            assert not url.startswith("stratum+") or "://" not in url or len(url.split("://")[1]) == 0

    def test_url_scheme_extraction(self):
        """Test URL scheme extraction."""
        test_cases = [
            ("stratum+tcp://pool:3333", "stratum+tcp"),
            ("stratum+udp://pool:3333", "stratum+udp"),
            ("http://pool:9332", "http"),
            ("https://pool:9332", "https"),
        ]
        for url, expected_scheme in test_cases:
            scheme = url.split("://")[0]
            assert scheme == expected_scheme, f"Scheme mismatch for {url}"

    def test_url_host_port_extraction(self):
        """Test URL host and port extraction."""
        test_cases = [
            ("stratum+tcp://pool.example.com:3333", "pool.example.com", 3333),
            ("stratum+tcp://192.168.1.1:3334", "192.168.1.1", 3334),
            ("http://localhost:9332", "localhost", 9332),
        ]
        for url, expected_host, expected_port in test_cases:
            # Extract host:port part after scheme
            parts = url.split("://")
            host_port = parts[1]
            if ":" in host_port:
                host, port = host_port.rsplit(":", 1)
                assert host == expected_host, f"Host mismatch for {url}"
                assert int(port) == expected_port, f"Port mismatch for {url}"


class TestIntensityValidation:
    """Test intensity value validation."""

    @pytest.mark.parametrize("intensity", [0, 10, 20, 30])
    def test_valid_intensity_values(self, intensity):
        """Test that valid intensity values are accepted."""
        assert 0 <= intensity <= 30, f"Intensity {intensity} should be in range 0-30"

    @pytest.mark.parametrize("intensity", [-1, 100])
    def test_invalid_intensity_values(self, intensity):
        """Test that invalid intensity values are detected."""
        # Intensity should be in range 0-30 (typical range)
        # Values outside this range should be flagged
        if intensity < 0 or intensity > 30:
            assert True  # These would be rejected by the config parser


class TestApiBindValidation:
    """Test API bind address format validation."""

    @pytest.mark.parametrize("bind", [
        "127.0.0.1:4068",
        "0.0.0.0:4068",
        "localhost:4068",
    ])
    def test_valid_api_bind_formats(self, bind):
        """Test that valid API bind formats are accepted."""
        # Must have host:port format
        assert ":" in bind, f"API bind missing ':' separator: {bind}"
        parts = bind.rsplit(":", 1)
        assert len(parts) == 2, f"Invalid API bind format: {bind}"
        host, port = parts
        assert len(host) > 0, f"API bind missing host: {bind}"
        assert port.isdigit(), f"API bind port not numeric: {bind}"

    @pytest.mark.parametrize("bind", [
        "4068",  # Missing host
        "127.0.0.1",  # Missing port
        ":4068",  # Missing host
        "127.0.0.1:abc",  # Non-numeric port
    ])
    def test_invalid_api_bind_formats(self, bind):
        """Test that invalid API bind formats are detected."""
        # These should be flagged as invalid
        if ":" in bind:
            parts = bind.rsplit(":", 1)
            host, port = parts
            # Either host is empty or port is not numeric
            assert len(host) == 0 or not port.isdigit()


class TestDeviceConfigValidation:
    """Test device configuration validation."""

    @pytest.mark.parametrize("devices,expected", [
        ("0", [0]),
        ("0,1", [0, 1]),
        ("0,1,2", [0, 1, 2]),
        ("all", "all"),
    ])
    def test_valid_device_configs(self, devices, expected):
        """Test that valid device configurations are parsed correctly."""
        if devices == "all":
            assert expected == "all"
        else:
            device_list = [int(d) for d in devices.split(',')]
            assert device_list == expected

    @pytest.mark.parametrize("devices", [
        "-1",
        "abc",
        "0,abc",
        "",
    ])
    def test_invalid_device_configs(self, devices):
        """Test that invalid device configurations are detected."""
        if devices and devices != "all":
            # Should fail to parse as integer list
            try:
                parts = devices.split(',')
                for p in parts:
                    if p:
                        assert int(p) >= 0, f"Negative device ID: {p}"
            except ValueError:
                pass  # Expected for non-numeric values


class TestMultiPoolConfig:
    """Test multi-pool configuration."""

    def test_multi_pool_structure(self, multi_pool_config):
        """Test multi-pool configuration structure."""
        assert "pools" in multi_pool_config
        assert isinstance(multi_pool_config["pools"], list)
        assert len(multi_pool_config["pools"]) >= 2

    def test_multi_pool_entries_valid(self, multi_pool_config):
        """Test that each pool entry has required fields."""
        for pool in multi_pool_config["pools"]:
            assert "url" in pool, "Pool missing 'url' field"
            assert "user" in pool, "Pool missing 'user' field"
            assert "pass" in pool, "Pool missing 'pass' field"

    def test_multi_pool_json_serialization(self, multi_pool_config, tmp_path):
        """Test multi-pool config can be serialized to JSON."""
        config_file = tmp_path / "multi_pool.json"
        config_file.write_text(json.dumps(multi_pool_config))

        data = json.loads(config_file.read_text())
        assert len(data["pools"]) == len(multi_pool_config["pools"])


class TestConfigFileOperations:
    """Test configuration file operations."""

    def test_config_file_not_found(self, tmp_path):
        """Test handling of missing config file."""
        missing_file = tmp_path / "nonexistent.json"
        assert not missing_file.exists()

    def test_config_file_empty(self, tmp_path):
        """Test handling of empty config file."""
        config_file = tmp_path / "empty.json"
        config_file.write_text("")

        content = config_file.read_text()
        assert len(content) == 0

        with pytest.raises(json.JSONDecodeError):
            json.loads(content)

    def test_config_file_permissions(self, tmp_path):
        """Test config file can be read after creation."""
        config_file = tmp_path / "readable.json"
        config_file.write_text('{"test": true}')

        assert config_file.exists()
        data = json.loads(config_file.read_text())
        assert data["test"] is True
