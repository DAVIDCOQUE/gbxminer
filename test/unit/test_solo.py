# Copyright (c) 2026-2026 The GBXMiner developers
"""
Unit tests for Bitcoin Core solo mining (bitcoin_solo.cpp / solo_script.cpp).

The consensus-critical vectors live in C, in solo_script_selftest(), so that the
binary that mines is the binary that gets verified. These tests drive
`gbxminer --solo-selftest` and assert on its report; they also reimplement the
merkle and BIP34 rules independently, so a bug would have to be made twice in
the same way to slip through.
"""

import re
import subprocess

import pytest


def _run_selftest(binary):
    return subprocess.run([str(binary), "--solo-selftest"],
                          capture_output=True, text=True, timeout=120)


@pytest.fixture(scope="module")
def selftest_output(miner_binary):
    """Output of the in-binary offline vector suite."""
    candidates = [miner_binary, miner_binary.with_suffix(".exe")]
    binary = next((c for c in candidates if c.exists()), None)
    if binary is None:
        pytest.skip("miner binary not built at " + " or ".join(map(str, candidates)))
    return _run_selftest(binary)


class TestSoloSelftest:
    """Drive the C vector suite through the CLI."""

    def test_selftest_exits_clean(self, selftest_output):
        assert selftest_output.returncode == 0, (
            "solo self-test reported failures:\n" + selftest_output.stdout)

    def test_no_failing_vectors(self, selftest_output):
        failures = [l for l in selftest_output.stdout.splitlines()
                    if l.strip().startswith("FAIL")]
        assert not failures, "failing vectors: " + "; ".join(failures)

    @pytest.mark.parametrize("group", [
        "CompactSize", "BIP34 height push", "Address -> scriptPubKey",
        "Merkle root", "Target", "Header serialization", "Coinbase",
    ])
    def test_all_groups_ran(self, selftest_output, group):
        assert group in selftest_output.stdout, f"{group} vectors did not run"

    def test_reports_success(self, selftest_output):
        assert "all offline vectors passed" in selftest_output.stdout


class TestMerkleRules:
    """Independent reimplementation of the rules the C code must follow."""

    @staticmethod
    def _sha256d(b):
        import hashlib
        return hashlib.sha256(hashlib.sha256(b).digest()).digest()

    def _root(self, leaves):
        lvl = list(leaves)
        while len(lvl) > 1:
            if len(lvl) % 2:
                lvl.append(lvl[-1])          # odd levels duplicate the last hash
            lvl = [self._sha256d(lvl[i] + lvl[i + 1])
                   for i in range(0, len(lvl), 2)]
        return lvl[0]

    def test_single_leaf_root_is_the_leaf(self):
        leaf = bytes.fromhex(
            "0e3e2357e806b6cdb1f70b54c3a3a17b6714ee1f0e68bebb44a74b1efd512098")[::-1]
        assert self._root([leaf]) == leaf

    def test_block_170_merkle_root(self):
        """Two real transactions from mainnet block 170."""
        txids = [
            "b1fea52486ce0c62bb442b530a3f0132b826c74e473d1f2c220bfa78111c5082",
            "f4184fc596403b9d638783cf57adfe4c75c605f6356fbc91338530e9831e9e16",
        ]
        leaves = [bytes.fromhex(t)[::-1] for t in txids]
        expected = bytes.fromhex(
            "7dac2c5666815c17a3b36427de37bb9d2e2c5ccec3f8633eb91a4205cb4c10ff")[::-1]
        assert self._root(leaves) == expected

    def test_odd_level_duplicates_last_hash(self):
        a, b, c = (bytes([x]) * 32 for x in (0xaa, 0xbb, 0xcc))
        manual = self._sha256d(self._sha256d(a + b) + self._sha256d(c + c))
        assert self._root([a, b, c]) == manual

    def test_linear_chaining_is_not_the_merkle_root(self):
        """Guards against the stratum-style fold, which is wrong for GBT."""
        a, b, c = (bytes([x]) * 32 for x in (0xaa, 0xbb, 0xcc))
        linear = a
        for nxt in (b, c):
            linear = self._sha256d(linear + nxt)
        assert self._root([a, b, c]) != linear


class TestBip34Height:
    """The height is a minimally encoded script number, never a CompactSize."""

    @staticmethod
    def _push(n):
        tmp = bytearray()
        while n:
            tmp.append(n & 0xff)
            n >>= 8
        if tmp and tmp[-1] & 0x80:
            tmp.append(0x00)
        return bytes([len(tmp)]) + bytes(tmp)

    @pytest.mark.parametrize("height,expected", [
        (1,       "0101"),
        (127,     "017f"),
        (128,     "028000"),
        (255,     "02ff00"),
        (256,     "020001"),
        (32767,   "02ff7f"),
        (32768,   "03008000"),
        (227836,  "03fc7903"),
        (900000,  "03a0bb0d"),
        (8388608, "0400008000"),
    ])
    def test_known_encodings(self, height, expected):
        assert self._push(height).hex() == expected

    @pytest.mark.parametrize("height", [128, 32768, 8388608])
    def test_high_bit_gets_a_padding_byte(self, height):
        """Without the pad the script number would read as negative."""
        enc = self._push(height)
        assert enc[-1] == 0x00

    def test_differs_from_compactsize(self):
        """253 is one byte as a script number but three as a CompactSize."""
        assert self._push(253).hex() == "02fd00"
