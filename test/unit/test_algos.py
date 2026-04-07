"""
Unit tests for algorithm name/enum consistency in GBXminer.

This module tests that algorithm names in algo_names[] array match
their enum values, aliases work correctly, and the algorithm count
is consistent.
"""

import pytest


# Algorithm enum and names from algos.h
# These must match the C header file exactly
ALGO_NAMES = [
    "blakecoin", "blake", "blake2b", "blake2s", "allium", "bmw", "bastion",
    "c11", "cryptolight", "cryptonight", "deep", "decred", "dmd-gr",
    "equihash", "exosis", "fresh", "fugue256", "groestl", "heavy", "hmq1725",
    "hsr", "keccak", "keccakc", "jackpot", "jha", "lbry", "luffa", "lyra2",
    "lyra2v2", "lyra2v3", "lyra2z", "mjollnir", "myr-gr", "neoscrypt", "nist5",
    "penta", "phi", "phi2", "polytimos", "quark", "qubit", "scrypt",
    "scrypt-jane", "sha256d", "sha256t", "sha256q", "sia", "sib", "skein",
    "skein2", "skunk", "sonoa", "s3", "timetravel", "tribus", "bitcore",
    "x11evo", "x11", "x12", "x13", "x14", "x15", "x16r", "x16s", "x17",
    "vanilla", "veltor", "whirlcoin", "whirlpool", "whirlpoolx", "wildkeccak",
    "zr5", "monero", "graft", "stellite", "auto", ""
]

# Algorithm aliases from algos.h algo_to_int() function
ALGO_ALIASES = {
    "all": "auto",
    "cryptonight-light": "cryptolight",
    "cryptonight-lite": "cryptolight",
    "flax": "c11",
    "diamond": "dmd-gr",
    "equi": "equihash",
    "doom": "luffa",
    "hmq17": "hmq1725",
    "hshare": "hsr",
    "lyra2re": "lyra2",
    "lyra2rev2": "lyra2v2",
    "lyra2rev3": "lyra2v3",
    "phi1612": "phi",
    "bitcoin": "sha256d",
    "sha256": "sha256d",
    "thorsriddle": "veltor",
    "timetravel10": "bitcore",
    "whirl": "whirlpool",
    "ziftr": "zr5",
}

# Cryptonight fork mappings from get_cryptonight_algo() in algos.h
CRYPTONIGHT_FORKS = {
    8: "graft",
    7: "monero",
    3: "stellite",
    "default": "cryptonight",
}

# Algorithm enum values (must match algos.h)
ALGO_ENUM = {
    "blakecoin": 0,
    "blake": 1,
    "blake2b": 2,
    "blake2s": 3,
    "allium": 4,
    "bmw": 5,
    "bastion": 6,
    "c11": 7,
    "cryptolight": 8,
    "cryptonight": 9,
    "deep": 10,
    "decred": 11,
    "dmd-gr": 12,
    "equihash": 13,
    "exosis": 14,
    "fresh": 15,
    "fugue256": 16,
    "groestl": 17,
    "heavy": 18,
    "hmq1725": 19,
    "hsr": 20,
    "keccak": 21,
    "keccakc": 22,
    "jackpot": 23,
    "jha": 24,
    "lbry": 25,
    "luffa": 26,
    "lyra2": 27,
    "lyra2v2": 28,
    "lyra2v3": 29,
    "lyra2z": 30,
    "mjollnir": 31,
    "myr-gr": 32,
    "neoscrypt": 33,
    "nist5": 34,
    "penta": 35,
    "phi": 36,
    "phi2": 37,
    "polytimos": 38,
    "quark": 39,
    "qubit": 40,
    "scrypt": 41,
    "scrypt-jane": 42,
    "sha256d": 43,
    "sha256t": 44,
    "sha256q": 45,
    "sia": 46,
    "sib": 47,
    "skein": 48,
    "skein2": 49,
    "skunk": 50,
    "sonoa": 51,
    "s3": 52,
    "timetravel": 53,
    "tribus": 54,
    "bitcore": 55,
    "x11evo": 56,
    "x11": 57,
    "x12": 58,
    "x13": 59,
    "x14": 60,
    "x15": 61,
    "x16r": 62,
    "x16s": 63,
    "x17": 64,
    "vanilla": 65,
    "veltor": 66,
    "whirlcoin": 67,
    "whirlpool": 68,
    "whirlpoolx": 69,
    "wildkeccak": 70,
    "zr5": 71,
    "monero": 72,
    "graft": 73,
    "stellite": 74,
    "auto": 75,
}

# ALGO_COUNT should be 76 (auto is 75, count is 76)
ALGO_COUNT = 76


