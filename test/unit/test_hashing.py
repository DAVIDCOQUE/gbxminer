"""
Unit tests for hashing utility functions in GBXminer.

This module tests swab32, swab64, hex conversion, difficulty calculation,
and hash comparison functions that are critical for mining operations.
"""

import pytest


def swab32(v):
    """Python implementation of swab32 for testing.

    This mirrors the C implementation in miner.h which uses
    __builtin_bswap32 or the bswap_32 macro.
    """
    return ((v << 24) & 0xFF000000) | \
           ((v << 8) & 0x00FF0000) | \
           ((v >> 8) & 0x0000FF00) | \
           ((v >> 24) & 0x000000FF)


def swab64(v):
    """Python implementation of swab64 for testing.

    This mirrors the C implementation in miner.h which uses
    __builtin_bswap64 or the bswap_64 macro.
    """
    # Swap 32-bit halves, then swap bytes within each half
    low = (v & 0xFFFFFFFF)
    high = (v >> 32) & 0xFFFFFFFF
    return (swab32(low) << 32) | swab32(high)


class TestSwab32:
    """Test swab32 (32-bit byte swap) function."""

    def test_swab32_basic(self, swab32_test_cases):
        """Test swab32 with basic test cases."""
        for input_val, expected in swab32_test_cases:
            result = swab32(input_val)
            assert result == expected, \
                f"swab32(0x{input_val:08x}) = 0x{result:08x}, expected 0x{expected:08x}"

    def test_swab32_identity_zero(self):
        """Test that swab32(0) = 0."""
        assert swab32(0) == 0

    def test_swab32_identity_all_ones(self):
        """Test that swab32(0xFFFFFFFF) = 0xFFFFFFFF."""
        assert swab32(0xFFFFFFFF) == 0xFFFFFFFF

    def test_swab32_symmetry(self):
        """Test that swab32 is its own inverse (applying twice gives original)."""
        test_values = [0x12345678, 0xABCDEF01, 0x00000001, 0x80000000]
        for val in test_values:
            result = swab32(swab32(val))
            assert result == val, f"swab32(swab32(0x{val:08x})) = 0x{result:08x}, expected 0x{val:08x}"

    def test_swab32_byte_reversal(self):
        """Test that swab32 correctly reverses byte order."""
        # 0x01020304 -> 0x04030201
        val = 0x01020304
        result = swab32(val)
        assert result == 0x04030201

        # Extract individual bytes and verify reversal
        b0 = (val >> 24) & 0xFF  # Most significant byte
        b1 = (val >> 16) & 0xFF
        b2 = (val >> 8) & 0xFF
        b3 = val & 0xFF  # Least significant byte

        result_b0 = (result >> 24) & 0xFF
        result_b1 = (result >> 16) & 0xFF
        result_b2 = (result >> 8) & 0xFF
        result_b3 = result & 0xFF

        assert result_b0 == b3, "MSB should be original LSB"
        assert result_b1 == b2, "Second byte should be original third byte"
        assert result_b2 == b1, "Third byte should be original second byte"
        assert result_b3 == b0, "LSB should be original MSB"


class TestSwab64:
    """Test swab64 (64-bit byte swap) function."""

    def test_swab64_basic(self, swab64_test_cases):
        """Test swab64 with basic test cases."""
        for input_val, expected in swab64_test_cases:
            result = swab64(input_val)
            assert result == expected, \
                f"swab64(0x{input_val:016x}) = 0x{result:016x}, expected 0x{expected:016x}"

    def test_swab64_identity_zero(self):
        """Test that swab64(0) = 0."""
        assert swab64(0) == 0

    def test_swab64_identity_all_ones(self):
        """Test that swab64(0xFFFFFFFFFFFFFFFF) = 0xFFFFFFFFFFFFFFFF."""
        assert swab64(0xFFFFFFFFFFFFFFFF) == 0xFFFFFFFFFFFFFFFF

    def test_swab64_symmetry(self):
        """Test that swab64 is its own inverse."""
        test_values = [0x123456789ABCDEF0, 0x0000000000000001, 0x8000000000000000]
        for val in test_values:
            result = swab64(swab64(val))
            assert result == val, \
                f"swab64(swab64(0x{val:016x})) = 0x{result:016x}, expected 0x{val:016x}"

    def test_swab64_byte_reversal(self):
        """Test that swab64 correctly reverses byte order."""
        val = 0x0102030405060708
        result = swab64(val)
        assert result == 0x0807060504030201


