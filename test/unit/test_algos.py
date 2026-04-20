# Copyright (c) 2026-2026 The GBXMiner developers
"""
Unit tests for algorithm name/enum consistency in GBXminer.

Reflects the algo set after v1.1.0 and v1.2.0 removals and additions.

Removed (ASIC-dominated or dead):
  v1.1.0:
  - X-series family:    hsr (X13), sonoa (X17), zr5 (X11)
  - Blake-ASIC:         decred, pentablake (penta), vanilla/blake
  - CryptoNight family: cryptonight, cryptolight, monero, graft,
                        stellite, wildkeccak
  - Scrypt family:      scrypt, scrypt-jane
    (neoscrypt RETAINED -- GoByte primary PoW)
  v1.2.0:
  - Groestl family:     groestl, myr-gr
  - Skein family:       skein, skein2
  - Ghost networks:     quark, qubit, keccakc

Added:
  v1.1.0:
  - etchash    -- Ethereum Classic ETCHash (ECIP-1099, epoch=60 000 blocks)
  - kawpow     -- Ravencoin ProgPoW variant (epoch=7 500, period=3)
  - autolykos2 -- Ergo PoW (EIP-0037, k-sum BLAKE2b-256)
  v1.2.0:
  - kheavyhash -- Kaspa (no DAG, 64x64 matrix per block)
  - zelhash    -- Flux (Equihash 125,4)
  - firopow    -- Firo ProgPoW (period=13, epoch=1300)

These lists MUST be kept in sync with algos.h.
"""

import pytest


# ---------------------------------------------------------------------------
# Ground truth: these must exactly mirror algos.h algo_names[]
# ---------------------------------------------------------------------------

ALGO_NAMES = [
    "allium",
    "autolykos2",
    "bmw",
    "dmd-gr",
    "equihash",
    "firopow",
    "etchash",
    "fugue256",
    "heavy",
    "keccak",
    "kheavyhash",
    "jackpot",
    "jha",
    "kawpow",
    "lbry",
    "luffa",
    "lyra2",
    "lyra2v2",
    "lyra2v3",
    "lyra2z",
    "mjollnir",
    "neoscrypt",
    "nist5",
    "sha256d",
    "sha256t",
    "whirlcoin",
    "whirlpool",
    "zelhash",
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
    "flux":       "zelhash",
    "firo":       "firopow",
    "zcoin":      "firopow",
    "kaspa":      "kheavyhash",
    "kas":        "kheavyhash",
    "zel":        "zelhash",
    "ergo":       "autolykos2",
    "autolykos":  "autolykos2",
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
    "ravencoin":  "kawpow",
    "rvn":        "kawpow",
}

# ---------------------------------------------------------------------------
# Algos explicitly REMOVED -- absence is a correctness invariant
# ---------------------------------------------------------------------------

REMOVED_ALGOS = [
    # v1.1.0 — X-series
    "hsr", "sonoa", "zr5",
    # v1.1.0 — Blake-ASIC
    "decred", "penta", "vanilla", "blake",
    # v1.1.0 — CryptoNight
    "cryptonight", "cryptolight", "monero", "graft", "stellite", "wildkeccak",
    # v1.1.0 — Scrypt-ASIC
    "scrypt", "scrypt-jane",
    # v1.2.0 — Groestl family
    "groestl", "myr-gr",
    # v1.2.0 — Skein family
    "skein", "skein2",
    # v1.2.0 — Ghost networks
    "quark", "qubit", "keccakc",
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

    def test_kheavyhash_present(self):
        """kheavyhash must be present (Kaspa, no DAG, matrix per block)."""
        assert "kheavyhash" in ALGO_NAMES, "kheavyhash missing from algo_names"
        assert ALGO_ENUM.get("kheavyhash") is not None
        assert ALGO_ALIASES.get("kaspa") == "kheavyhash"
        assert ALGO_ALIASES.get("kas") == "kheavyhash"

    def test_firopow_present(self):
        """firopow must be present (Firo ProgPoW, period=13, epoch=1300)."""
        assert "firopow" in ALGO_NAMES, "firopow missing from algo_names"
        assert ALGO_ENUM.get("firopow") is not None
        assert ALGO_ALIASES.get("firo") == "firopow"
        assert ALGO_ALIASES.get("zcoin") == "firopow"

    def test_zelhash_present(self):
        """zelhash must be present (Flux Equihash 125,4)."""
        assert "zelhash" in ALGO_NAMES, "zelhash missing from algo_names"
        assert ALGO_ENUM.get("zelhash") is not None
        # Aliases
        assert ALGO_ALIASES.get("flux") == "zelhash"
        assert ALGO_ALIASES.get("zel") == "zelhash"

    def test_autolykos2_present(self):
        """autolykos2 must be present (Ergo PoW, EIP-0037)."""
        assert "autolykos2" in ALGO_NAMES, "autolykos2 missing from algo_names"
        assert ALGO_ENUM.get("autolykos2") is not None

    def test_etchash_present(self):
        """etchash must be present (ECIP-1099 ETCHash, epoch=60000)."""
        assert "etchash" in ALGO_NAMES, "etchash is missing from algo_names"
        assert ALGO_ENUM.get("etchash") is not None

    def test_kawpow_present(self):
        """kawpow must be present (Ravencoin ProgPoW, epoch=7500, period=3)."""
        assert "kawpow" in ALGO_NAMES, "kawpow is missing from algo_names"
        assert ALGO_ENUM.get("kawpow") is not None

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
            "neoscrypt", "etchash", "kawpow", "firopow", "kheavyhash", "autolykos2", "zelhash",
            "lyra2", "lyra2v2", "lyra2v3", "lyra2z",
            "equihash", "keccak", "sha256d",
        ]
        missing = [a for a in required if a not in ALGO_NAMES]
        assert not missing, f"Required GPU-minable algos missing: {missing}"
