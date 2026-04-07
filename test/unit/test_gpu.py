# Copyright (c) 2026-2026 The GBXMiner developers
"""
Unit tests for CUDA GPU detection helpers in GBXminer.

These tests verify compute-capability parsing, minimum-architecture
enforcement (sm_50 / Maxwell is the floor after the CUDA 12 migration that
dropped sm_35), and the architecture-name lookup table that maps (major,minor)
tuples to NVIDIA micro-architecture strings.

No GPU hardware is required; all inputs are synthesised in Python, mirroring
the logic that gbxminer performs in cuda_util.cu / miner.h at start-up.
"""

import pytest

from utils.helpers import (
    get_cuda_arch_name,
    is_cuda_compute_supported,
    parse_cuda_compute_capability,
)


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------

@pytest.fixture
def supported_architectures():
    """Return (major, minor, expected_arch_name) triples for every GPU
    generation GBXminer can target after the sm_35 removal.

    The minimum supported SM is 5.0 (Maxwell, GTX 900-series) because CUDA 12
    dropped support for all pre-Maxwell (Kepler / Fermi) devices and all
    GBXminer kernel Makefile rules were updated accordingly.
    """
    return [
        (5, 0, "Maxwell"),   # GTX 750, GTX 900
        (5, 2, "Maxwell"),   # GTX 980 Ti
        (6, 0, "Pascal"),    # GP100 (Tesla P100)
        (6, 1, "Pascal"),    # GTX 1080 Ti
        (6, 2, "Pascal"),    # Jetson TX2
        (7, 0, "Volta"),     # V100
        (7, 2, "Turing"),    # Jetson AGX Xavier
        (7, 5, "Turing"),    # RTX 2080 Ti
        (8, 0, "Ampere"),    # A100
        (8, 6, "Ampere"),    # RTX 3080
        (8, 7, "Ampere"),    # Jetson Orin
        (8, 9, "Ada"),       # RTX 4090
        (9, 0, "Hopper"),    # H100
    ]


@pytest.fixture
def unsupported_architectures():
    """Return (major, minor) pairs for devices below the sm_50 floor."""
    return [
        (2, 0),   # Fermi
        (3, 0),   # Kepler
        (3, 5),   # Kepler (K80)
        (3, 7),   # Kepler (K80 XL)
        (4, 0),   # Intentional gap — never shipped as compute device
    ]


# ---------------------------------------------------------------------------
# parse_cuda_compute_capability
# ---------------------------------------------------------------------------

class TestParseCudaComputeCapability:
    """Tests for parse_cuda_compute_capability(cc_string) → (major, minor)."""

    @pytest.mark.parametrize("cc_string,expected", [
        ("5.0", (5, 0)),
        ("6.1", (6, 1)),
        ("7.5", (7, 5)),
        ("8.6", (8, 6)),
        ("8.9", (8, 9)),
        ("9.0", (9, 0)),
    ])
    def test_valid_cc_strings(self, cc_string, expected):
        """Verify well-formed CC strings parse to correct integer tuples."""
        assert parse_cuda_compute_capability(cc_string) == expected

    @pytest.mark.parametrize("bad_input", [
        "8",           # no dot
        "8.6.1",       # too many components
        "a.b",         # non-numeric
        "",            # empty
        "8.",          # trailing dot
        ".6",          # leading dot
    ])
    def test_invalid_cc_strings_raise_value_error(self, bad_input):
        """Malformed CC strings must raise ValueError, not silently succeed."""
        with pytest.raises(ValueError):
            parse_cuda_compute_capability(bad_input)

    def test_returns_tuple_not_list(self):
        """Return type must be tuple for unpacking stability."""
        result = parse_cuda_compute_capability("8.6")
        assert isinstance(result, tuple), \
            "parse_cuda_compute_capability must return a tuple, not a list"

    def test_components_are_ints(self):
        """Both components must be Python ints, not strings."""
        major, minor = parse_cuda_compute_capability("7.5")
        assert isinstance(major, int)
        assert isinstance(minor, int)


# ---------------------------------------------------------------------------
# is_cuda_compute_supported
# ---------------------------------------------------------------------------

