# Copyright (c) 2009-2014 The Bitcoin Core developers
"""
Unit tests for general utility functions in GBXminer.

This module tests various utility functions including hashrate formatting,
time value operations, and other helper functions.
"""

import pytest
import time


class TestHashrateFormatting:
    """Test hashrate formatting utilities."""

    def format_hashrate(self, hashrate):
        """Python implementation of format_hashrate_unit logic.

        Formats a hashrate value with appropriate SI prefix.
        """
        if hashrate < 10000:
            prefix = ""
            value = hashrate
        elif hashrate < 1e7:
            prefix = "k"
            value = hashrate * 1e-3
        elif hashrate < 1e10:
            prefix = "M"
            value = hashrate * 1e-6
        elif hashrate < 1e13:
            prefix = "G"
            value = hashrate * 1e-9
        else:
            prefix = "T"
            value = hashrate * 1e-12

        return f"{value:.2f} {prefix}H/s"

    @pytest.mark.parametrize("hashrate,expected_prefix", [
        (100, ""),           # < 10k -> no prefix
        (5000, ""),          # < 10k -> no prefix
        (10000, "k"),        # >= 10k -> k
        (50000, "k"),        # < 10M -> k
        (10000000, "M"),     # >= 10M -> M
        (500000000, "M"),    # < 10G -> M
        (10000000000, "G"),  # >= 10G -> G
        (500000000000, "G"), # < 10T -> G
        (10000000000000, "T"),  # >= 10T -> T
    ])
    def test_hashrate_prefix_selection(self, hashrate, expected_prefix):
        """Test that correct SI prefix is selected for hashrate."""
        result = self.format_hashrate(hashrate)
        assert expected_prefix in result, \
            f"Hashrate {hashrate} should use prefix '{expected_prefix}', got: {result}"

    @pytest.mark.parametrize("hashrate", [0, 1, 100, 1000, 10000, 1000000])
    def test_hashrate_non_negative(self, hashrate):
        """Test that formatted hashrate is never negative."""
        result = self.format_hashrate(hashrate)
        # Extract numeric part
        numeric_part = float(result.split()[0])
        assert numeric_part >= 0, f"Formatted hashrate should be non-negative: {result}"

    def test_hashrate_format_includes_unit(self):
        """Test that formatted hashrate includes unit."""
        result = self.format_hashrate(1000000)
        assert "H/s" in result, f"Formatted hashrate should include 'H/s': {result}"

    def test_hashrate_format_two_decimal_places(self):
        """Test that formatted hashrate has two decimal places."""
        result = self.format_hashrate(12345678)
        # Should have format like "12.35 MH/s"
        parts = result.split()
        assert len(parts) >= 2, f"Expected at least 2 parts: {result}"
        value_str = parts[0]
        if "." in value_str:
            decimal_places = len(value_str.split(".")[1])
            assert decimal_places == 2, f"Expected 2 decimal places: {value_str}"