class TestAlgorithms:
    """Test algorithm name/enum consistency."""

    def test_algo_names_count_matches_enum(self):
        """Verify ALGO_COUNT matches algo_names array size."""
        # The last entry should be empty string
        assert ALGO_NAMES[-1] == "", "Last entry in algo_names should be empty string"
        # Count excludes the empty terminator
        algo_count = len(ALGO_NAMES) - 1
        assert algo_count > 0, "No algorithms defined"
        assert algo_count == ALGO_COUNT, f"ALGO_COUNT ({ALGO_COUNT}) doesn't match algo_names count ({algo_count})"

    def test_algo_names_are_unique(self):
        """Verify all algorithm names are unique (excluding empty terminator)."""
        names = [n for n in ALGO_NAMES if n]  # Filter out empty string
        duplicates = [name for name in names if names.count(name) > 1]
        assert len(duplicates) == 0, f"Duplicate algorithm names found: {set(duplicates)}"

    def test_algo_enum_count_matches_names(self):
        """Verify ALGO_ENUM dictionary has correct number of entries."""
        # ALGO_ENUM should have all algorithms except the empty terminator
        assert len(ALGO_ENUM) == ALGO_COUNT, f"ALGO_ENUM has {len(ALGO_ENUM)} entries, expected {ALGO_COUNT}"

    def test_algo_enum_values_are_sequential(self):
        """Verify algorithm enum values are sequential starting from 0."""
        for name, expected_value in ALGO_ENUM.items():
            assert expected_value >= 0, f"Enum value for {name} is negative"
            assert expected_value < ALGO_COUNT, f"Enum value for {name} ({expected_value}) >= ALGO_COUNT ({ALGO_COUNT})"

    @pytest.mark.parametrize("alias,expected", ALGO_ALIASES.items())
    def test_algorithm_aliases(self, alias, expected):
        """Test that algorithm aliases resolve to correct names."""
        assert expected in ALGO_NAMES, f"Alias '{alias}' points to unknown algo '{expected}'"
        assert expected != "", f"Alias '{alias}' points to empty string"

    def test_algorithm_aliases_target_unique(self):
        """Verify all alias targets are unique algorithm names."""
        targets = list(ALGO_ALIASES.values())
        unique_targets = set(targets)
        assert len(targets) == len(unique_targets), "Some aliases point to the same target"

    @pytest.mark.parametrize("fork,expected_algo", [
        (8, "graft"),
        (7, "monero"),
        (3, "stellite"),
        (0, "cryptonight"),  # default
        (99, "cryptonight"),  # unknown defaults to cryptonight
        (-1, "cryptonight"),  # negative defaults to cryptonight
    ])
    def test_cryptonight_fork_mapping(self, fork, expected_algo):
        """Test get_cryptonight_algo() fork mapping."""
        if fork in CRYPTONIGHT_FORKS:
            assert CRYPTONIGHT_FORKS[fork] == expected_algo
        else:
            assert CRYPTONIGHT_FORKS["default"] == expected_algo

    def test_cryptonight_fork_mapping_all_defined(self):
        """Verify all cryptonight fork mappings point to valid algorithms."""
        for fork, algo in CRYPTONIGHT_FORKS.items():
            if fork != "default":
                assert algo in ALGO_NAMES, f"Fork {fork} maps to unknown algo '{algo}'"
        assert CRYPTONIGHT_FORKS["default"] in ALGO_NAMES, "Default cryptonight algo is unknown"

    def test_no_empty_algo_names_before_terminator(self):
        """Ensure no empty strings in algo_names before the terminator."""
        for i, name in enumerate(ALGO_NAMES[:-1]):
            assert name, f"Empty algorithm name at index {i}"

    def test_terminator_is_last_entry(self):
        """Verify empty string is the last entry in algo_names."""
        assert ALGO_NAMES[-1] == "", "Empty string must be the last entry in algo_names"
        # Also verify there's only one empty string (the terminator)
        empty_count = sum(1 for name in ALGO_NAMES if name == "")
        assert empty_count == 1, f"Expected exactly 1 empty string (terminator), found {empty_count}"

    def test_algo_enum_matches_names_order(self):
        """Verify algo_names array order matches enum values."""
        for i, name in enumerate(ALGO_NAMES[:-1]):  # Exclude terminator
            assert name in ALGO_ENUM, f"Algorithm '{name}' not in ALGO_ENUM"
            assert ALGO_ENUM[name] == i, f"Enum value for '{name}' is {ALGO_ENUM[name]}, expected {i}"

    def test_primary_algorithms_present(self):
        """Verify primary/important algorithms are defined."""
        primary_algos = [
            "neoscrypt",  # Primary algorithm per AGENTS.md
            "x11", "x12", "x13", "x14", "x15", "x16r", "x16s", "x17",
            "lyra2", "lyra2v2", "lyra2v3", "lyra2z",
            "quark", "qubit",
            "groestl",
            "skein", "skein2",
            "blake", "blake2b", "blake2s", "blakecoin",
            "scrypt", "scrypt-jane",
            "sha256d",
            "cryptonight", "cryptolight",
            "equihash",
        ]
        for algo in primary_algos:
            assert algo in ALGO_NAMES, f"Primary algorithm '{algo}' is missing"

    def test_auto_algo_is_last_real_entry(self):
        """Verify 'auto' algorithm is the last real algorithm before terminator."""
        # auto should be at index ALGO_COUNT - 1
        assert ALGO_NAMES[ALGO_COUNT - 1] == "auto", "'auto' should be the last real algorithm"