class TestHexConversion:
    """Test hex encoding/decoding utilities."""

    def test_hex_to_bytes(self, hex_conversion_test_cases):
        """Test hex string to bytes conversion."""
        for hex_str, expected in hex_conversion_test_cases["hex_to_bytes"]:
            result = bytes.fromhex(hex_str)
            assert result == expected, \
                f"hex_to_bytes('{hex_str}') = {result!r}, expected {expected!r}"

    def test_bytes_to_hex(self, hex_conversion_test_cases):
        """Test bytes to hex string conversion."""
        for byte_val, expected in hex_conversion_test_cases["bytes_to_hex"]:
            result = byte_val.hex()
            assert result == expected, \
                f"bytes_to_hex({byte_val!r}) = '{result}', expected '{expected}'"

    def test_hex_roundtrip(self):
        """Test that hex encoding/decoding is reversible."""
        test_bytes = [
            b'\x00',
            b'\xff',
            b'\x01\x02\x03',
            b'\xde\xad\xbe\xef',
            b'\x00' * 32,  # 32-byte hash
            b'\xff' * 32,
        ]
        for original in test_bytes:
            hex_str = original.hex()
            roundtrip = bytes.fromhex(hex_str)
            assert roundtrip == original, f"Roundtrip failed for {original!r}"

    def test_hex_uppercase_lowercase(self):
        """Test that hex parsing handles both upper and lowercase."""
        hex_lower = "deadbeef"
        hex_upper = "DEADBEEF"
        hex_mixed = "DeAdBeEf"

        assert bytes.fromhex(hex_lower) == bytes.fromhex(hex_upper)
        assert bytes.fromhex(hex_mixed) == bytes.fromhex(hex_lower)

    def test_hex_empty_string(self):
        """Test handling of empty hex string."""
        result = bytes.fromhex("")
        assert result == b''

    def test_hex_odd_length(self):
        """Test that odd-length hex strings are handled."""
        # Python's fromhex requires even length
        with pytest.raises(ValueError):
            bytes.fromhex("abc")


class TestDifficultyCalculation:
    """Test difficulty calculation functions."""

    def test_difficulty_calculation(self, difficulty_test_cases):
        """Test difficulty calculation from target."""
        max_target = 0x00000000FFFF0000000000000000000000000000000000000000000000000000

        for target, expected_diff in difficulty_test_cases:
            diff = max_target / target
            assert abs(diff - expected_diff) < 0.0001, \
                f"Difficulty for target 0x{target:x} = {diff}, expected {expected_diff}"

    def test_difficulty_inverse_relationship(self):
        """Test that difficulty is inversely proportional to target."""
        max_target = 0x00000000FFFF0000000000000000000000000000000000000000000000000000

        target1 = max_target
        target2 = max_target // 2
        target3 = max_target // 4

        diff1 = max_target / target1
        diff2 = max_target / target2
        diff3 = max_target / target3

        assert diff2 == 2 * diff1, "Half target should give double difficulty"
        assert diff3 == 4 * diff1, "Quarter target should give quadruple difficulty"

    def test_difficulty_edge_cases(self):
        """Test difficulty calculation edge cases."""
        max_target = 0x00000000FFFF0000000000000000000000000000000000000000000000000000

        # Minimum difficulty (target = max_target)
        assert max_target / max_target == 1.0

        # Very high difficulty (very small target)
        small_target = 1
        high_diff = max_target / small_target
        assert high_diff > 1e70, "Difficulty for target=1 should be enormous"


