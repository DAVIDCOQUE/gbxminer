# Copyright (c) 2026-2026 The GBXMiner developers
"""
Config file parsing utilities for GBXminer tests.

This module provides a ConfigParser class for parsing and validating
JSON configuration files used by GBXminer.
"""

import json
import re
from pathlib import Path
from typing import Any, Dict, List, Optional, Union


class ConfigValidationError(Exception):
    """Exception raised when configuration validation fails."""
    def __init__(self, message: str, field: Optional[str] = None):
        self.message = message
        self.field = field
        super().__init__(self.message)


class ConfigParser:
    """
    Parser and validator for GBXminer JSON configuration files.

    Supports both single-pool and multi-pool configurations.
    """

    # Valid URL schemes for mining pools
    VALID_URL_SCHEMES = [
        'stratum+tcp',
        'stratum+udp',
        'stratum+tcps',
        'stratum+udps',
        'http',
        'https',
        'getwork',
    ]

    # Required fields for a valid configuration
    REQUIRED_FIELDS = ['url', 'user', 'pass']

    # Optional fields with their default values
    OPTIONAL_FIELDS = {
        'algo': 'auto',
        'intensity': 20,
        'api-bind': '127.0.0.1:4068',
        'statsavg': 20,
        'max-log-rate': 60,
        'quiet': False,
        'debug': False,
        'protocol': False,
        'cpu-priority': 3,
        'devices': 'all',
    }

    # Intensity range validation
    MIN_INTENSITY = 0
    MAX_INTENSITY = 30

    def __init__(self, config_data: Optional[Dict[str, Any]] = None):
        """
        Initialize the ConfigParser.

        Args:
            config_data: Optional dictionary containing configuration data.
        """
        self.config: Dict[str, Any] = {}
        self.pools: List[Dict[str, Any]] = []

        if config_data is not None:
            self.load_dict(config_data)

    def load_file(self, filepath: Union[str, Path]) -> 'ConfigParser':
        """
        Load configuration from a JSON file.

        Args:
            filepath: Path to the JSON configuration file.

        Returns:
            self for method chaining.

        Raises:
            FileNotFoundError: If the config file doesn't exist.
            json.JSONDecodeError: If the file contains invalid JSON.
            ConfigValidationError: If validation fails.
        """
        path = Path(filepath)
        if not path.exists():
            raise FileNotFoundError(f"Configuration file not found: {filepath}")

        with open(path, 'r') as f:
            try:
                config_data = json.load(f)
            except json.JSONDecodeError as e:
                raise ConfigValidationError(f"Invalid JSON syntax: {e}")

        return self.load_dict(config_data)

    def load_dict(self, config_data: Dict[str, Any]) -> 'ConfigParser':
        """
        Load configuration from a dictionary.

        Args:
            config_data: Dictionary containing configuration data.

        Returns:
            self for method chaining.

        Raises:
            ConfigValidationError: If validation fails.
        """
        if not isinstance(config_data, dict):
            raise ConfigValidationError("Configuration must be a JSON object")

        # Check for multi-pool configuration
        if 'pools' in config_data:
            self._parse_multi_pool_config(config_data)
        else:
            self._parse_single_pool_config(config_data)

        return self

    def _parse_single_pool_config(self, config_data: Dict[str, Any]) -> None:
        """
        Parse a single-pool configuration.

        Args:
            config_data: Dictionary containing single-pool configuration.

        Raises:
            ConfigValidationError: If validation fails.
        """
        # Validate required fields
        for field in self.REQUIRED_FIELDS:
            if field not in config_data:
                raise ConfigValidationError(
                    f"Missing required field: '{field}'",
                    field=field
                )

        # Validate URL format
        self._validate_url(config_data['url'])

        # Validate optional fields
        if 'intensity' in config_data:
            self._validate_intensity(config_data['intensity'])

        if 'api-bind' in config_data:
            self._validate_api_bind(config_data['api-bind'])

        if 'devices' in config_data:
            self._validate_devices(config_data['devices'])

        # Store config and create single pool entry
        self.config = config_data.copy()
        self.pools = [{
            'url': config_data['url'],
            'user': config_data['user'],
            'pass': config_data['pass'],
            'algo': config_data.get('algo', self.OPTIONAL_FIELDS['algo']),
        }]

    def _parse_multi_pool_config(self, config_data: Dict[str, Any]) -> None:
        """
        Parse a multi-pool configuration.

        Args:
            config_data: Dictionary containing multi-pool configuration.

        Raises:
            ConfigValidationError: If validation fails.
        """
        pools = config_data.get('pools', [])

        if not isinstance(pools, list):
            raise ConfigValidationError("'pools' must be an array")

        if len(pools) == 0:
            raise ConfigValidationError("'pools' array must not be empty")

        # Validate each pool
        for i, pool in enumerate(pools):
            if not isinstance(pool, dict):
                raise ConfigValidationError(
                    f"Pool at index {i} must be an object",
                    field=f"pools[{i}]"
                )

            for field in self.REQUIRED_FIELDS:
                if field not in pool:
                    raise ConfigValidationError(
                        f"Missing required field '{field}' in pool {i}",
                        field=f"pools[{i}].{field}"
                    )

            self._validate_url(pool['url'])

            if 'intensity' in pool:
                self._validate_intensity(pool['intensity'])

        self.config = config_data.copy()
        self.pools = pools

    def _validate_url(self, url: str) -> None:
        """
        Validate a pool URL format.

        Args:
            url: URL string to validate.

        Raises:
            ConfigValidationError: If the URL format is invalid.
        """
        if not url or not isinstance(url, str):
            raise ConfigValidationError("URL must be a non-empty string")

        # Parse URL scheme
        scheme_match = re.match(r'^([a-zA-Z][a-zA-Z0-9+.-]*)://', url)

        if not scheme_match:
            raise ConfigValidationError(
                f"Invalid URL format: missing scheme (e.g., stratum+tcp://)",
                field='url'
            )

        scheme = scheme_match.group(1).lower()
        if scheme not in self.VALID_URL_SCHEMES:
            raise ConfigValidationError(
                f"Invalid URL scheme: '{scheme}'. Valid schemes: {', '.join(self.VALID_URL_SCHEMES)}",
                field='url'
            )

        # Check for host and port
        host_port_pattern = r'^[a-zA-Z][a-zA-Z0-9+.-]*://([^/:]+)(?::(\d+))?'
        match = re.match(host_port_pattern, url)

        if not match:
            raise ConfigValidationError(
                f"Invalid URL format: could not parse host",
                field='url'
            )

        port = match.group(2)
        if port is not None:
            port_int = int(port)
            if port_int < 1 or port_int > 65535:
                raise ConfigValidationError(
                    f"Invalid port number: {port_int}. Must be 1-65535",
                    field='url'
                )

    def _validate_intensity(self, intensity: Any) -> None:
        """
        Validate intensity value.

        Args:
            intensity: Intensity value to validate.

        Raises:
            ConfigValidationError: If the intensity is invalid.
        """
        try:
            intensity_int = int(intensity)
        except (ValueError, TypeError):
            raise ConfigValidationError(
                f"Intensity must be an integer, got: {type(intensity).__name__}",
                field='intensity'
            )

        if intensity_int < self.MIN_INTENSITY or intensity_int > self.MAX_INTENSITY:
            raise ConfigValidationError(
                f"Intensity must be between {self.MIN_INTENSITY} and {self.MAX_INTENSITY}, got: {intensity_int}",
                field='intensity'
            )

    def _validate_api_bind(self, api_bind: str) -> None:
        """
        Validate API bind address format.

        Args:
            api_bind: API bind address string to validate.

        Raises:
            ConfigValidationError: If the format is invalid.
        """
        if not api_bind or not isinstance(api_bind, str):
            raise ConfigValidationError(
                "API bind must be a non-empty string in format 'host:port'",
                field='api-bind'
            )

        # Pattern: host:port
        pattern = r'^([^:]+):(\d+)$'
        match = re.match(pattern, api_bind)

        if not match:
            raise ConfigValidationError(
                f"Invalid API bind format: '{api_bind}'. Expected 'host:port'",
                field='api-bind'
            )

        port = int(match.group(2))
        if port < 1 or port > 65535:
            raise ConfigValidationError(
                f"Invalid API bind port: {port}. Must be 1-65535",
                field='api-bind'
            )

    def _validate_devices(self, devices: Any) -> None:
        """
        Validate device configuration.

        Args:
            devices: Device configuration (string or list).

        Raises:
            ConfigValidationError: If the configuration is invalid.
        """
        if isinstance(devices, str):
            if devices.lower() == 'all':
                return

            # Comma-separated list of device IDs
            try:
                device_ids = [int(d.strip()) for d in devices.split(',')]
                if any(d < 0 for d in device_ids):
                    raise ValueError("Negative device IDs not allowed")
            except ValueError:
                raise ConfigValidationError(
                    f"Invalid device configuration: '{devices}'. "
                    "Use 'all' or comma-separated device IDs (e.g., '0,1,2')",
                    field='devices'
                )
        elif isinstance(devices, list):
            for d in devices:
                if not isinstance(d, int) or d < 0:
                    raise ConfigValidationError(
                        f"Invalid device ID: {d}. Must be a non-negative integer",
                        field='devices'
                    )
        else:
            raise ConfigValidationError(
                f"Invalid device configuration type: {type(devices).__name__}",
                field='devices'
            )

    def get(self, key: str, default: Any = None) -> Any:
        """
        Get a configuration value.

        Args:
            key: Configuration key.
            default: Default value if key is not found.

        Returns:
            The configuration value or default.
        """
        return self.config.get(key, default)

    def get_pool(self, index: int = 0) -> Optional[Dict[str, Any]]:
        """
        Get a pool configuration by index.

        Args:
            index: Pool index.

        Returns:
            Pool configuration dictionary or None if not found.
        """
        if 0 <= index < len(self.pools):
            return self.pools[index]
        return None

    @property
    def pool_count(self) -> int:
        """Return the number of configured pools."""
        return len(self.pools)

    def to_dict(self) -> Dict[str, Any]:
        """
        Return the configuration as a dictionary.

        Returns:
            Configuration dictionary.
        """
        return self.config.copy()

    def validate(self) -> bool:
        """
        Validate the current configuration.

        Returns:
            True if configuration is valid.

        Raises:
            ConfigValidationError: If validation fails.
        """
        if not self.config:
            raise ConfigValidationError("No configuration loaded")

        # Re-validate to catch any issues
        if 'pools' in self.config:
            self._parse_multi_pool_config(self.config)
        else:
            self._parse_single_pool_config(self.config)

        return True
