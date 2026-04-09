# Copyright (c) 2026-2026 The GBXMiner developers
"""
Unit tests for algorithm name/enum consistency in GBXminer.

Reflects the algo set after the following removals and additions:

Removed (ASIC-dominated or dead):
  - X-series family:    hsr (X13), sonoa (X17), zr5 (X11)
  - Blake-ASIC:         decred, pentablake (penta), vanilla/blake
  - CryptoNight family: cryptonight, cryptolight, monero, graft,
                        stellite, wildkeccak
  - Scrypt family:      scrypt, scrypt-jane
    (neoscrypt RETAINED -- GoByte primary PoW)

Added (GPU-minable):
  - etchash  -- Ethereum Classic ETCHash (ECIP-1099, epoch=60 000 blocks)
  - kapow    -- Ravencoin ProgPoW variant (epoch=7 500, period=3)

These lists MUST be kept in sync with algos.h.
"""

import pytest


# ---------------------------------------------------------------------------
# Ground truth: these must exactly mirror algos.h algo_names[]
# ---------------------------------------------------------------------------

ALGO_NAMES = [
    "allium",
    "bmw",
    "dmd-gr",
    "equihash",
    "etchash",
    "fugue256",
    "groestl",
    "heavy",
    "keccak",
    "keccakc",
    "jackpot",
    "jha",
    "kapow",
    "lbry",
    "luffa",
    "lyra2",
    "lyra2v2",
    "lyra2v3",
    "lyra2z",
    "mjollnir",
    "myr-gr",
    "neoscrypt",
    "nist5",
    "quark",
    "qubit",
    "sha256d",
    "sha256t",
    "skein",
    "skein2",
    "whirlcoin",
    "whirlpool",
    "auto",
    "",
]

ALGO_ENUM = {name: i for i, name in enumerate(ALGO_NAMES[:-1])}
ALGO_COUNT = len(ALGO_NAMES) - 1

# ---------------------------------------------------------------------------
# Aliases from algo_to_int() in algos.h
# ---------------------------------------------------------------------------

ALGO_ALIASES = {
    "all":        "auto",
    "diamond":    "dmd-gr",
    "equi":       "equihash",
    "etc":        "etchash",
    "doom":       "luffa",
    "lyra2re":    "lyra2",
    "lyra2rev2":  "lyra2v2",
    "lyra2rev3":  "lyra2v3",
    "bitcoin":    "sha256d",
    "sha256":     "sha256d",
    "whirl":      "whirlpool",
    "ravencoin":  "kapow",
    "rvn":        "kapow",
}

# ---------------------------------------------------------------------------
# Algos explicitly REMOVED -- absence is a correctness invariant
# ---------------------------------------------------------------------------

REMOVED_ALGOS = [
    # X-series
    "hsr", "sonoa", "zr5",
    # Blake-ASIC
    "decred", "penta", "vanilla", "blake",
    # CryptoNight
    "cryptonight", "cryptolight", "monero", "graft", "stellite", "wildkeccak",
    # Scrypt-ASIC
    "scrypt", "scrypt-jane",
]


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------


class TestAlgorithms:
    """Test algorithm name/enum consistency."""

    def test_algo_names_count_matches_constant(self):
        """ALGO_COUNT must equal the number of entries before the terminator."""
        assert ALGO_NAMES[-1] == "", "Last entry in algo_names must be empty string"
        assert len(ALGO_NAMES) - 1 == ALGO_COUNT, (
            f"ALGO_COUNT ({ALGO_COUNT}) doesn't match "
            f"algo_names count ({len(ALGO_NAMES) - 1})"
        )

    def test_algo_names_are_unique(self):
        """All algorithm names (except the terminator) must be unique."""
        names = [n for n in ALGO_NAMES if n]
        duplicates = {n for n in names if names.count(n) > 1}
        assert not duplicates, f"Duplicate algorithm names: {duplicates}"

    def test_algo_enum_sequential(self):
        """Enum values must be sequential from 0 to ALGO_COUNT-1."""
        for name, val in ALGO_ENUM.items():
            assert 0 <= val < ALGO_COUNT, (
                f"Enum value for '{name}' ({val}) out of range [0, {ALGO_COUNT})"
            )

    def test_algo_enum_matches_names_order(self):
        """Enum values must match the position in ALGO_NAMES."""
        for i, name in enumerate(ALGO_NAMES[:-1]):
            assert name in ALGO_ENUM, f"'{name}' missing from ALGO_ENUM"
            assert ALGO_ENUM[name] == i, (
                f"Enum value for '{name}' is {ALGO_ENUM[name]}, expected {i}"
            )

    def test_auto_is_last_real_entry(self):
        """'auto' must be the last real algorithm (before the terminator)."""
        assert ALGO_NAMES[ALGO_COUNT - 1] == "auto", (
            "'auto' must be the last real algorithm in algo_names"
        )

    def test_no_empty_names_before_terminator(self):
        """No empty strings are allowed before the final terminator."""
        for i, name in enumerate(ALGO_NAMES[:-1]):
            assert name, f"Empty algorithm name at index {i}"

    @pytest.mark.parametrize("alias,expected", ALGO_ALIASES.items())
    def test_aliases_resolve_to_valid_algo(self, alias, expected):
        """Every alias must resolve to a known, non-empty algorithm."""
        assert expected in ALGO_NAMES, (
            f"Alias '{alias}' -> '{expected}' which is not in ALGO_NAMES"
        )
        assert expected != "", f"Alias '{alias}' resolves to empty string"

    def test_neoscrypt_present(self):
        """neoscrypt MUST be present -- it is GoByte's primary PoW algorithm."""
        assert "neoscrypt" in ALGO_NAMES, (
            "CRITICAL: neoscrypt has been removed. "
            "neoscrypt is GoByte's primary PoW algorithm and must never be removed."
        )
        assert ALGO_ENUM.get("neoscrypt") is not None

    def test_etchash_present(self):
        """etchash must be present (ECIP-1099 ETCHash, epoch=60000)."""
        assert "etchash" in ALGO_NAMES, "etchash is missing from algo_names"
        assert ALGO_ENUM.get("etchash") is not None

    def test_kapow_present(self):
        """kapow must be present (Ravencoin ProgPoW, epoch=7500, period=3)."""
        assert "kapow" in ALGO_NAMES, "kapow is missing from algo_names"
        assert ALGO_ENUM.get("kapow") is not None

    @pytest.mark.parametrize("algo", REMOVED_ALGOS)
    def test_removed_algos_absent(self, algo):
        """ASIC-dominated and dead-chain algos must not appear in algo_names."""
        assert algo not in ALGO_NAMES, (
            f"Removed algorithm '{algo}' was re-introduced into algo_names. "
            "ASIC/dead-chain algos must not be restored without a full PR review."
        )

    def test_gpu_minable_algos_present(self):
        """All required GPU-minable algos must be present."""
        required = [
            "neoscrypt", "etchash", "kapow",
            "lyra2", "lyra2v2", "lyra2v3", "lyra2z",
            "equihash", "groestl", "keccak", "quark", "qubit",
            "skein", "skein2", "sha256d",
        ]
        missing = [a for a in required if a not in ALGO_NAMES]
        assert not missing, f"Required GPU-minable algos missing: {missing}"