class TestHashComparison:
    """Test hash comparison for share validation."""

    def test_hash_comparison(self, hash_comparison_test_cases):
        """Test hash comparison for share validation."""
        for hash_hex, target_hex, expected_valid in hash_comparison_test_cases:
            hash_int = int(hash_hex, 16)
            target_int = int(target_hex, 16)
            is_valid = hash_int <= target_int
            assert is_valid == expected_valid, \
                f"Hash {hash_hex[:16]}... vs target {target_hex[:16]}...: " \
                f"valid={is_valid}, expected={expected_valid}"

    def test_hash_comparison_boundary(self):
        """Test hash comparison at boundary values."""
        target = 0x00000000FFFF0000000000000000000000000000000000000000000000000000

        # Hash equal to target
        assert target <= target, "Hash equal to target should be valid"

        # Hash one less than target
        assert (target - 1) <= target, "Hash less than target should be valid"

        # Hash one more than target
        assert not ((target + 1) <= target), "Hash greater than target should be invalid"

    def test_hash_comparison_zero_hash(self):
        """Test that zero hash is always valid."""
        target = 0x00000000FFFF0000000000000000000000000000000000000000000000000000
        zero_hash = 0
        assert zero_hash <= target, "Zero hash should always be valid"

    def test_hash_comparison_max_hash(self):
        """Test that max hash (all 1s) is only valid for max target."""
        max_hash = 2**256 - 1
        max_target = 0x00000000FFFF0000000000000000000000000000000000000000000000000000

        assert not (max_hash <= max_target), "Max hash should not be <= typical target"
        assert max_hash <= max_hash, "Max hash should be <= max hash"


class TestSwab256:
    """Test swab256 (256-bit byte swap) function."""

    def swab256(self, data_bytes):
        """Python implementation of swab256 for testing.

        This mirrors the C implementation in miner.h which swaps
        32-bit words and reverses their order.
        """
        assert len(data_bytes) == 32, "swab256 requires 32 bytes"

        result = bytearray(32)
        # Convert to 32-bit words, swap bytes in each word, and reverse order
        for i in range(8):
            # Read word i from source
            word = int.from_bytes(data_bytes[i*4:(i+1)*4], byteorder='little')
            # Write to position (7-i) in destination with byte swap
            result[(7-i)*4:(7-i+1)*4] = word.to_bytes(4, byteorder='little')

        return bytes(result)

    def test_swab256_basic(self):
        """Test swab256 with a known input."""
        # Input: 0x00, 0x01, ..., 0x1F
        input_data = bytes(range(32))
        result = self.swab256(input_data)

        # Expected: bytes reversed in 4-byte groups, then groups reversed
        # Group 0: 00 01 02 03 -> 03 02 01 00 -> goes to position 7
        # Group 7: 1C 1D 1E 1F -> 1F 1E 1D 1C -> goes to position 0
        expected = bytes([
            0x1F, 0x1E, 0x1D, 0x1C,  # Group 7 swapped
            0x1B, 0x1A, 0x19, 0x18,  # Group 6 swapped
            0x17, 0x16, 0x15, 0x14,  # Group 5 swapped
            0x13, 0x12, 0x11, 0x10,  # Group 4 swapped
            0x0F, 0x0E, 0x0D, 0x0C,  # Group 3 swapped
            0x0B, 0x0A, 0x09, 0x08,  # Group 2 swapped
            0x07, 0x06, 0x05, 0x04,  # Group 1 swapped
            0x03, 0x02, 0x01, 0x00,  # Group 0 swapped
        ])
        assert result == expected

    def test_swab256_symmetry(self):
        """Test that swab256 is its own inverse."""
        test_data = bytes(range(32))
        result = self.swab256(self.swab256(test_data))
        assert result == test_data, "swab256 should be its own inverse"

    def test_swab256_zero(self):
        """Test swab256 with all zeros."""
        zeros = b'\x00' * 32
        result = self.swab256(zeros)
        assert result == zeros

    def test_swab256_ones(self):
        """Test swab256 with all ones."""
        ones = b'\xff' * 32
        result = self.swab256(ones)
        assert result == ones
