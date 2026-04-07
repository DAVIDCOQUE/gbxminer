# Copyright (c) 2026-2026 The GBXMiner developers
"""
General test helpers for GBXminer tests.

This module provides utility functions and helpers commonly used
across the test suite.
"""

import json
import os
import tempfile
from pathlib import Path
from typing import Any, Dict, List, Optional, Union


def make_mock_gpu_info(
    device_id: int = 0,
    name: str = "NVIDIA GeForce RTX 3080",
    cuda_compute: str = "8.6",
    memory: int = 10240,
    sm_count: int = 68
) -> Dict[str, Any]:
    """
    Create a mock GPU information dictionary.

    Args:
        device_id: GPU device ID.
        name: GPU name string.
        cuda_compute: CUDA compute capability (e.g., "8.6").
        memory: GPU memory in MB.
        sm_count: Number of streaming multiprocessors.

    Returns:
        Dictionary containing mock GPU information.
    """
    return {
        'device_id': device_id,
        'name': name,
        'cuda_compute': cuda_compute,
        'memory': memory,
        'sm_count': sm_count,
    }


def make_mock_stratum_job(
    job_id: str = "abcd1234",
    prev_hash: str = None,
    coinbase1: str = None,
    coinbase2: str = None,
    merkle_branch: List[str] = None,
    version: int = 536870912,
    nbits: str = "1a00ffff",
    ntime: str = None,
    clean_jobs: bool = True
) -> Dict[str, Any]:
    """
    Create a mock stratum job dictionary.

    Args:
        job_id: Unique job identifier.
        prev_hash: Previous block hash (64 hex chars).
        coinbase1: First part of coinbase transaction.
        coinbase2: Second part of coinbase transaction.
        merkle_branch: List of merkle branch hashes.
        version: Block version.
        nbits: Difficulty target in compact form.
        ntime: Block timestamp.
        clean_jobs: Whether to clean previous jobs.

    Returns:
        Dictionary containing mock stratum job data.
    """
    return {
        'job_id': job_id,
        'prev_hash': prev_hash or '0' * 64,
        'coinbase1': coinbase1 or '01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff',
        'coinbase2': coinbase2 or 'ffffffffffffffff0100f2052a010000001976a914000000000000000000000000000000000000000088ac00000000',
        'merkle_branch': merkle_branch or [],
        'version': version,
        'nbits': nbits,
        'ntime': ntime or '63f0c0a0',
        'clean_jobs': clean_jobs,
    }


def create_temp_config(
    config_data: Dict[str, Any],
    directory: Optional[Union[str, Path]] = None,
    filename: str = "config.json"
) -> Path:
    """
    Create a temporary configuration file.

    Args:
        config_data: Configuration dictionary to write.
        directory: Directory to create file in (uses tempdir if None).
        filename: Name of the configuration file.

    Returns:
        Path to the created configuration file.
    """
    if directory is None:
        temp_dir = Path(tempfile.mkdtemp())
    else:
        temp_dir = Path(directory)
        temp_dir.mkdir(parents=True, exist_ok=True)

    config_path = temp_dir / filename
    with open(config_path, 'w') as f:
        json.dump(config_data, f, indent=2)

    return config_path


def validate_url_format(url: str) -> bool:
    """
    Validate a mining pool URL format.

    Args:
        url: URL string to validate.

    Returns:
        True if the URL format is valid, False otherwise.
    """
    import re

    if not url or not isinstance(url, str):
        return False

    # Valid schemes
    valid_schemes = [
        'stratum+tcp', 'stratum+udp', 'stratum+tcps', 'stratum+udps',
        'http', 'https', 'getwork'
    ]

    # Parse scheme
    scheme_match = re.match(r'^([a-zA-Z][a-zA-Z0-9+.-]*)://', url)
    if not scheme_match:
        return False

    scheme = scheme_match.group(1).lower()
    if scheme not in valid_schemes:
        return False

    # Check for host
    host_pattern = r'^[a-zA-Z][a-zA-Z0-9+.-]*://([^/:]+)(?::(\d+))?'
    match = re.match(host_pattern, url)
    if not match:
        return False

    # Validate port if present
    port = match.group(2)
    if port is not None:
        try:
            port_int = int(port)
            if port_int < 1 or port_int > 65535:
                return False
        except ValueError:
            return False

    return True