class TestIsCudaComputeSupported:
    """Tests for is_cuda_compute_supported(major, minor) → bool.

    Default floor is sm_50 (5.0) matching the post-CUDA-12 build rules.
    """

    def test_minimum_supported_sm50(self):
        """sm_50 (Maxwell) is the minimum supported SM after CUDA 12 migration."""
        assert is_cuda_compute_supported(5, 0), \
            "sm_50 must be supported — it is the new minimum after sm_35 removal"

    def test_all_supported_architectures(self, supported_architectures):
        """Every listed supported SM must pass the check."""
        for major, minor, arch_name in supported_architectures:
            assert is_cuda_compute_supported(major, minor), \
                f"SM {major}.{minor} ({arch_name}) should be supported"

    def test_all_unsupported_architectures(self, unsupported_architectures):
        """Every SM below 5.0 must fail the check."""
        for major, minor in unsupported_architectures:
            assert not is_cuda_compute_supported(major, minor), \
                f"SM {major}.{minor} is below sm_50 and must not be supported"

    def test_below_minimum_major(self):
        """Any SM whose major < 5 must be unsupported."""
        for major in range(0, 5):
            assert not is_cuda_compute_supported(major, 0), \
                f"SM {major}.0 is below sm_50 minimum"

    def test_custom_minimum_floor(self):
        """The caller can raise the floor (e.g. sm_70 for Volta-only kernels)."""
        assert is_cuda_compute_supported(7, 0, min_major=7, min_minor=0)
        assert not is_cuda_compute_supported(6, 1, min_major=7, min_minor=0)

    def test_same_major_minor_boundary(self):
        """SM exactly equal to the floor must be accepted (inclusive lower-bound)."""
        assert is_cuda_compute_supported(5, 0, min_major=5, min_minor=0)

    def test_same_major_higher_minor(self):
        """Higher minor with same major must be accepted."""
        assert is_cuda_compute_supported(5, 2, min_major=5, min_minor=0)

    def test_same_major_lower_minor_rejected(self):
        """Lower minor with same major must be rejected."""
        assert not is_cuda_compute_supported(5, 0, min_major=5, min_minor=2)


# ---------------------------------------------------------------------------
# get_cuda_arch_name
# ---------------------------------------------------------------------------

class TestGetCudaArchName:
    """Tests for get_cuda_arch_name(major, minor) → str."""

    def test_all_known_architectures(self, supported_architectures):
        """Every well-known SM must resolve to its documented NVIDIA arch name."""
        for major, minor, expected_name in supported_architectures:
            result = get_cuda_arch_name(major, minor)
            assert result == expected_name, \
                f"SM {major}.{minor}: expected '{expected_name}', got '{result}'"

    def test_unknown_sm_returns_fallback_string(self):
        """An unrecognised SM must return a non-empty fallback, never raise."""
        result = get_cuda_arch_name(99, 9)
        assert isinstance(result, str), "Fallback must be a string"
        assert len(result) > 0, "Fallback must not be empty"

    def test_return_type_is_str(self, supported_architectures):
        """Return type must always be str."""
        for major, minor, _ in supported_architectures:
            assert isinstance(get_cuda_arch_name(major, minor), str)


# ---------------------------------------------------------------------------
# Integration: parse → check → name
# ---------------------------------------------------------------------------

class TestCudaCapabilityPipeline:
    """End-to-end: parse a CC string, check support, resolve arch name."""

    @pytest.mark.parametrize("cc_string,expected_supported,expected_arch", [
        ("5.0", True,  "Maxwell"),
        ("8.6", True,  "Ampere"),
        ("3.5", False, None),      # Kepler — dropped in CUDA 12
        ("9.0", True,  "Hopper"),
    ])
    def test_full_pipeline(self, cc_string, expected_supported, expected_arch):
        """Parse CC string → check support → optionally verify arch name."""
        major, minor = parse_cuda_compute_capability(cc_string)
        supported = is_cuda_compute_supported(major, minor)
        assert supported == expected_supported, \
            f"Support check failed for CC {cc_string}"

        if expected_arch is not None:
            arch = get_cuda_arch_name(major, minor)
            assert arch == expected_arch, \
                f"Arch name mismatch for CC {cc_string}: got '{arch}'"
