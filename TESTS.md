# GBXminer Python Test Suite

> **Reviewer note** — This document is the authoritative reference for the
> GBXminer test suite.  Every section maps directly to files that exist in the
> repository.  If a test is described here, the file exists.  If a file exists,
> it is described here.  Discrepancies are bugs.

---

## Table of Contents

1. [Overview](#overview)
2. [Design Philosophy](#design-philosophy)
3. [Test Directory Structure](#test-directory-structure)
4. [Setup and Requirements](#setup-and-requirements)
5. [Running the Suite](#running-the-suite)
6. [Test Inventory](#test-inventory)
   - [Unit Tests](#unit-tests)
   - [Functional Tests](#functional-tests)
   - [Lint Tests](#lint-tests)
7. [CI/CD Integration](#cicd-integration)
8. [Adding New Tests](#adding-new-tests)

---

## Overview

The Python test suite validates GBXminer correctness without requiring physical
GPU hardware or a live mining pool. Hardware and network dependencies are
isolated behind thin mock servers (`MockAPIServer`, `MockStratumServer`) or
skipped gracefully when the built binary is absent.

| Tier | Directory | Requires binary | Requires network |
|------|-----------|-----------------|-----------------|
| Unit | `test/unit/` | No | No |
| Functional | `test/functional/` | Conditionally | No |
| Lint | `test/lint/` | No | No |

---

## Design Philosophy

1. **NACK-first mindset** — every test must have a clear failure mode. A test
   that can never fail is dead code, not a safety net.
2. **No implicit state** — tests must not rely on global mutable state or
   execution order. Each test class is independently instantiable.
3. **Mock at the boundary** — hardware (GPU), network (pool, API socket), and
   OS process calls are the only things that need mocking. Pure algorithmic
   logic is tested directly.
4. **Binary-optional** — the suite runs fully (with appropriate skips) without
   building the C++ binary. This keeps unit and lint tiers fast in CI.
5. **No `sys.path` manipulation** — `pytest.ini` sets `pythonpath = test` so
   all imports are rooted at `test/`. Inline `sys.path.insert` calls are
   banned; they break when pytest is invoked from a non-standard CWD.
6. **Single source of truth for helpers** — utility implementations live in
   `test/utils/helpers.py`. Test files import from there rather than
   reimplementing the same logic inline — a diverged copy silently tests the
   wrong thing.

---


## Test Directory Structure

```
.
├── pytest.ini                        # rootdir anchor; pythonpath = test
└── test/
    ├── __init__.py
    ├── conftest.py                   # Session and module-level shared fixtures
    ├── requirements.txt              # Python test dependencies
    ├── utils/                        # Test utilities (NOT test files)
    │   ├── __init__.py
    │   ├── config_parser.py          # ConfigParser + ConfigValidationError
    │   ├── helpers.py                # Pure-Python mirrors of C helpers
    │   ├── mock_api.py               # MockAPIServer (cgminer JSON-API)
    │   └── mock_stratum.py           # MockStratumServer + MockStratumJob
    ├── unit/                         # Isolated, hardware-free unit tests
    │   ├── __init__.py
    │   ├── conftest.py               # Unit-only fixtures
    │   ├── test_algos.py             # algo_names[] / ALGO_ENUM consistency
    │   ├── test_config.py            # JSON config parsing and validation
    │   ├── test_gpu.py               # CUDA compute capability helpers  [NEW]
    │   ├── test_hashing.py           # swab32/64, hex utils, difficulty, share check
    │   ├── test_pools.py             # Pool config structure and validation
    │   └── test_utils.py             # Hashrate formatting, timeval, endianness
    ├── functional/                   # Component interaction / mock-server tests
    │   ├── __init__.py
    │   ├── test_api.py               # cgminer JSON-API via MockAPIServer
    │   ├── test_cli.py               # CLI argument handling
    │   ├── test_integration.py       # End-to-end mining workflow tests
    │   ├── test_stats.py             # Statistics tracking and metrics
    │   └── test_stratum.py           # Stratum protocol via MockStratumServer
    └── lint/                         # Static analysis and style checks
        ├── all-lint.py               # Orchestrator: runs all lint-*.py
        ├── lint-files.py             # Filename conventions and permissions
        ├── lint-include-guards.py    # C/C++ header include-guard format   [FIXED]
        ├── lint-logs.py              # applog()/gpulog() newline discipline
        ├── lint-python-utf8-encoding.py  # Explicit encoding= in open() calls
        ├── lint-shell-locale.py      # LC_ALL=C in shell scripts
        ├── lint-shell.py             # shellcheck wrapper
        ├── lint-spelling.py          # codespell wrapper
        ├── lint-whitespace.py        # Trailing whitespace / tab check
        └── spelling.ignore-words.txt # codespell false-positive suppression
```

---

## Setup and Requirements

```bash
# From the project root
python3 -m venv .venv
source .venv/bin/activate
pip install -r test/requirements.txt
```

`test/requirements.txt`:

```
pytest>=7.0.0
pytest-cov>=4.0.0
pytest-mock>=3.10.0
pytest-asyncio>=0.21.0
jsonschema>=4.0.0
requests>=2.28.0
```

`pytest.ini` (project root — **do not remove**):

```ini
[pytest]
testpaths    = test
pythonpath   = test
addopts      = --tb=short -q
asyncio_mode = auto
```

The `pythonpath = test` entry makes `from utils.mock_api import ...` work from
any invocation directory. Removing it will break all functional test imports.

---

## Running the Suite

```bash
# All tests (unit + functional)
pytest

# Unit tests only — fastest, no mock servers
pytest test/unit/

# Functional tests only
pytest test/functional/

# Lint checks
python test/lint/all-lint.py

# Coverage report
pytest --cov=test/utils --cov-report=term-missing

# Verbose (useful for parametrize debugging)
pytest -v test/unit/test_hashing.py

# Stop on first failure
pytest -x
```

---

## Test Inventory

### Unit Tests

#### `test/unit/test_algos.py` — Algorithm registry consistency

| Test | What it checks |
|------|---------------|
| `test_algo_names_count_matches_enum` | `len(ALGO_NAMES) - 1 == ALGO_COUNT (76)` |
| `test_algo_names_are_unique` | No duplicate names before the `""` terminator |
| `test_algo_enum_count_matches_names` | `len(ALGO_ENUM) == ALGO_COUNT` |
| `test_algo_enum_values_are_sequential` | All values in `[0, ALGO_COUNT)` |
| `test_algorithm_aliases` *(parametrized)* | Each alias target is in `ALGO_NAMES` |
| `test_algorithm_aliases_all_targets_are_valid` | ✏️ **EDITED** — replaces BUG-2 test |
| `test_cryptonight_fork_mapping` *(parametrized)* | Fork variant → canonical algo name |
| `test_cryptonight_fork_mapping_all_defined` | All fork entries reference valid algos |
| `test_no_empty_algo_names_before_terminator` | No internal `""` gaps |
| `test_terminator_is_last_entry` | Exactly one `""` and it is last |
| `test_algo_enum_matches_names_order` | Enum values match array positions |
| `test_primary_algorithms_present` | Known-important algos are not accidentally deleted |
| `test_auto_algo_is_last_real_entry` | `ALGO_NAMES[75] == "auto"` |

#### `test/unit/test_config.py` — JSON configuration validation

| Test | What it checks |
|------|---------------|
| `test_valid_minimal_config_structure` | `url`, `user`, `pass` present |
| `test_valid_full_config_structure` | Required + optional fields present |
| `test_config_json_serialization` | Round-trip write + read preserves data |
| `test_config_json_deserialization` | `json.load` recovers correct values |
| `test_invalid_json_syntax` | `json.JSONDecodeError` on malformed input |
| `test_missing_required_url_field` | Missing `url` is detectable |
| `test_missing_required_user_field` | Missing `user` is detectable |
| `test_missing_required_pass_field` | Missing `pass` is detectable |
| `test_valid_url_formats` *(parametrized)* | Valid schemes are accepted |
| `test_invalid_url_formats` *(parametrized)* | ✏️ **EDITED** — calls real validator (BUG-4) |
| `test_url_scheme_extraction` | Scheme parsed correctly |
| `test_url_host_port_extraction` | Host + port parsed correctly |
| `test_valid_intensity_values` *(parametrized)* | `[0, 30]` accepted |
| `test_invalid_intensity_values` *(parametrized)* | `< 0` or `> 30` rejected |
| `test_valid_api_bind_formats` *(parametrized)* | `host:port` format accepted |
| `test_invalid_api_bind_formats` *(parametrized)* | Malformed binds rejected |
| `test_valid_device_configs` *(parametrized)* | `"0"`, `"0,1"`, `"all"` parse correctly |
| `test_invalid_device_configs` *(parametrized)* | Negative IDs / non-numeric rejected |
| `test_multi_pool_structure` | `pools` array present and is a list |
| `test_multi_pool_entries_valid` | Each pool entry has required fields |
| `test_multi_pool_json_serialization` | Pool count preserved after round-trip |
| `test_config_file_not_found` | Missing file is detectable |
| `test_config_file_empty` | Empty file raises `json.JSONDecodeError` |
| `test_config_file_permissions` | Created config is readable |

#### `test/unit/test_hashing.py` — Byte-swap and share-validation arithmetic

| Test | What it checks |
|------|---------------|
| `test_swab32_basic` *(fixture)* | 8 representative 32-bit byte swaps |
| `test_swab32_identity_zero` | `swab32(0) == 0` |
| `test_swab32_identity_all_ones` | `swab32(0xFFFFFFFF) == 0xFFFFFFFF` |
| `test_swab32_symmetry` | `swab32(swab32(x)) == x` for all test values |
| `test_swab32_byte_reversal` | Individual byte positions verified |
| `test_swab64_basic` *(fixture)* | 6 representative 64-bit byte swaps |
| `test_swab64_identity_zero` | `swab64(0) == 0` |
| `test_swab64_identity_all_ones` | `swab64(0xFFFF…) == 0xFFFF…` |
| `test_swab64_symmetry` | `swab64(swab64(x)) == x` |
| `test_swab64_byte_reversal` | `0x0102030405060708 → 0x0807060504030201` |
| `test_hex_to_bytes` *(fixture)* | Hex strings decoded correctly |
| `test_bytes_to_hex` *(fixture)* | Byte sequences encoded correctly |
| `test_hex_roundtrip` | Encode → decode preserves bytes |
| `test_hex_uppercase_lowercase` | Case-insensitive parsing |
| `test_hex_empty_string` | Empty hex string → `b''` |
| `test_hex_odd_length` | Odd-length hex raises `ValueError` |
| `test_difficulty_calculation` *(fixture)* | `max_target / target` is correct |
| `test_difficulty_inverse_relationship` | Half target → double difficulty |
| `test_difficulty_edge_cases` | Min and astronomical difficulties |
| `test_hash_comparison` *(fixture)* | ✏️ **EDITED** — strict `<` not `<=` (BUG-3) |
| `test_hash_comparison_boundary` | ✏️ **EDITED** — `hash == target` is invalid (BUG-3) |
| `test_hash_comparison_zero_hash` | Zero hash is always a valid share |
| `test_hash_comparison_max_hash` | Max hash fails any realistic target |
| `test_swab256_basic` | 256-bit word-level byte swap |
| `test_swab256_symmetry` | `swab256(swab256(x)) == x` |
| `test_swab256_zero` | All-zero input unchanged |
| `test_swab256_ones` | All-one input unchanged |

#### `test/unit/test_pools.py` — Pool configuration and failover

| Test | What it checks |
|------|---------------|
| `test_pool_config_required_fields` | `url`, `user`, `pass` present |
| `test_pool_config_optional_fields` | Optional fields accepted without error |
| `test_pool_config_json_serialization` | Round-trip preserves data |
| `test_valid_pool_urls` *(parametrized)* | URL parsed to scheme / host / port |
| `test_invalid_pool_urls` *(parametrized)* | Bad schemes rejected |
| `test_wallet_address_format` | Address non-empty and ≤ 128 chars |
| `test_password_format` | Password is a string ≤ 128 chars |
| `test_multi_pool_basic_structure` | `pools` key present and is a list |
| `test_multi_pool_minimum_pools` | At least 2 pool entries |
| `test_multi_pool_each_valid` | Each pool has required fields |
| `test_multi_pool_unique_urls` | No duplicate pool URLs |
| `test_multi_pool_json_serialization` | Pool count preserved |
| `test_pool_priority_order` | `priority` field sorts numerically |
| `test_pool_retry_configuration` | `retries` and `retry-pause` accepted |
| `test_difficulty_start` | `diff`, `min-diff`, `max-diff` constraints |
| `test_difficulty_range_validity` | `min_diff > 0`, `max_diff > min_diff` |

#### `test/unit/test_utils.py` — General utility functions

| Test | What it checks |
|------|---------------|
| `test_hashrate_unit_selection` *(parametrized)* | ✏️ **NEW** — canonical thresholds from `helpers.py` |
| `test_hashrate_prefix_selection` *(parametrized)* | ✏️ **EDITED** — delegates to `helpers.py` (BUG-6) |
| `test_hashrate_non_negative` *(parametrized)* | Formatted value ≥ 0 |
| `test_hashrate_format_includes_unit` | Output contains `H/s` |
| `test_hashrate_format_two_decimal_places` | Two digits after the decimal point |
| `test_timeval_subtract_positive` | `10.5s - 5.25s = 5.25s` |
| `test_timeval_subtract_negative` | Subtraction of larger from smaller |
| `test_timeval_subtract_equal` | Equal times → zero delta |
| `test_timeval_subtract_borrow` | Microsecond carry handled correctly |
| `test_aligned_allocation_concept` | Cache-line (64-byte) alignment check |
| `test_aligned_allocation_rounding` | Round-up arithmetic for alignment |
| `test_array_size_calculation` | `len(arr)` mirrors `ARRAY_SIZE` macro |
| `test_max_function` | `max(a, b)` mirrors C `MAX` macro |
| `test_min_function` | `min(a, b)` mirrors C `MIN` macro |
| `test_be32enc_basic` | Big-endian 32-bit encode |
| `test_be32dec_basic` | Big-endian 32-bit decode |
| `test_le32dec_basic` | Little-endian 32-bit decode |
| `test_be32_roundtrip` | Encode → decode round-trip |
| `test_is_windows_concept` | `sys.platform` detection |
| `test_is_x64_concept` | `struct.calcsize("P") == 8` on 64-bit host |
| `test_likely_unlikely_semantics` | `bool()` is idempotent |

---

### Functional Tests

#### `test/functional/test_api.py` — cgminer JSON-API via `MockAPIServer`

| Test | What it checks |
|------|---------------|
| `test_server_start_stop` | Server starts and sets `running = True` |
| `test_server_context_manager` | `with MockAPIServer()` lifecycle |
| `test_version_command` | `version` returns `STATUS` + `VERSION` |
| `test_summary_command` | `summary` returns `Elapsed`, `Accepted`, etc. |
| `test_devs_command` | `devs` returns a non-empty `DEVS` list |
| `test_threads_command` | `threads` returns a `THREADS` list |
| `test_pools_command` | `pools` returns a `POOLS` list with `URL` |
| `test_unknown_command` | Unknown command returns `STATUS.SUCCESS = False` |
| `test_multiple_commands` | Pipe-separated commands return a 2-element list |
| `test_miner_state_update` | `update_miner_state()` persists to `get_miner_state()` |
| `test_custom_response_handler` | `set_response_handler()` registers handler |
| `test_version_response_format` | All required fields present |
| `test_summary_response_format` | All 12 required summary fields present |
| `test_devs_response_format` | All 11 required device fields present |
| `test_devsdetail_response_format` | All 7 required detail fields present |
| `test_multi_gpu_devs_response` | 4 GPU state → `len(DEVS) == 4` |
| `test_multi_gpu_threads_response` | 3 GPU state → `len(THREADS) == 3` |
| `test_connection_refused` | Connecting to dead port raises `ConnectionRefusedError` |
| `test_empty_command` | Empty payload does not crash the server |

#### `test/functional/test_cli.py` — CLI argument handling

| Test | What it checks |
|------|---------------|
| `test_help_exits_cleanly` | `--help` exits 0 and mentions binary name |
| `test_version_exits_cleanly` | `--version` exits 0 and prints version string |
| `test_unknown_option_exits_nonzero` | Unrecognized option causes non-zero exit |
| `test_invalid_algo_exits_nonzero` | Unknown algorithm triggers error |
| `test_valid_config_file_is_accepted` | Valid config file parses without error |
| `test_missing_config_file_exits_nonzero` | Non-existent config file causes non-zero exit |
| `test_invalid_json_config_exits_nonzero` | Malformed JSON config causes non-zero exit |
| `test_valid_urls_accepted` *(parametrized)* | Valid mining-pool URLs return True |
| `test_invalid_urls_rejected` *(parametrized)* | Invalid URLs return False |
| `test_valid_api_binds` *(parametrized)* | Valid `host:port` formats accepted |
| `test_invalid_api_binds` *(parametrized)* | Malformed API binds rejected |
| `test_valid_device_list_parsed` *(parametrized)* | Comma-separated device IDs parse correctly |
| `test_all_keyword_accepted` | String 'all' is valid device specification |
| `test_invalid_device_list_rejected` *(parametrized)* | Negative IDs, non-numeric tokens rejected |
| `test_neoscrypt_is_present` | neoscrypt is always listed in algo_names |
| `test_auto_is_present` | 'auto' sentinel is present for auto-selection |
| `test_no_algorithm_name_contains_space` | Algorithm names must not contain spaces |
| `test_aliases_resolve_to_known_algorithms` | Every alias target names a valid algorithm |

#### `test/functional/test_integration.py` — End-to-end mining workflow tests

| Test | What it checks |
|------|---------------|
| `test_full_mining_workflow` | Complete workflow: connect, subscribe, authorize, submit |
| `test_mining_workflow_with_multiple_shares` | Submitting multiple shares in sequence |
| `test_api_reports_stratum_activity` | API reflects stratum mining activity |
| `test_stratum_server_restart` | Client behavior when server restarts |
| `test_multiple_clients_simultaneous` | Multiple clients connecting simultaneously |
| `test_job_creation_and_notification` | Creating and notifying about new jobs |
| `test_job_clean_jobs_flag` | `clean_jobs` flag properly set in notifications |
| `test_invalid_json_recovery` | Server recovery after receiving invalid JSON |
| `test_unknown_method_recovery` | Server recovery after unknown method |
| `test_api_queries_during_mining` | API queries work during active mining |

#### `test/functional/test_stats.py` — Statistics tracking and metrics

| Test | What it checks |
|------|---------------|
| `test_share_count_increment` | Share count increments correctly |
| `test_accepted_rejected_ratio` | Accepted/rejected share ratio tracking |
| `test_hashrate_basic_calculation` | Basic hashrate calculation |
| `test_hashrate_zero_time` | Hashrate with zero time returns 0 |
| `test_hashrate_unit_conversion` | Hashrate unit conversion (H/s to MH/s, GH/s) |
| `test_hashrate_calculations` *(parametrized)* | Various hashrate calculations |
| `test_difficulty_tracking` | Difficulty is tracked correctly |
| `test_difficulty_notification` | Difficulty change notification |
| `test_elapsed_time_increases` | Elapsed time increases over time |
| `test_stats_reset` | Stats can be reset |
| `test_best_share_tracking` | Best share (highest difficulty) is tracked |
| `test_network_stats` | Network-related statistics tracking |
| `test_stale_share_concept` | Concept of stale share detection |

#### `test/functional/test_stratum.py` — Stratum protocol via `MockStratumServer`

| Test | What it checks |
|------|---------------|
| `test_server_start_stop` | Server starts and sets `running = True` |
| `test_server_context_manager` | `with MockStratumServer()` lifecycle |
| `test_client_connection` | Client count increments / decrements correctly |
| `test_mining_subscribe` | Subscribe response has `extranonce1` + size |
| `test_mining_authorize` | Authorize returns `result = True` |
| `test_mining_submit` | Share accepted; `submitted_shares == 1` |
| `test_mining_authorize_invalid_params` | Missing password → `error != None` |
| `test_mining_submit_invalid_params` | Truncated params → `error != None` |
| `test_unknown_method` | Error code 20 returned |
| `test_parse_error` | Invalid JSON → error code 1 |
| `test_job_creation` | `MockStratumJob` stores given fields |
| `test_job_to_dict` | All 9 Stratum notify fields present |
| `test_job_to_notify_params` | `to_notify_params()` returns 9-element list |
| `test_job_create_share_submission` | Share submission JSON is correct |
| `test_job_default_values` | Sensible defaults for all fields |
| `test_set_difficulty` | `set_difficulty(8.0)` updates `server.difficulty` |
| `test_difficulty_notification` | Difficulty change broadcasts `mining.set_difficulty` |
| `test_multiple_client_connections` | 3 clients → count = 3 → 0 after close |
| `test_broadcast_to_clients` | All 2 clients receive broadcast message |
| `test_on_connect_callback` | `on_connect` fires with correct client ID |
| `test_on_share_callback` | `on_share` fires with `username` + `nonce` |
| `test_custom_method_handler` | Custom handler returns expected response |

---

### Lint Tests

Run via `python test/lint/all-lint.py`.

| Script | What it checks |
|--------|---------------|
| `lint-files.py` | Filename conventions, file permissions, shebang lines |
| `lint-include-guards.py` | ✏️ **EDITED** (BUG-1) — all `*.h` files have `GBXMINER_*_H` guards |
| `lint-logs.py` | `applog()` / `gpulog()` calls do not embed `\n` in format strings |
| `lint-python-utf8-encoding.py` | All `open()` / `check_output()` calls specify `encoding=` |
| `lint-shell-locale.py` | All shell scripts set `export LC_ALL=C` or opt-in explicitly |
| `lint-shell.py` | `shellcheck` wrapper (skips gracefully if not installed) |
| `lint-spelling.py` | `codespell` wrapper (exits 0 even with findings — warnings only) |
| `lint-whitespace.py` | No trailing whitespace or tab characters in new diff lines |

---

#### `pytest.ini` *(new file, project root)*

```ini
[pytest]
testpaths    = test
pythonpath   = test
addopts      = --tb=short -q
asyncio_mode = auto
```

Eliminates all `sys.path` manipulation from test files and anchors the rootdir
so `pytest` can be invoked from anywhere in the repository.

---

## CI/CD Integration

Recommended job split:

```yaml
# Fast lane: no binary needed, <5 seconds
unit-and-lint:
  script:
    - pip install -r test/requirements.txt
    - pytest test/unit/ -q
    - python test/lint/all-lint.py

# Mock-server functional tests
functional:
  script:
    - pip install -r test/requirements.txt
    - pytest test/functional/ -q

# Only after `make` succeeds
binary-cli:
  needs: [build]
  script:
    - pip install -r test/requirements.txt
    - pytest test/functional/test_cli.py -v
```

Coverage gate (suggested minimum 80 % for `test/utils/`):

```bash
pytest --cov=test/utils --cov-report=term-missing --cov-fail-under=80
```

---

## Adding New Tests

### Unit test

1. Add the test class to the appropriate `test/unit/test_*.py` file, or create
   a new `test_<component>.py`.
2. Add shared fixtures to `test/unit/conftest.py` (unit-only) or
   `test/conftest.py` (suite-wide).
3. Add a row to the relevant table in this document.

### Lint check

1. Create `test/lint/lint-<what>.py`.
2. Follow the existing pattern: exit 0 on success, exit 1 on failure.
3. Add a row to the lint table.
4. `all-lint.py` picks it up automatically via `glob("lint-*.py")`.

### Functional test

1. Add a test class to an existing `test/functional/test_*.py` or create a
   new one.
2. Guard binary-dependent tests with `_skip_if_missing(miner_binary)`.
3. Use `MockAPIServer` / `MockStratumServer` rather than connecting to external
   services.
4. Update the functional tests table in this document.

### Updating helper implementations

If you change the behaviour of any function in `test/utils/helpers.py`, update
the corresponding unit tests. Because `test_utils.py` now delegates to
`helpers.py`, thresholds and semantics live in exactly one place.