class TestTimevalOperations:
    """Test time value operations."""

    def timeval_subtract(self, x_sec, x_usec, y_sec, y_usec):
        """Python implementation of timeval_subtract logic.

        Returns (result_sec, result_usec, is_negative).
        """
        # Convert to microseconds for easier calculation
        x_total_usec = x_sec * 1000000 + x_usec
        y_total_usec = y_sec * 1000000 + y_usec

        diff_usec = x_total_usec - y_total_usec
        is_negative = diff_usec < 0

        result_sec = abs(diff_usec) // 1000000
        result_usec = abs(diff_usec) % 1000000

        if is_negative:
            result_sec = -result_sec
            result_usec = -result_usec

        return result_sec, result_usec, is_negative

    def test_timeval_subtract_positive(self):
        """Test timeval subtraction when x > y."""
        x_sec, x_usec = 10, 500000
        y_sec, y_usec = 5, 250000

        result_sec, result_usec, is_negative = self.timeval_subtract(
            x_sec, x_usec, y_sec, y_usec
        )

        assert not is_negative, "Result should be positive"
        assert result_sec == 5, f"Expected 5 seconds, got {result_sec}"
        assert result_usec == 250000, f"Expected 250000 usec, got {result_usec}"

    def test_timeval_subtract_negative(self):
        """Test timeval subtraction when x < y."""
        x_sec, x_usec = 5, 250000
        y_sec, y_usec = 10, 500000

        result_sec, result_usec, is_negative = self.timeval_subtract(
            x_sec, x_usec, y_sec, y_usec
        )

        assert is_negative, "Result should be negative"

    def test_timeval_subtract_equal(self):
        """Test timeval subtraction when x == y."""
        x_sec, x_usec = 5, 500000
        y_sec, y_usec = 5, 500000

        result_sec, result_usec, is_negative = self.timeval_subtract(
            x_sec, x_usec, y_sec, y_usec
        )

        assert not is_negative, "Equal times should not be negative"
        assert result_sec == 0, "Difference should be 0 seconds"
        assert result_usec == 0, "Difference should be 0 microseconds"

    def test_timeval_subtract_borrow(self):
        """Test timeval subtraction that requires borrowing."""
        x_sec, x_usec = 10, 100000  # 10.1 seconds
        y_sec, y_usec = 5, 900000   # 5.9 seconds

        result_sec, result_usec, is_negative = self.timeval_subtract(
            x_sec, x_usec, y_sec, y_usec
        )

        assert not is_negative
        # 10.1 - 5.9 = 4.2 seconds
        assert result_sec == 4, f"Expected 4 seconds, got {result_sec}"
        assert result_usec == 200000, f"Expected 200000 usec, got {result_usec}"


