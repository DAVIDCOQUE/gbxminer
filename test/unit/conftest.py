"""
Unit test specific fixtures for GBXminer.

This module provides fixtures that are only used by unit tests,
complementing the shared fixtures in test/conftest.py.
"""

import pytest


@pytest.fixture
def swab32_test_cases():
    """Return test cases for swab32 function."""
    return [
        (0x00000001, 0x01000000),
        (0x01020304, 0x04030201),
        (0xFFFFFFFF, 0xFFFFFFFF),
        (0x00000000, 0x00000000),
        (0x12345678, 0x78563412),
        (0xABCDEF01, 0x01EFCDAB),
        (0x80000000, 0x00000080),
        (0x00000080, 0x80000000),
    ]


@pytest.fixture
def swab64_test_cases():
    """Return test cases for swab64 function."""
    return [
        (0x0000000000000001, 0x0100000000000000),
        (0x0102030405060708, 0x0807060504030201),
        (0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF),
        (0x0000000000000000, 0x0000000000000000),
        (0x123456789ABCDEF0, 0xF0DEBC9A78563412),
        (0x8000000000000000, 0x0000000000000080),
    ]


@pytest.fixture
def hex_conversion_test_cases():
    """Return test cases for hex conversion functions."""
    return {
        "hex_to_bytes": [
            ("00", b'\x00'),
            ("ff", b'\xff'),
            ("010203", b'\x01\x02\x03'),
            ("deadbeef", b'\xde\xad\xbe\xef'),
            ("0123456789abcdef", b'\x01\x23\x45\x67\x89\xab\xcd\xef'),
        ],
        "bytes_to_hex": [
            (b'\x00', "00"),
            (b'\xff', "ff"),
            (b'\x01\x02\x03', "010203"),
            (b'\xde\xad\xbe\xef', "deadbeef"),
        ],
    }


@pytest.fixture
def difficulty_test_cases():
    """Return test cases for difficulty calculation."""
    max_target = 0x00000000FFFF0000000000000000000000000000000000000000000000000000
    return [
        (max_target, 1.0),
        (max_target // 2, 2.0),
        (max_target // 10, 10.0),
        (max_target // 100, 100.0),
    ]


@pytest.fixture
def hash_comparison_test_cases():
    """Return test cases for hash comparison (share validation)."""
    target = "00000000FFFF0000000000000000000000000000000000000000000000000000"
    return [
        # (hash_hex, target_hex, expected_valid)
        ("0000000000000000000000000000000000000000000000000000000000000001", target, True),
        ("00000000FFFF0000000000000000000000000000000000000000000000000000", target, False),
        ("00000000FFFE0000000000000000000000000000000000000000000000000000", target, True),
        ("0000000100000000000000000000000000000000000000000000000000000000", target, False),
        ("0000000000000000000000000000000000000000000000000000000000000000", target, True),
    ]


@pytest.fixture
def valid_intensity_values():
    """Return valid intensity values."""
    return [0, 10, 20, 30]


@pytest.fixture
def invalid_intensity_values():
    """Return invalid intensity values."""
    return [-1, 100]


@pytest.fixture
def config_validation_cases():
    """Return configuration validation test cases."""
    return {
        "valid_minimal": {
            "url": "stratum+tcp://pool.example.com:3333",
            "user": "wallet.address",
            "pass": "password"
        },
        "valid_full": {
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
        },
    }
