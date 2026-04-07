# GBXminer Python Test Suite

This document describes the Python test suite for GBXminer, including test structure, implementation details, and how each test category works.

## Table of Contents

1. [Overview](#overview)
2. [Test Environment Setup](#test-environment-setup)
3. [Test Directory Structure](#test-directory-structure)
4. [Test Categories](#test-categories)
   - [Algorithm Tests](#1-algorithm-tests-testunittestalgospy)
   - [Configuration Tests](#2-configuration-tests-testunittestconfigpy)
   - [Hashing Utility Tests](#3-hashing-utility-tests-testunittesthashingpy)
   - [Pool Tests](#4-pool-tests-testunittestpoolspy)
   - [Utility Tests](#5-utility-tests-testunittestutilspy)
   - [API Functional Tests](#6-api-functional-tests-testfunctionaltestapipy)
   - [CLI Arguments Tests](#7-cli-arguments-tests-testfunctionaltestclipy)
   - [Stratum Protocol Tests](#8-stratum-protocol-tests-testfunctionalteststratumpy)
   - [Integration Tests](#9-integration-tests-testfunctionaltestintegrationpy)
   - [Statistics Tests](#10-statistics-tests-testfunctionalteststatspy)
   - [Lint Tests](#11-lint-tests-testlint)
5. [Running Tests](#running-tests)
6. [CI/CD Integration](#cicd-integration)

---

## Overview

The Python test suite provides comprehensive testing for GBXminer's functionality without requiring actual GPU hardware. Tests use mocking and stubbing to simulate hardware and network components.

### Testing Philosophy

- **Unit Tests**: Test individual functions and components in isolation (`test/unit/`)
- **Integration Tests**: Test component interactions (`test/functional/`)
- **Functional Tests**: Test end-to-end functionality (`test/functional/`)
- **Lint Tests**: Code quality and style checks (`test/lint/`)
- **Mock External Dependencies**: Simulate GPUs, pools, and network resources

---

## Test Environment Setup

### Requirements

Install dependencies from `test/requirements.txt`:

```
pytest>=7.0.0
pytest-cov>=4.0.0
pytest-mock>=3.10.0
pytest-asyncio>=0.21.0
jsonschema>=4.0.0
requests>=2.28.0
```

### Fixtures Setup

Shared fixtures are defined in `test/conftest.py`:

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
│   ├── test_api.py          # API functional tests
│   ├── test_cli.py          # CLI arguments tests
│   ├── test_integration.py  # Integration tests
│   ├── test_stats.py        # Statistics tests
│   └── test_stratum.py      # Stratum protocol tests
└── lint/                    # Lint tests
    ├── all-lint.py          # Main lint runner
    ├── lint-files.py        # File linting
    ├── lint-include-guards.py  # Header guard checks
    ├── lint-logs.py         # Log format checks
    ├── lint-python-utf8-encoding.py  # Python encoding checks
    ├── lint-shell-locale.py # Shell locale checks
    ├── lint-shell.py        # Shell script linting
    ├── lint-spelling.py     # Spelling checks
    ├── lint-whitespace.py   # Whitespace checks
    └── spelling.ignore-words.txt  # Spelling exceptions
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

### 2. Configuration Tests (`test/unit/test_config.py`)

**Purpose**: Validate JSON configuration file parsing and structure.

**What it tests**:
- Valid JSON configuration files
- Invalid JSON syntax detection
- Required fields validation (url, user, pass)
- Optional fields with defaults
- Pool configuration (url, user, pass)
- Invalid URL formats
- Intensity range validation (typically 0-30)
- Device ID validation
- Multi-pool configuration
- API bind format validation

### 3. Hashing Utility Tests (`test/unit/test_hashing.py`)

**Purpose**: Test hash calculation and utility functions.

**What it tests**:
- `swab32()` - 32-bit byte swap
- `swab64()` - 64-bit byte swap
- `swab256()` - 256-bit byte swap
- Hex encoding/decoding utilities
- Difficulty calculation
- Hash comparison for share validation

### 4. Pool Tests (`test/unit/test_pools.py`)

**Purpose**: Test pool configuration and management utilities.

**What it tests**:
- Pool URL validation (stratum and getblocktemplate formats)
- Pool user/password requirements
- Multi-pool configuration
- Pool failover logic

### 5. Utility Tests (`test/unit/test_utils.py`)

**Purpose**: Test general utility functions.

**What it tests**:
- Hashrate formatting with SI prefixes
- Time value operations (subtraction, comparison)
- Endian conversion (big-endian, little-endian)
- Array size calculations
- Min/max operations

### 6. API Functional Tests (`test/functional/test_api.py`)

**Purpose**: Test the miner's built-in API for monitoring and control.

**What it tests**:
- Server start/stop and lifecycle management
- API commands: `version`, `summary`, `devs`, `threads`, `pools`, `devsdetail`
- Response format validation (JSON)
- Multi-GPU scenarios
- Error handling and connection failures
- Custom response handlers
- Empty command handling

### 7. CLI Arguments Tests (`test/functional/test_cli.py`)

**Purpose**: Validate command-line argument parsing.

**What it tests**:
- Help and version output (`--help`, `-h`, `--version`, `-V`)
- Algorithm selection (`-a`, `--algo`) with valid and invalid algorithms
- URL format validation (`-o`, `--url`) with various schemes
- Device selection (`-d`, `--devices`)
- Intensity options (`-l`, `--intensity`)
- Configuration file loading (`-c`, `--config`)
- Benchmark mode (`--benchmark`, `--time-limit`)
- API bind options (`-b`, `--api-bind`)
- Debug options (`-D`, `--debug`)
- Invalid option handling

### 8. Stratum Protocol Tests (`test/functional/test_stratum.py`)

**Purpose**: Test the stratum mining protocol implementation.

**What it tests**:
- Server lifecycle and client connections
- `mining.subscribe`, `mining.authorize`, `mining.submit` methods
- Difficulty management and notifications
- Multi-client scenarios
- Callback functionality
- Custom method handlers
- Error handling (invalid params, parse errors, unknown methods)
- Job creation and management
- MockStratumJob helper class

### 9. Integration Tests (`test/functional/test_integration.py`)

**Purpose**: End-to-end integration tests verifying component interactions.

**What it tests**:
- **Mining Workflow**: Complete workflow from connection to share submission
- **Multiple Share Submission**: Sequential share submissions
- **API and Stratum Integration**: API reflecting stratum mining activity
- **Connection Resilience**: Server restart and reconnection scenarios
- **Multiple Clients**: Simultaneous client connections
- **Job Management**: Job creation, notification, and clean_jobs flag
- **Error Recovery**: Recovery after invalid JSON and unknown methods
- **Concurrent Operations**: API queries during active mining

### 10. Statistics Tests (`test/functional/test_stats.py`)

**Purpose**: Test mining statistics tracking and calculation.

**What it tests**:
- **Share Tracking**: Share count increment, accepted/rejected ratio
- **Hashrate Calculation**: Basic calculation, unit conversion, edge cases
- **Difficulty Tracking**: Difficulty changes and notifications
- **Uptime Tracking**: Elapsed time monitoring
- **Stats Persistence**: Stats reset and retrieval
- **Performance Metrics**: Best share tracking, network stats
- **Stale Share Detection**: Concept validation

### 11. Lint Tests (`test/lint/`)

**Purpose**: Code quality and style checks for the codebase.

**What it tests**:
- `all-lint.py` - Main lint runner that orchestrates all lint checks
- `lint-files.py` - File naming and organization checks
- `lint-include-guards.py` - C/C++ header guard consistency
- `lint-logs.py` - Log message format and consistency
- `lint-python-utf8-encoding.py` - Python file encoding validation
- `lint-shell-locale.py` - Shell script locale settings
- `lint-shell.py` - Shell script syntax and style
- `lint-spelling.py` - Spelling checks in comments and strings
- `lint-whitespace.py` - Trailing whitespace and line ending checks

---

## Running Tests

### Run all tests:
```bash
cd test
pip install -r requirements.txt
pytest -v
```

### Run specific test category:
```bash
# Unit tests only
pytest unit/ -v

# Functional tests only
pytest functional/ -v

# Lint tests only
pytest lint/ -v
```

### Run with coverage:
```bash
pytest --cov=../src --cov-report=html
```

### Run specific test file:
```bash
pytest unit/test_algos.py -v
```

### Run tests matching keyword:
```bash
pytest -k "swab" -v
```

