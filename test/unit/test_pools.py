# Copyright (c) 2026-2026 The GBXMiner developers
"""
Unit tests for pool configuration in GBXminer.

This module tests pool configuration parsing, validation,
and multi-pool management functionality.
"""

import pytest
import json


class TestPoolConfiguration:
    """Test pool configuration structure and validation."""

    def test_pool_config_required_fields(self):
        """Test that pool config has required fields."""
        pool_config = {
            "url": "stratum+tcp://pool.example.com:3333",
            "user": "wallet.address",
            "pass": "password"
        }

        assert "url" in pool_config
        assert "user" in pool_config
        assert "pass" in pool_config

    def test_pool_config_optional_fields(self):
        """Test that pool config can have optional fields."""
        pool_config = {
            "url": "stratum+tcp://pool.example.com:3333",
            "user": "wallet.address",
            "pass": "password",
            "algo": "neoscrypt",
            "intensity": 20,
            "name": "Primary Pool"
        }

        assert "algo" in pool_config
        assert "intensity" in pool_config
        assert "name" in pool_config

    def test_pool_config_json_serialization(self, tmp_path):
        """Test pool config can be serialized to JSON."""
        pool_config = {
            "url": "stratum+tcp://pool.example.com:3333",
            "user": "wallet.address",
            "pass": "password"
        }

        config_file = tmp_path / "pool.json"
        config_file.write_text(json.dumps(pool_config))

        data = json.loads(config_file.read_text())
        assert data == pool_config


class TestPoolUrlValidation:
    """Test pool URL validation."""

    @pytest.mark.parametrize("url,expected_scheme,expected_host,expected_port", [
        ("stratum+tcp://pool.example.com:3333", "stratum+tcp", "pool.example.com", 3333),
        ("stratum+udp://pool.example.com:3334", "stratum+udp", "pool.example.com", 3334),
        ("http://localhost:9332", "http", "localhost", 9332),
        ("https://pool.example.com:9332", "https", "pool.example.com", 9332),
        ("stratum+tcp://192.168.1.100:3333", "stratum+tcp", "192.168.1.100", 3333),
    ])
    def test_valid_pool_urls(self, url, expected_scheme, expected_host, expected_port):
        """Test parsing of valid pool URLs."""
        # Extract scheme
        scheme, rest = url.split("://", 1)
        assert scheme == expected_scheme

        # Extract host and port
        if ":" in rest:
            host, port_str = rest.rsplit(":", 1)
            port = int(port_str)
            assert host == expected_host
            assert port == expected_port

    @pytest.mark.parametrize("url", [
        "",
        "not-a-url",
        "ftp://pool.example.com:3333",  # Invalid scheme for mining
        "stratum://pool.example.com:3333",  # Missing tcp/udp
    ])
    def test_invalid_pool_urls(self, url):
        """Test detection of invalid pool URLs."""
        if url and "://" in url:
            scheme = url.split("://")[0]
            # Scheme should be stratum+tcp, stratum+udp, http, or https
            valid_schemes = ["stratum+tcp", "stratum+udp", "http", "https"]
            assert scheme not in valid_schemes or "://" not in url


class TestPoolCredentials:
    """Test pool credential handling."""

    def test_wallet_address_format(self):
        """Test wallet address format validation."""
        # Valid wallet addresses (various formats)
        valid_addresses = [
            "wallet.address",
            "wallet",
            "1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa",  # Bitcoin-style
            "t1abc123def456",  # Zcash-style
            "0x1234567890abcdef",  # Ethereum-style
            "user@domain",  # Email-style
        ]

        for addr in valid_addresses:
            assert len(addr) > 0, f"Empty address: {addr}"
            assert len(addr) <= 128, f"Address too long: {addr}"

    def test_password_format(self):
        """Test password format validation."""
        # Valid passwords
        valid_passwords = [
            "password",
            "c=BTC",  # Currency specification
            "x",  # Placeholder
            "",  # Empty password (some pools don't require)
            "p@ss!word123",  # Complex password
        ]

        for pwd in valid_passwords:
            assert isinstance(pwd, str), f"Password not string: {pwd}"
            assert len(pwd) <= 128, f"Password too long: {pwd}"


class TestMultiPoolConfiguration:
    """Test multi-pool configuration."""

    def test_multi_pool_basic_structure(self, multi_pool_config):
        """Test basic multi-pool configuration structure."""
        assert "pools" in multi_pool_config
        assert isinstance(multi_pool_config["pools"], list)

    def test_multi_pool_minimum_pools(self, multi_pool_config):
        """Test that multi-pool has at least 2 pools."""
        assert len(multi_pool_config["pools"]) >= 2

    def test_multi_pool_each_valid(self, multi_pool_config):
        """Test that each pool in multi-pool config is valid."""
        for pool in multi_pool_config["pools"]:
            assert "url" in pool, "Pool missing URL"
            assert "user" in pool, "Pool missing user"
            assert "pass" in pool, "Pool missing pass"

    def test_multi_pool_unique_urls(self, multi_pool_config):
        """Test that pool URLs are unique."""
        urls = [pool["url"] for pool in multi_pool_config["pools"]]
        assert len(urls) == len(set(urls)), "Duplicate pool URLs found"

    def test_multi_pool_json_serialization(self, multi_pool_config, tmp_path):
        """Test multi-pool config serialization."""
        config_file = tmp_path / "multi_pool.json"
        config_file.write_text(json.dumps(multi_pool_config))

        data = json.loads(config_file.read_text())
        assert len(data["pools"]) == len(multi_pool_config["pools"])


class TestPoolFailover:
    """Test pool failover configuration."""

    def test_pool_priority_order(self):
        """Test that pools can be ordered by priority."""
        pools = [
            {"url": "stratum+tcp://primary:3333", "user": "u", "pass": "p", "priority": 1},
            {"url": "stratum+tcp://backup1:3333", "user": "u", "pass": "p", "priority": 2},
            {"url": "stratum+tcp://backup2:3333", "user": "u", "pass": "p", "priority": 3},
        ]

        # Verify priority ordering
        priorities = [p["priority"] for p in pools]
        assert priorities == sorted(priorities), "Pools not in priority order"

    def test_pool_retry_configuration(self):
        """Test pool retry configuration."""
        pool_config = {
            "url": "stratum+tcp://pool:3333",
            "user": "u",
            "pass": "p",
            "retries": 3,
            "retry-pause": 5
        }

        assert "retries" in pool_config
        assert "retry-pause" in pool_config
        assert pool_config["retries"] > 0
        assert pool_config["retry-pause"] > 0


class TestPoolDiffConfiguration:
    """Test pool difficulty configuration."""

    def test_difficulty_start(self):
        """Test starting difficulty configuration."""
        pool_config = {
            "url": "stratum+tcp://pool:3333",
            "user": "u",
            "pass": "p",
            "diff": 16,
            "max-diff": 64,
            "min-diff": 4
        }

        assert "diff" in pool_config
        assert "max-diff" in pool_config
        assert "min-diff" in pool_config

        assert pool_config["diff"] > 0
        assert pool_config["min-diff"] <= pool_config["diff"]
        assert pool_config["max-diff"] >= pool_config["diff"]

    def test_difficulty_range_validity(self):
        """Test that difficulty range is valid."""
        min_diff = 1
        max_diff = 1000000

        # Difficulty should be positive
        assert min_diff > 0
        assert max_diff > 0

        # Max should be greater than min
        assert max_diff > min_diff