def validate_api_bind_format(api_bind: str) -> bool:
    """
    Validate an API bind address format.

    Args:
        api_bind: API bind address string (host:port).

    Returns:
        True if the format is valid, False otherwise.
    """
    import re

    if not api_bind or not isinstance(api_bind, str):
        return False

    # Pattern: host:port
    pattern = r'^([^:]+):(\d+)$'
    match = re.match(pattern, api_bind)

    if not match:
        return False

    try:
        port = int(match.group(2))
        if port < 1 or port > 65535:
            return False
    except ValueError:
        return False

    return True


def swab32(value: int) -> int:
    """
    Perform 32-bit byte swap.

    Args:
        value: 32-bit integer value.

    Returns:
        Byte-swapped 32-bit integer.
    """
    return (
        ((value << 24) & 0xFF000000) |
        ((value << 8) & 0x00FF0000) |
        ((value >> 8) & 0x0000FF00) |
        ((value >> 24) & 0x000000FF)
    ) & 0xFFFFFFFF


def swab64(value: int) -> int:
    """
    Perform 64-bit byte swap.

    Args:
        value: 64-bit integer value.

    Returns:
        Byte-swapped 64-bit integer.
    """
    low = value & 0xFFFFFFFF
    high = (value >> 32) & 0xFFFFFFFF
    return (swab32(low) << 32) | swab32(high)


def hex_to_bytes(hex_str: str) -> bytes:
    """
    Convert hex string to bytes.

    Args:
        hex_str: Hexadecimal string.

    Returns:
        Bytes object.
    """
    return bytes.fromhex(hex_str)


def bytes_to_hex(data: bytes) -> str:
    """
    Convert bytes to hex string.

    Args:
        data: Bytes object.

    Returns:
        Hexadecimal string.
    """
    return data.hex()


def calculate_difficulty(target: int, max_target: int = None) -> float:
    """
    Calculate mining difficulty from target.

    Args:
        target: Current target value.
        max_target: Maximum target (difficulty 1). Defaults to Bitcoin-style max.

    Returns:
        Difficulty as a float.
    """
    if max_target is None:
        # Bitcoin-style max target
        max_target = 0x00000000FFFF0000000000000000000000000000000000000000000000000000

    if target <= 0:
        return float('inf')

    return max_target / target


def is_valid_share(hash_value: int, target: int) -> bool:
    """
    Check if a hash meets the target difficulty.

    Args:
        hash_value: Hash value as integer.
        target: Target value.

    Returns:
        True if hash < target (valid share).
    """
    return hash_value < target


def parse_device_config(devices: Union[str, List[int]]) -> Union[str, List[int]]:
    """
    Parse device configuration.

    Args:
        devices: Device configuration string or list.

    Returns:
        Parsed device configuration.

    Raises:
        ValueError: If configuration is invalid.
    """
    if isinstance(devices, str):
        if devices.lower() == 'all':
            return 'all'

        try:
            device_ids = [int(d.strip()) for d in devices.split(',')]
            if any(d < 0 for d in device_ids):
                raise ValueError("Negative device IDs not allowed")
            return device_ids
        except (ValueError, AttributeError) as e:
            raise ValueError(
                f"Invalid device configuration: {devices}. "
                "Use 'all' or comma-separated device IDs"
            ) from e

    if isinstance(devices, list):
        for d in devices:
            if not isinstance(d, int) or d < 0:
                raise ValueError(f"Invalid device ID: {d}")
        return devices

    raise ValueError(f"Invalid device configuration type: {type(devices).__name__}")


def format_hashrate(hashrate: float) -> str:
    """
    Format hashrate with appropriate units.

    Args:
        hashrate: Hashrate in H/s.

    Returns:
        Formatted hashrate string.
    """
    if hashrate >= 1e12:
        return f"{hashrate / 1e12:.2f} TH/s"
    elif hashrate >= 1e9:
        return f"{hashrate / 1e9:.2f} GH/s"
    elif hashrate >= 1e6:
        return f"{hashrate / 1e6:.2f} MH/s"
    elif hashrate >= 1e3:
        return f"{hashrate / 1e3:.2f} kH/s"
    else:
        return f"{hashrate:.2f} H/s"