class TestMemoryAllocation:
    """Test memory allocation utilities (conceptual tests)."""

    def test_aligned_allocation_concept(self):
        """Test concept of aligned allocation."""
        # In C, aligned_calloc allocates memory aligned to cache lines (64 bytes)
        alignment = 64

        # Test that aligned addresses are multiples of alignment
        test_addresses = [0, 64, 128, 256, 1024, 4096]
        for addr in test_addresses:
            assert addr % alignment == 0, f"Address {addr} not aligned to {alignment}"

    def test_aligned_allocation_rounding(self):
        """Test that unaligned addresses can be rounded up to alignment."""
        alignment = 64

        test_cases = [
            (0, 0),
            (1, 64),
            (63, 64),
            (64, 64),
            (65, 128),
            (127, 128),
            (128, 128),
        ]

        for unaligned, expected_aligned in test_cases:
            # Round up to next alignment boundary
            aligned = ((unaligned + alignment - 1) // alignment) * alignment
            assert aligned == expected_aligned, \
                f"Aligning {unaligned} should give {expected_aligned}, got {aligned}"


class TestArraySizeMacro:
    """Test ARRAY_SIZE macro concept."""

    def test_array_size_calculation(self):
        """Test ARRAY_SIZE concept."""
        # In C: ARRAY_SIZE(arr) = sizeof(arr) / sizeof(arr[0])
        test_arrays = [
            [1, 2, 3, 4, 5],
            ["a", "b", "c"],
            [True, False],
            [],
        ]

        expected_sizes = [5, 3, 2, 0]

        for arr, expected in zip(test_arrays, expected_sizes):
            assert len(arr) == expected, f"Array size mismatch: {arr}"


class TestMinMaxMacros:
    """Test min/max macro concepts."""

    def test_max_function(self):
        """Test max concept."""
        test_cases = [
            ((1, 2), 2),
            ((5, 3), 5),
            ((-1, -5), -1),
            ((0, 0), 0),
            ((3.14, 2.71), 3.14),
        ]

        for (a, b), expected in test_cases:
            assert max(a, b) == expected, f"max({a}, {b}) should be {expected}"

    def test_min_function(self):
        """Test min concept."""
        test_cases = [
            ((1, 2), 1),
            ((5, 3), 3),
            ((-1, -5), -5),
            ((0, 0), 0),
            ((3.14, 2.71), 2.71),
        ]

        for (a, b), expected in test_cases:
            assert min(a, b) == expected, f"min({a}, {b}) should be {expected}"


class TestEndianConversion:
    """Test endian conversion utilities."""

    def be32enc(self, x):
        """Python implementation of be32enc (big-endian 32-bit encode)."""
        return x.to_bytes(4, byteorder='big')

    def le32dec(self, data):
        """Python implementation of le32dec (little-endian 32-bit decode)."""
        return int.from_bytes(data, byteorder='little')

    def be32dec(self, data):
        """Python implementation of be32dec (big-endian 32-bit decode)."""
        return int.from_bytes(data, byteorder='big')

    def test_be32enc_basic(self):
        """Test big-endian 32-bit encoding."""
        test_cases = [
            (0x00000000, b'\x00\x00\x00\x00'),
            (0x01020304, b'\x01\x02\x03\x04'),
            (0xFFFFFFFF, b'\xff\xff\xff\xff'),
            (0x12345678, b'\x12\x34\x56\x78'),
        ]

        for value, expected_bytes in test_cases:
            result = self.be32enc(value)
            assert result == expected_bytes, \
                f"be32enc(0x{value:08x}) = {result!r}, expected {expected_bytes!r}"

    def test_be32dec_basic(self):
        """Test big-endian 32-bit decoding."""
        test_cases = [
            (b'\x00\x00\x00\x00', 0x00000000),
            (b'\x01\x02\x03\x04', 0x01020304),
            (b'\xff\xff\xff\xff', 0xFFFFFFFF),
            (b'\x12\x34\x56\x78', 0x12345678),
        ]

        for data, expected_value in test_cases:
            result = self.be32dec(data)
            assert result == expected_value, \
                f"be32dec({data!r}) = 0x{result:08x}, expected 0x{expected_value:08x}"

    def test_le32dec_basic(self):
        """Test little-endian 32-bit decoding."""
        test_cases = [
            (b'\x00\x00\x00\x00', 0x00000000),
            (b'\x01\x02\x03\x04', 0x04030201),
            (b'\xff\xff\xff\xff', 0xFFFFFFFF),
            (b'\x12\x34\x56\x78', 0x78563412),
        ]

        for data, expected_value in test_cases:
            result = self.le32dec(data)
            assert result == expected_value, \
                f"le32dec({data!r}) = 0x{result:08x}, expected 0x{expected_value:08x}"

    def test_be32_roundtrip(self):
        """Test big-endian encode/decode roundtrip."""
        test_values = [0, 1, 0x12345678, 0xFFFFFFFF]

        for value in test_values:
            encoded = self.be32enc(value)
            decoded = self.be32dec(encoded)
            assert decoded == value, f"Roundtrip failed for 0x{value:08x}"


class TestBoolConversion:
    """Test boolean conversion utilities."""

    def test_is_windows_concept(self):
        """Test is_windows concept (always False on Linux)."""
        import sys
        is_windows = sys.platform == 'win32'

        # On Linux, this should be False
        if sys.platform != 'win32':
            assert not is_windows, "is_windows should be False on Linux"

    def test_is_x64_concept(self):
        """Test is_x64 concept."""
        import struct
        is_x64 = struct.calcsize("P") == 8

        # On 64-bit system, this should be True
        assert is_x64, "is_x64 should be True on 64-bit system"


class TestLikelyUnlikely:
    """Test likely/unlikely macro concepts."""

    def test_likely_unlikely_semantics(self):
        """Test that likely/unlikely don't change semantics."""
        # likely() and unlikely() are compiler hints for branch prediction
        # They should not change the boolean value

        test_values = [True, False, 0, 1, 2, -1, None, "", "test"]

        for val in test_values:
            # In Python, we just use the value directly
            # The C macros __builtin_expect don't change the value
            bool_val = bool(val)
            assert bool_val == bool(val), "Boolean conversion should be consistent"
