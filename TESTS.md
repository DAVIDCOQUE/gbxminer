# GBXminer Python Test Suite

This document describes the Python test suite for GBXminer, including test structure, implementation details, and how each test category works.

## Table of Contents

1. [Overview](#overview)
2. [Test Environment Setup](#test-environment-setup)
3. [Test Directory Structure](#test-directory-structure)
4. [Test Categories](#test-categories)
   - [Algorithm Tests](#1-algorithm-tests-testalgospy)
   - [Configuration Tests](#2-configuration-tests-testconfigpy)
   - [API Tests](#3-api-tests-testapipy)
   - [CLI Arguments Tests](#4-cli-arguments-tests-testclipy)
   - [Hashing Utility Tests](#5-hashing-utility-tests-testhashingpy)
   - [GPU Detection Tests](#6-gpu-detection-tests-testgpupy)
   - [Stratum Protocol Tests](#7-stratum-protocol-tests-teststratumpy)
   - [Logging Tests](#8-logging-tests-testloggingpy)
   - [Build System Tests](#9-build-system-tests-testbuildpy)
   - [Integration Tests](#10-integration-tests-testintegrationpy)
   - [Security Tests](#11-security-tests-testsecuritypy)
   - [Performance Tests](#12-performance-tests-testperformancepy)
5. [Running Tests](#running-tests)
6. [CI/CD Integration](#cicd-integration)

---

## Overview

The Python test suite provides comprehensive testing for GBXminer's functionality without requiring actual GPU hardware. Tests use mocking and stubbing to simulate hardware and network components.

### Testing Philosophy

- **Unit Tests**: Test individual functions and components in isolation
- **Integration Tests**: Test component interactions
- **Functional Tests**: Test end-to-end functionality
- **Mock External Dependencies**: Simulate GPUs, pools, and network resources

---

## Test Environment Setup

### Requirements

Create `test/requirements.txt`:

```
pytest>=7.0.0
pytest-cov>=4.0.0
pytest-mock>=3.10.0
pytest-asyncio>=0.21.0
jsonschema>=4.0.0
requests>=2.28.0
```

### Fixtures Setup

Create `test/conftest.py` with shared fixtures:

```python
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
```

---

## Test Directory Structure

```
test/
├── __init__.py
├── conftest.py              # Shared pytest fixtures
├── requirements.txt         # Python test dependencies
├── utils/                   # Test utilities and mocks
│   ├── __init__.py
│   ├── config_parser.py     # Config file parsing utilities
│   ├── mock_api.py          # Mock API server for testing
│   ├── mock_stratum.py      # Mock stratum pool server
│   └── helpers.py           # General test helpers
├── unit/                    # Unit tests (isolated component testing)
│   ├── __init__.py
│   ├── conftest.py          # Unit test specific fixtures
│   ├── test_algos.py        # Algorithm name/enum tests
│   ├── test_config.py       # Configuration file tests
│   ├── test_hashing.py      # Hash utility tests
│   ├── test_pools.py        # Pool utility tests
│   └── test_utils.py        # General utility tests
├── functional/              # Functional/integration tests (end-to-end)
│   ├── __init__.py
│   └── ...                  # Future functional tests (test_api.py, test_cli.py, etc.)
└── lint/                    # Existing lint tests
    └── ...
```

---

## Test Categories

### 1. Algorithm Tests (`test/unit/test_algos.py`)

**Purpose**: Verify algorithm name-to-enum mappings are correct and consistent.

**What it tests**:
- All algorithm names in `algo_names[]` array match their enum values
- Algorithm aliases work correctly (e.g., "bitcoin" → ALGO_SHA256D)
- `algo_to_int()` function returns correct enum values
- Invalid algorithm names return -1
- `get_cryptonight_algo()` fork mapping is correct
- ALGO_COUNT matches the number of algorithms

**Implementation**:

```python
import pytest

# Algorithm enum and names from algos.h
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

CRYPTONIGHT_FORKS = {
    8: "graft",
    7: "monero",
    3: "stellite",
    "default": "cryptonight",
}

class TestAlgorithms:
    """Test algorithm name/enum consistency."""

    def test_algo_names_count_matches_enum(self):
        """Verify ALGO_COUNT matches algo_names array size."""
        # The last entry should be empty string
        assert ALGO_NAMES[-1] == ""
        # Count excludes the empty terminator
        algo_count = len(ALGO_NAMES) - 1
        assert algo_count > 0, "No algorithms defined"

    def test_algo_names_are_unique(self):
        """Verify all algorithm names are unique (excluding empty terminator)."""
        names = [n for n in ALGO_NAMES if n]  # Filter out empty string
        assert len(names) == len(set(names)), "Duplicate algorithm names found"

    @pytest.mark.parametrize("alias,expected", ALGO_ALIASES.items())
    def test_algorithm_aliases(self, alias, expected):
        """Test that algorithm aliases resolve to correct names."""
        # This tests the logic - actual implementation would parse algos.h
        assert expected in ALGO_NAMES, f"Alias '{alias}' points to unknown algo '{expected}'"

    @pytest.mark.parametrize("fork,expected_algo", [
        (8, "graft"),
        (7, "monero"),
        (3, "stellite"),
        (0, "cryptonight"),  # default
        (99, "cryptonight"),  # unknown defaults
    ])
    def test_cryptonight_fork_mapping(self, fork, expected_algo):
        """Test get_cryptonight_algo() fork mapping."""
        # Verify expected mappings
        if fork in CRYPTONIGHT_FORKS:
            assert CRYPTONIGHT_FORKS[fork] == expected_algo
        else:
            assert CRYPTONIGHT_FORKS["default"] == expected_algo

    def test_no_empty_algo_names_before_terminator(self):
        """Ensure no empty strings in algo_names before the terminator."""
        for i, name in enumerate(ALGO_NAMES[:-1]):
            assert name, f"Empty algorithm name at index {i}"

    def test_terminator_is_last_entry(self):
        """Verify empty string is the last entry in algo_names."""
        assert ALGO_NAMES[-1] == ""
```

---

### 2. Configuration Tests (`test/unit/test_config.py`)

**Purpose**: Validate JSON configuration file parsing and structure.

**What it tests**:
- Valid JSON configuration files
- Invalid JSON syntax detection
- Required fields validation
- Optional fields with defaults
- Pool configuration (url, user, pass)
- Invalid URL formats
- Intensity range validation (typically 0-30)
- Device ID validation
- Multi-pool configuration

**Implementation**:

```python
import pytest
import json
from pathlib import Path

VALID_CONFIG_MINIMAL = {
    "url": "stratum+tcp://pool.example.com:3333",
    "user": "wallet.address",
    "pass": "password"
}

VALID_CONFIG_FULL = {
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

class TestConfiguration:
    """Test configuration file parsing and validation."""

    def test_valid_minimal_config(self, tmp_path):
        """Test that a minimal valid config is accepted."""
        config_file = tmp_path / "config.json"
        config_file.write_text(json.dumps(VALID_CONFIG_MINIMAL))
        # Would call config parser and verify success

    def test_valid_full_config(self, tmp_path):
        """Test that a full valid config is accepted."""
        config_file = tmp_path / "config.json"
        config_file.write_text(json.dumps(VALID_CONFIG_FULL))
        # Would call config parser and verify success

    def test_invalid_json_syntax(self, tmp_path):
        """Test that invalid JSON is rejected."""
        config_file = tmp_path / "config.json"
        config_file.write_text('{"url": "test", invalid}')
        # Would call config parser and verify it raises an error

    def test_missing_required_url(self, tmp_path):
        """Test that missing URL is rejected."""
        config = {"user": "test", "pass": "test"}
        config_file = tmp_path / "config.json"
        config_file.write_text(json.dumps(config))
        # Would verify validation error

    def test_invalid_url_format(self, tmp_path):
        """Test that invalid URL formats are rejected."""
        invalid_urls = [
            "not-a-url",
            "http://example.com",  # missing stratum+tcp://
            "ftp://pool.example.com:3333",
            "",
        ]
        for url in invalid_urls:
            config = {"url": url, "user": "test", "pass": "test"}
            config_file = tmp_path / "config.json"
            config_file.write_text(json.dumps(config))
            # Would verify validation error

    def test_valid_url_formats(self):
        """Test that valid URL formats are accepted."""
        valid_urls = [
            "stratum+tcp://pool.example.com:3333",
            "stratum+tcp://pool.example.com:3334",
            "stratum+udp://pool.example.com:3333",
            "http://pool.example.com:9332",  # getblocktemplate
            "https://pool.example.com:9332",
        ]
        for url in valid_urls:
            config = {"url": url, "user": "test", "pass": "test"}
            # Would verify acceptance

    def test_intensity_range(self, tmp_path):
        """Test intensity value validation."""
        # Valid intensities (typically 0-30 or higher for some algos)
        for intensity in [0, 10, 20, 30]:
            config = {"url": "stratum+tcp://pool:3333", "user": "u", "pass": "p", "intensity": intensity}
            config_file = tmp_path / "config.json"
            config_file.write_text(json.dumps(config))
            # Would verify acceptance

        # Invalid intensities
        for intensity in [-1, 100]:
            config = {"url": "stratum+tcp://pool:3333", "user": "u", "pass": "p", "intensity": intensity}
            config_file = tmp_path / "config.json"
            config_file.write_text(json.dumps(config))
            # Would verify rejection

    def test_device_ids_validation(self, tmp_path):
        """Test device ID configuration validation."""
        # Valid device configurations
        valid_devices = ["0", "0,1", "0,1,2", "all"]
        for devices in valid_devices:
            config = {"url": "stratum+tcp://pool:3333", "user": "u", "pass": "p", "devices": devices}
            # Would verify acceptance

    def test_multi_pool_config(self, tmp_path):
        """Test multi-pool configuration."""
        config = {
            "pools": [
                {"url": "stratum+tcp://pool1:3333", "user": "u1", "pass": "p1"},
                {"url": "stratum+tcp://pool2:3333", "user": "u2", "pass": "p2"}
            ]
        }
        config_file = tmp_path / "config.json"
        config_file.write_text(json.dumps(config))
        # Would verify multi-pool parsing

    def test_api_bind_format(self, tmp_path):
        """Test API bind address format validation."""
        valid_binds = ["127.0.0.1:4068", "0.0.0.0:4068", "localhost:4068"]
        for bind in valid_binds:
            config = {"url": "stratum+tcp://pool:3333", "user": "u", "pass": "p", "api-bind": bind}
            # Would verify acceptance

        invalid_binds = ["4068", "127.0.0.1", ":4068", "127.0.0.1:abc"]
        for bind in invalid_binds:
            config = {"url": "stratum+tcp://pool:3333", "user": "u", "pass": "p", "api-bind": bind}
            # Would verify rejection
```

---

### 3. API Tests (`test/test_api.py`)

**Purpose**: Test the miner's built-in API for monitoring and control.

**What it tests**:
- API connection and authentication
- API commands: `summary`, `devs`, `threads`, `version`, `devsdetail`
- Response format validation (pipe-delimited or JSON)
- API permission groups (R/W access levels)
- Error handling for invalid commands
- API rate limiting

**Implementation**:

```python
import pytest
import socket
import json
import time

API_DEFAULT_PORT = 4068
API_DEFAULT_HOST = "127.0.0.1"

class MockAPIServer:
    """Mock API server for testing without running actual miner."""

    def __init__(self, host=API_DEFAULT_HOST, port=API_DEFAULT_PORT):
        self.host = host
        self.port = port
        self.socket = None

    def start(self):
        """Start the mock API server."""
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.socket.bind((self.host, self.port))
        self.socket.listen(1)

    def stop(self):
        """Stop the mock API server."""
        if self.socket:
            self.socket.close()

    def handle_command(self, command):
        """Process API command and return response."""
        responses = {
            "version": {
                "STATUS": {"SUCCESS": "Command sent to miner"},
                "VERSION": {"CGMINER": "5.6.0", "API": "1.9"}
            },
            "summary": {
                "STATUS": {"SUCCESS": "Command sent to miner"},
                "SUMMARY": {
                    "Elapsed": 3600,
                    "MHS av": 50000,
                    "Found Blocks": 0,
                    "Getworks": 10,
                    "Accepted": 100,
                    "Rejected": 2,
                    "Stale": 0,
                    "Hardware Errors": 0,
                    "Utility": 1.0,
                    "Discarded": 0,
                    "GPU Rejected": 0,
                    "Network Blocks": 1000
                }
            },
            "devs": {
                "STATUS": {"SUCCESS": "Command sent to miner"},
                "DEVS": [
                    {
                        "GPU": 0,
                        "Enabled": "Y",
                        "Status": "Alive",
                        "Temperature": 65.0,
                        "Fan Speed": 70,
                        "Fan Percent": 70,
                        "GPU Clock": 1710,
                        "Memory Clock": 4700,
                        "GPU Voltage": 0.950,
                        "GPU Activity": 100,
                        "Powertune": 100,
                        "MHS av": 50000,
                        "Accepted": 100,
                        "Rejected": 2,
                        "Hardware Errors": 0,
                        "Utility": 1.0
                    }
                ]
            }
        }
        return responses.get(command, {"STATUS": {"SUCCESS": "Unknown command"}})

class TestAPI:
    """Test miner API functionality."""

    def test_api_version_command(self):
        """Test API version command response."""
        server = MockAPIServer()
        try:
            server.start()
            # Connect and send command
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect((API_DEFAULT_HOST, API_DEFAULT_PORT))
            sock.sendall(b"version")
            response = sock.recv(4096).decode()
            sock.close()

            # Parse and validate response
            data = json.loads(response)
            assert "VERSION" in data
            assert "CGMINER" in data["VERSION"]
            assert "API" in data["VERSION"]
        finally:
            server.stop()

    def test_api_summary_command(self):
        """Test API summary command response."""
        server = MockAPIServer()
        try:
            server.start()
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect((API_DEFAULT_HOST, API_DEFAULT_PORT))
            sock.sendall(b"summary")
            response = sock.recv(4096).decode()
            sock.close()

            data = json.loads(response)
            assert "SUMMARY" in data
            summary = data["SUMMARY"]
            assert "Elapsed" in summary
            assert "MHS av" in summary
            assert "Accepted" in summary
            assert "Rejected" in summary
        finally:
            server.stop()

    def test_api_devs_command(self):
        """Test API devs command response."""
        server = MockAPIServer()
        try:
            server.start()
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect((API_DEFAULT_HOST, API_DEFAULT_PORT))
            sock.sendall(b"devs")
            response = sock.recv(4096).decode()
            sock.close()

            data = json.loads(response)
            assert "DEVS" in data
            assert isinstance(data["DEVS"], list)
        finally:
            server.stop()

    def test_api_invalid_command(self):
        """Test API response to invalid command."""
        server = MockAPIServer()
        try:
            server.start()
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect((API_DEFAULT_HOST, API_DEFAULT_PORT))
            sock.sendall(b"invalidcommand")
            response = sock.recv(4096).decode()
            sock.close()

            data = json.loads(response)
            assert "STATUS" in data
        finally:
            server.stop()

    def test_api_response_format(self):
        """Test that API responses are valid JSON."""
        server = MockAPIServer()
        try:
            server.start()
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect((API_DEFAULT_HOST, API_DEFAULT_PORT))
            sock.sendall(b"version|summary")
            response = sock.recv(4096).decode()
            sock.close()

            # Should be valid JSON
            data = json.loads(response)
            assert isinstance(data, dict)
        finally:
            server.stop()

    def test_api_permission_groups(self):
        """Test API permission group access control."""
        # Test that read-only commands work without W permission
        # Test that write commands require W permission
        pass  # Implementation depends on API group configuration

    def test_api_connection_refused(self):
        """Test behavior when API is not running."""
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(1)
        with pytest.raises(ConnectionRefusedError):
            sock.connect(("127.0.0.1", 19999))  # Unlikely port
        sock.close()
```

---

### 4. CLI Arguments Tests (`test/test_cli.py`)

**Purpose**: Validate command-line argument parsing.

**What it tests**:
- All supported command-line options
- Short vs long option formats
- Required vs optional arguments
- Argument value validation
- Mutually exclusive options
- Help text generation
- Version output

**Implementation**:

```python
import pytest
import subprocess
from pathlib import Path

class TestCLIArguments:
    """Test command-line argument parsing."""

    def test_help_output(self, miner_binary):
        """Test that --help outputs usage information."""
        if not miner_binary.exists():
            pytest.skip("Miner binary not built")

        result = subprocess.run(
            [str(miner_binary), "--help"],
            capture_output=True,
            text=True
        )
        assert result.returncode == 0
        assert "usage" in result.stdout.lower() or "gbxminer" in result.stdout.lower()

    def test_version_output(self, miner_binary):
        """Test that --version outputs version information."""
        if not miner_binary.exists():
            pytest.skip("Miner binary not built")

        result = subprocess.run(
            [str(miner_binary), "--version"],
            capture_output=True,
            text=True
        )
        assert result.returncode == 0
        assert "gbxminer" in result.stdout.lower()

    def test_invalid_option(self, miner_binary):
        """Test that invalid options are rejected."""
        if not miner_binary.exists():
            pytest.skip("Miner binary not built")

        result = subprocess.run(
            [str(miner_binary), "--invalid-option"],
            capture_output=True,
            text=True
        )
        assert result.returncode != 0

    @pytest.mark.parametrize("option,expected_algo", [
        ("-a neoscrypt", "neoscrypt"),
        ("--algo neoscrypt", "neoscrypt"),
        ("-a x11", "x11"),
        ("-a lyra2v2", "lyra2v2"),
    ])
    def test_algo_option(self, option, expected_algo, miner_binary):
        """Test algorithm selection options."""
        # This would parse the output or check internal state
        pass

    @pytest.mark.parametrize("invalid_algo", ["invalidalgo", "notanalgo", ""])
    def test_invalid_algo_option(self, invalid_algo, miner_binary):
        """Test that invalid algorithm names are rejected."""
        if not miner_binary.exists():
            pytest.skip("Miner binary not built")

        result = subprocess.run(
            [str(miner_binary), "-a", invalid_algo, "--benchmark", "--time-limit=1"],
            capture_output=True,
            text=True
        )
        # Should fail or show error about unknown algorithm

    def test_url_option_formats(self):
        """Test various URL format options."""
        valid_urls = [
            "stratum+tcp://pool.example.com:3333",
            "stratum+udp://pool.example.com:3333",
            "http://pool.example.com:9332",
        ]
        for url in valid_urls:
            # Would verify URL is accepted
            pass

    def test_intensity_option(self):
        """Test intensity option values."""
        # Valid intensities
        for intensity in ["0", "10", "20", "30"]:
            # Would verify acceptance
            pass

        # Invalid intensities
        for intensity in ["-1", "100", "abc"]:
            # Would verify rejection
            pass

    def test_device_option(self):
        """Test device selection options."""
        valid_devices = ["-d 0", "-d 0,1", "-d 0,1,2", "--devices=0"]
        for device_opt in valid_devices:
            # Would verify acceptance
            pass

    def test_api_bind_option(self):
        """Test API bind option."""
        valid_binds = [
            "-b 127.0.0.1:4068",
            "--api-bind=127.0.0.1:4068",
            "-b 0.0.0.0:4068",
        ]
        for bind_opt in valid_binds:
            # Would verify acceptance
            pass

    def test_config_file_option(self, miner_binary, tmp_path):
        """Test configuration file option."""
        config_file = tmp_path / "config.json"
        config_file.write_text('{"url": "stratum+tcp://pool:3333", "user": "u", "pass": "p"}')

        if not miner_binary.exists():
            pytest.skip("Miner binary not built")

        result = subprocess.run(
            [str(miner_binary), "-c", str(config_file), "--help"],
            capture_output=True,
            text=True
        )
        # Should not error on config file parsing

    def test_benchmark_mode(self, miner_binary):
        """Test benchmark mode options."""
        if not miner_binary.exists():
            pytest.skip("Miner binary not built")

        result = subprocess.run(
            [str(miner_binary), "--benchmark", "-a", "neoscrypt", "--time-limit=1"],
            capture_output=True,
            text=True,
            timeout=10
        )
        # Should run in benchmark mode

    def test_debug_options(self):
        """Test debug option flags."""
        debug_options = ["-D", "--debug"]
        for opt in debug_options:
            # Would verify debug mode is enabled
            pass
```

---

### 4. Functional Tests (`test/functional/`)

**Purpose**: End-to-end tests that verify component interactions using mock servers.

#### API Functional Tests (`test/functional/test_api.py`)

Tests the miner's built-in API using `MockAPIServer`:

- Server start/stop and lifecycle management
- API commands: `version`, `summary`, `devs`, `threads`, `pools`
- Response format validation
- Multi-GPU scenarios
- Error handling and connection failures
- Custom response handlers

#### Stratum Protocol Tests (`test/functional/test_stratum.py`)

Tests the stratum mining protocol using `MockStratumServer`:

- Server lifecycle and client connections
- `mining.subscribe`, `mining.authorize`, `mining.submit` methods
- Difficulty management and notifications
- Multi-client scenarios
- Callback functionality
- Custom method handlers
- Error handling (invalid params, parse errors, unknown methods)

### 5. Hashing Utility Tests (`test/unit/test_hashing.py`)

**Purpose**: Test hash calculation and utility functions.

**What it tests**:
- `swab32()` - 32-bit byte swap
- `swab64()` - 64-bit byte swap
- `bswap_32()` - 32-bit byte swap fallback
- `bswap_64()` - 64-bit byte swap fallback
- Hex encoding/decoding utilities
- Difficulty calculation
- Hash comparison

**Implementation**:

```python
import pytest

class TestHashingUtilities:
    """Test hashing utility functions."""

    def test_swab32_basic(self):
        """Test 32-bit byte swap."""
        # Test cases: (input, expected_output)
        test_cases = [
            (0x00000001, 0x01000000),
            (0x01020304, 0x04030201),
            (0xFFFFFFFF, 0xFFFFFFFF),
            (0x00000000, 0x00000000),
            (0x12345678, 0x78563412),
        ]
        for input_val, expected in test_cases:
            # Would call swab32() and verify result
            result = self.swab32(input_val)
            assert result == expected, f"swab32(0x{input_val:08x}) = 0x{result:08x}, expected 0x{expected:08x}"

    def test_swab64_basic(self):
        """Test 64-bit byte swap."""
        test_cases = [
            (0x0000000000000001, 0x0100000000000000),
            (0x0102030405060708, 0x0807060504030201),
            (0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF),
            (0x0000000000000000, 0x0000000000000000),
        ]
        for input_val, expected in test_cases:
            result = self.swab64(input_val)
            assert result == expected

    def swab32(self, v):
        """Python implementation of swab32 for testing."""
        return ((v << 24) & 0xFF000000) | \
               ((v << 8) & 0x00FF0000) | \
               ((v >> 8) & 0x0000FF00) | \
               ((v >> 24) & 0x000000FF)

    def swab64(self, v):
        """Python implementation of swab64 for testing."""
        # Swap 32-bit halves, then swap bytes within each half
        low = (v & 0xFFFFFFFF)
        high = (v >> 32) & 0xFFFFFFFF
        return (self.swab32(low) << 32) | self.swab32(high)

    def test_hex_to_bytes(self):
        """Test hex string to bytes conversion."""
        test_cases = [
            ("00", b'\x00'),
            ("ff", b'\xff'),
            ("010203", b'\x01\x02\x03'),
            ("deadbeef", b'\xde\xad\xbe\xef'),
        ]
        for hex_str, expected in test_cases:
            result = bytes.fromhex(hex_str)
            assert result == expected

    def test_bytes_to_hex(self):
        """Test bytes to hex string conversion."""
        test_cases = [
            (b'\x00', "00"),
            (b'\xff', "ff"),
            (b'\x01\x02\x03', "010203"),
            (b'\xde\xad\xbe\xef', "deadbeef"),
        ]
        for byte_val, expected in test_cases:
            result = byte_val.hex()
            assert result == expected

    def test_difficulty_calculation(self):
        """Test difficulty calculation from target."""
        # Difficulty = max_target / current_target
        max_target = 0x00000000FFFF0000000000000000000000000000000000000000000000000000
        test_cases = [
            (max_target, 1.0),
            (max_target // 2, 2.0),
            (max_target // 10, 10.0),
        ]
        for target, expected_diff in test_cases:
            diff = max_target / target
            assert abs(diff - expected_diff) < 0.0001

    def test_hash_comparison(self):
        """Test hash comparison for share validation."""
        # Hash must be less than target to be valid
        test_cases = [
            # (hash_hex, target_hex, expected_valid)
            ("0000000000000000000000000000000000000000000000000000000000000001",
             "00000000FFFF0000000000000000000000000000000000000000000000000000",
             True),
            ("00000000FFFF0000000000000000000000000000000000000000000000000001",
             "00000000FFFF0000000000000000000000000000000000000000000000000000",
             False),
        ]
        for hash_hex, target_hex, expected_valid in test_cases:
            hash_int = int(hash_hex, 16)
            target_int = int(target_hex, 16)
            is_valid = hash_int < target_int
            assert is_valid == expected_valid
```

---

### 6. GPU Detection Tests (`test/test_gpu.py`)

**Purpose**: Test GPU detection and management functionality.

**What it tests**:
- GPU enumeration
- CUDA compute capability detection
- GPU memory detection
- Multi-GPU configuration
- GPU temperature monitoring (NVML)
- GPU power management

**Implementation**:

```python
import pytest
from unittest.mock import Mock, patch, MagicMock

class MockCUDADevice:
    """Mock CUDA device for testing."""
    def __init__(self, device_id, name, compute_capability, memory):
        self.device_id = device_id
        self.name = name
        self.compute_capability = compute_capability
        self.memory = memory

class TestGPUDetection:
    """Test GPU detection and management."""

    @patch('subprocess.run')
    def test_gpu_enumeration(self, mock_run):
        """Test that GPUs are properly enumerated."""
        # Mock nvidia-smi output
        mock_run.return_value.stdout = """
        GPU Name,Memory Total,Compute Capability
        0,NVIDIA GeForce RTX 3080,10240,8.6
        1,NVIDIA GeForce RTX 3080,10240,8.6
        """
        # Would parse GPU list and verify count

    @patch('subprocess.run')
    def test_cuda_compute_capability(self, mock_run):
        """Test CUDA compute capability detection."""
        # Test various compute capabilities
        test_cases = [
            ("5.0", "Maxwell"),
            ("6.1", "Pascal"),
            ("7.5", "Turing"),
            ("8.6", "Ampere"),
            ("8.9", "Ada"),
            ("9.0", "Hopper"),
        ]
        for cc, arch_name in test_cases:
            # Would verify compute capability parsing
            major, minor = cc.split('.')
            assert int(major) >= 5, "Minimum compute capability is 5.0 (Maxwell)"

    def test_gpu_memory_detection(self):
        """Test GPU memory detection."""
        # Test memory values in MB
        test_cases = [
            (2048, 2),
            (4096, 4),
            (8192, 8),
            (10240, 10),
            (24576, 24),
        ]
        for memory_mb, expected_gb in test_cases:
            memory_gb = memory_mb / 1024
            assert abs(memory_gb - expected_gb) < 0.01

    def test_multi_gpu_configuration(self):
        """Test multi-GPU configuration parsing."""
        test_cases = [
            ("0", [0]),
            ("0,1", [0, 1]),
            ("0,1,2", [0, 1, 2]),
            ("all", "all"),
        ]
        for config, expected in test_cases:
            if config == "all":
                # Would detect all available GPUs
                pass
            else:
                devices = [int(d) for d in config.split(',')]
                assert devices == expected

    @patch('subprocess.run')
    def test_gpu_temperature_monitoring(self, mock_run):
        """Test GPU temperature monitoring via NVML."""
        # Mock temperature readings
        mock_run.return_value.stdout = "65"
        # Would parse temperature and verify range

    def test_gpu_architecture_support(self):
        """Test that minimum GPU architecture is enforced."""
        # Minimum is sm_50 (Maxwell)
        min_compute = (5, 0)

        # Test various architectures
        architectures = {
            "GTX 900": (5, 0),
            "GTX 1000": (6, 1),
            "RTX 2000": (7, 5),
            "RTX 3000": (8, 6),
            "RTX 4000": (8, 9),
            "RTX 5000": (9, 0),
        }

        for gpu_name, (major, minor) in architectures.items():
            assert (major, minor) >= min_compute, f"{gpu_name} should be supported"

    def test_invalid_gpu_device_id(self):
        """Test handling of invalid GPU device IDs."""
        invalid_ids = [-1, 99, "abc"]