def parse_cuda_compute_capability(cc_string: str) -> tuple:
    """
    Parse CUDA compute capability string.

    Args:
        cc_string: Compute capability string (e.g., "8.6").

    Returns:
        Tuple of (major, minor) integers.

    Raises:
        ValueError: If format is invalid.
    """
    parts = cc_string.split('.')
    if len(parts) != 2:
        raise ValueError(f"Invalid compute capability format: {cc_string}")

    try:
        major = int(parts[0])
        minor = int(parts[1])
    except ValueError:
        raise ValueError(f"Invalid compute capability format: {cc_string}")

    return (major, minor)


def is_cuda_compute_supported(major: int, minor: int, min_major: int = 5, min_minor: int = 0) -> bool:
    """
    Check if a CUDA compute capability is supported.

    Args:
        major: Major version number.
        minor: Minor version number.
        min_major: Minimum supported major version.
        min_minor: Minimum supported minor version.

    Returns:
        True if the compute capability is supported.
    """
    if major < min_major:
        return False
    if major == min_major and minor < min_minor:
        return False
    return True


def get_cuda_arch_name(major: int, minor: int) -> str:
    """
    Get the NVIDIA architecture name for a CUDA compute capability.

    Args:
        major: Major version number.
        minor: Minor version number.

    Returns:
        Architecture name string.
    """
    arch_map = {
        (5, 0): "Maxwell",
        (5, 2): "Maxwell",
        (6, 0): "Pascal",
        (6, 1): "Pascal",
        (6, 2): "Pascal",
        (7, 0): "Volta",
        (7, 2): "Turing",
        (7, 5): "Turing",
        (8, 0): "Ampere",
        (8, 6): "Ampere",
        (8, 7): "Ampere",
        (8, 9): "Ada",
        (9, 0): "Hopper",
    }

    return arch_map.get((major, minor), f"Unknown ({major}.{minor})")


def create_sample_config(
    algo: str = "neoscrypt",
    url: str = "stratum+tcp://pool.example.com:3333",
    user: str = "wallet.address",
    password: str = "password",
    intensity: int = 20,
    api_bind: str = "127.0.0.1:4068"
) -> Dict[str, Any]:
    """
    Create a sample miner configuration.

    Args:
        algo: Algorithm name.
        url: Pool URL.
        user: Pool username/wallet.
        password: Pool password.
        intensity: Mining intensity.
        api_bind: API bind address.

    Returns:
        Configuration dictionary.
    """
    return {
        'algo': algo,
        'url': url,
        'user': user,
        'pass': password,
        'intensity': intensity,
        'api-bind': api_bind,
    }


def create_multi_pool_config(pools: List[Dict[str, Any]]) -> Dict[str, Any]:
    """
    Create a multi-pool configuration.

    Args:
        pools: List of pool configuration dictionaries.

    Returns:
        Multi-pool configuration dictionary.
    """
    return {'pools': pools}


class TempFileContext:
    """
    Context manager for temporary files.

    Usage:
        with TempFileContext() as tmp:
            # tmp is a Path to a temporary directory
            config_path = tmp / "config.json"
    """

    def __init__(self, prefix: str = "gbxtest_"):
        """
        Initialize the context manager.

        Args:
            prefix: Prefix for temporary directory name.
        """
        self.prefix = prefix
        self.temp_dir: Optional[Path] = None

    def __enter__(self) -> Path:
        """Create temporary directory."""
        self.temp_dir = Path(tempfile.mkdtemp(prefix=self.prefix))
        return self.temp_dir

    def __exit__(self, exc_type, exc_val, exc_tb) -> None:
        """Clean up temporary directory."""
        if self.temp_dir and self.temp_dir.exists():
            import shutil
            shutil.rmtree(self.temp_dir, ignore_errors=True)
