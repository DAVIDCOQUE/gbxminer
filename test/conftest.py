# Copyright (c) 2026-2026 The GBXMiner developers
"""
Shared pytest fixtures for GBXminer test suite.

This module provides common fixtures used across all test categories.
"""

import pytest
import json
import os
import subprocess
import time
from pathlib import Path


@pytest.fixture(scope="session")
def project_root():
    """Return the project root directory."""
    return Path(__file__).parent.parent


@pytest.fixture(scope="session")
def miner_binary(project_root):
    """Return path to the miner binary."""
    return project_root / "gbxminer"


@pytest.fixture
def sample_config():
    """Return a sample configuration dictionary."""
    return {
        "algo": "neoscrypt",
        "url": "stratum+tcp://pool.example.com:3333",
        "user": "wallet.address",
        "pass": "password",
        "intensity": 20,
        "api-bind": "127.0.0.1:4068"
    }


@pytest.fixture
def mock_gpu_info():
    """Return mock GPU information."""
    return {
        "device_id": 0,
        "name": "NVIDIA GeForce RTX 3080",
        "cuda_compute": "8.6",
        "memory": 10240,
        "sm_count": 68
    }


@pytest.fixture
def mock_stratum_job():
    """Return a mock stratum job."""
    return {
        "job_id": "abcd1234",
        "prev_hash": "0000000000000000000000000000000000000000000000000000000000000000",
        "coinbase1": "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff",
        "coinbase2": "ffffffffffffffff0100f2052a010000001976a914000000000000000000000000000000000000000088ac00000000",
        "merkle_branch": [],
        "version": 536870912,
        "nbits": "1a00ffff",
        "ntime": "63f0c0a0",
        "clean_jobs": True
    }


@pytest.fixture
def temp_config_file(tmp_path, sample_config):
    """Create a temporary config file with sample configuration."""
    config_path = tmp_path / "config.json"
    with open(config_path, 'w') as f:
        json.dump(sample_config, f)
    return config_path


@pytest.fixture
def valid_config_minimal():
    """Return a minimal valid configuration."""
    return {
        "url": "stratum+tcp://pool.example.com:3333",
        "user": "wallet.address",
        "pass": "password"
    }


@pytest.fixture
def valid_config_full():
    """Return a full valid configuration."""
    return {
        "algo": "neoscrypt",
        "intensity": 20,
        "api-bind": "127.0.0.1:4068",
        "statsavg": 20,
        "max-log-rate": 60,
        "quiet": False,
        "debug": False,
        "protocol": False,
        "cpu-priority": 3,
        "url": "stratum+tcp://pool.example.com:3333",
        "user": "wallet.address",
        "pass": "c=BTC"
    }


@pytest.fixture
def multi_pool_config():
    """Return a multi-pool configuration."""
    return {
        "pools": [
            {"url": "stratum+tcp://pool1.example.com:3333", "user": "u1", "pass": "p1"},
            {"url": "stratum+tcp://pool2.example.com:3333", "user": "u2", "pass": "p2"}
        ]
    }


@pytest.fixture
def algorithm_names():
    """Return the list of algorithm names — must mirror algos.h algo_names[]."""
    return [
        "allium", "autolykos2", "bmw", "dmd-gr", "equihash",
        "firopow", "etchash", "fugue256", "heavy", "keccak",
        "kheavyhash", "jackpot", "jha", "kawpow", "lbry",
        "luffa", "lyra2", "lyra2v2", "lyra2v3", "lyra2z",
        "mjollnir", "neoscrypt", "nist5", "sha256d", "sha256t",
        "whirlcoin", "whirlpool", "zelhash", "auto", "",
    ]


@pytest.fixture
def algorithm_aliases():
    """Return the algorithm aliases mapping — must mirror algo_to_int() in algos.h."""
    return {
        "all":       "auto",
        "diamond":   "dmd-gr",
        "equi":      "equihash",
        "etc":       "etchash",
        "doom":      "luffa",
        "lyra2re":   "lyra2",
        "lyra2rev2": "lyra2v2",
        "lyra2rev3": "lyra2v3",
        "bitcoin":   "sha256d",
        "sha256":    "sha256d",
        "whirl":     "whirlpool",
        "ravencoin": "kawpow",
        "rvn":       "kawpow",
        "flux":      "zelhash",
        "zel":       "zelhash",
        "kaspa":     "kheavyhash",
        "kas":       "kheavyhash",
        "firo":      "firopow",
        "zcoin":     "firopow",
    }


@pytest.fixture
def cryptonight_forks():
    """Return the cryptonight fork mapping."""
    return {
        8: "graft",
        7: "monero",
        3: "stellite",
        "default": "cryptonight",
    }


@pytest.fixture
def cuda_architectures():
    """Return CUDA architecture information."""
    return {
        "GTX 900": (5, 0),
        "GTX 1000": (6, 1),
        "RTX 2000": (7, 5),
        "RTX 3000": (8, 6),
        "RTX 4000": (8, 9),
        "RTX 5000": (9, 0),
    }


@pytest.fixture
def valid_url_formats():
    """Return a list of valid URL formats."""
    return [
        "stratum+tcp://pool.example.com:3333",
        "stratum+tcp://pool.example.com:3334",
        "stratum+udp://pool.example.com:3333",
        "http://pool.example.com:9332",
        "https://pool.example.com:9332",
    ]


@pytest.fixture
def invalid_url_formats():
    """Return a list of invalid URL formats."""
    return [
        "not-a-url",
        "http://example.com",
        "ftp://pool.example.com:3333",
        "",
    ]


@pytest.fixture
def valid_api_binds():
    """Return a list of valid API bind formats."""
    return [
        "127.0.0.1:4068",
        "0.0.0.0:4068",
        "localhost:4068",
    ]


@pytest.fixture
def invalid_api_binds():
    """Return a list of invalid API bind formats."""
    return [
        "4068",
        "127.0.0.1",
        ":4068",
        "127.0.0.1:abc",
    ]


@pytest.fixture
def valid_device_configs():
    """Return a list of valid device configurations."""
    return ["0", "0,1", "0,1,2", "all"]


@pytest.fixture
def invalid_device_configs():
    """Return a list of invalid device configurations."""
    return ["-1", "abc", "0,abc", ""]
