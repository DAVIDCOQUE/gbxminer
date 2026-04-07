"""
Mock API server for testing GBXminer's built-in API.

This module provides a MockAPIServer class that simulates the miner's
API for testing purposes without requiring an actual running miner.
"""

import json
import socket
import threading
import time
from typing import Any, Callable, Dict, List, Optional


class MockAPIServer:
    """
    Mock API server that simulates GBXminer's built-in API.

    This server can be used to test API clients or to simulate
    API responses for integration testing.
    """

    DEFAULT_HOST = "127.0.0.1"
    DEFAULT_PORT = 4068

    def __init__(
        self,
        host: str = DEFAULT_HOST,
        port: int = DEFAULT_PORT,
        auto_start: bool = False
    ):
        """
        Initialize the mock API server.

        Args:
            host: Host address to bind to.
            port: Port number to listen on.
            auto_start: If True, automatically start the server.
        """
        self.host = host
        self.port = port
        self.socket: Optional[socket.socket] = None
        self.server_thread: Optional[threading.Thread] = None
        self.running = False

        # Default responses for various commands
        self._responses: Dict[str, Callable[[str], Dict[str, Any]]] = {
            'version': self._get_version_response,
            'summary': self._get_summary_response,
            'devs': self._get_devs_response,
            'devsdetail': self._get_devsdetail_response,
            'threads': self._get_threads_response,
            'pools': self._get_pools_response,
        }

        # Custom command handlers can be added
        self._custom_handlers: Dict[str, Callable[[str], Dict[str, Any]]] = {}

        # Simulated miner state
        self._miner_state = {
            'start_time': time.time(),
            'accepted_shares': 100,
            'rejected_shares': 2,
            'stale_shares': 0,
            'hardware_errors': 0,
            'getworks': 10,
            'found_blocks': 0,
            'utility': 1.0,
            'gpu_count': 1,
            'gpu_temp': 65.0,
            'gpu_fan_speed': 70,
            'gpu_clock': 1710,
            'memory_clock': 4700,
            'gpu_voltage': 0.950,
            'hashrate_mhs': 50.0,  # 50 MH/s for neoscrypt
        }

        if auto_start:
            self.start()

    def start(self, background: bool = True) -> 'MockAPIServer':
        """
        Start the mock API server.

        Args:
            background: If True, run in a background thread.

        Returns:
            self for method chaining.
        """
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.socket.bind((self.host, self.port))
        self.socket.listen(5)
        self.running = True

        if background:
            self.server_thread = threading.Thread(
                target=self._accept_connections,
                daemon=True
            )
            self.server_thread.start()

        return self

    def stop(self) -> None:
        """Stop the mock API server."""
        self.running = False

        if self.socket:
            try:
                self.socket.close()
            except OSError:
                pass
            self.socket = None

        if self.server_thread and self.server_thread.is_alive():
            self.server_thread.join(timeout=5)
            self.server_thread = None

    def _accept_connections(self) -> None:
        """Accept and handle incoming connections."""
        while self.running:
            try:
                self.socket.settimeout(1.0)
                try:
                    client_socket, address = self.socket.accept()
                except socket.timeout:
                    continue

                # Handle client in a separate thread
                client_thread = threading.Thread(
                    target=self._handle_client,
                    args=(client_socket,),
                    daemon=True
                )
                client_thread.start()
            except OSError:
                if self.running:
                    raise
                break

    def _handle_client(self, client_socket: socket.socket) -> None:
        """
        Handle a client connection.

        Args:
            client_socket: The client socket.
        """
        try:
            # Receive command
            data = client_socket.recv(4096).decode('utf-8').strip()

            if not data:
                return

            # Process commands (can be pipe-separated)
            commands = data.split('|')
            responses = []

            for command in commands:
                response = self._process_command(command.strip())
                responses.append(response)

            # Send response
            if len(responses) == 1:
                response_data = json.dumps(responses[0])
            else:
                response_data = json.dumps(responses)

            client_socket.sendall(response_data.encode('utf-8'))

        except (OSError, json.JSONDecodeError) as e:
            error_response = {
                'STATUS': {'SUCCESS': False, 'msg': str(e)},
                'error': str(e)
            }
            try:
                client_socket.sendall(json.dumps(error_response).encode('utf-8'))
            except OSError:
                pass
        finally:
            client_socket.close()

    def _process_command(self, command: str) -> Dict[str, Any]:
        """
        Process an API command and return the response.

        Args:
            command: The command string.

        Returns:
            Response dictionary.
        """
        # Check custom handlers first
        if command in self._custom_handlers:
            return self._custom_handlers[command](command)

        # Check built-in handlers
        if command in self._responses:
            return self._responses[command](command)

        # Unknown command
        return {
            'STATUS': {
                'SUCCESS': False,
                'msg': f'Unknown command: {command}',
                'code': 77,
            },
            'command': command,
        }

    def set_response_handler(
        self,
        command: str,
        handler: Callable[[str], Dict[str, Any]]
    ) -> None:
        """
        Set a custom response handler for a command.

        Args:
            command: The command name.
            handler: Function that takes command string and returns response dict.
        """
        self._custom_handlers[command] = handler

    def update_miner_state(self, **kwargs) -> None:
        """
        Update the simulated miner state.

        Args:
            **kwargs: Key-value pairs to update in the miner state.
        """
        self._miner_state.update(kwargs)

    def get_miner_state(self) -> Dict[str, Any]:
        """
        Get the current simulated miner state.

        Returns:
            Copy of the miner state dictionary.
        """
        return self._miner_state.copy()

    # Response generators

    def _get_version_response(self, command: str) -> Dict[str, Any]:
        """Generate version command response."""
        return {
            'STATUS': {
                'SUCCESS': True,
                'msg': 'Command sent to miner',
                'When': time.strftime('%Y-%m-%d %H:%M:%S'),
                'code': 11,
            },
            'VERSION': [
                {
                    'CGMINER': '5.6.0',
                    'API': '1.9',
                }
            ],
            'id': 1,
        }

    def _get_summary_response(self, command: str) -> Dict[str, Any]:
        """Generate summary command response."""
        state = self._miner_state
        elapsed = time.time() - state['start_time']

        return {
            'STATUS': {
                'SUCCESS': True,
                'msg': 'Command sent to miner',
                'When': time.strftime('%y%m%d %H:%M:%S'),
                'code': 11,
            },
            'SUMMARY': [
                {
                    'Elapsed': int(elapsed),
                    'MHS av': state['hashrate_mhs'],
                    'MHS 5s': state['hashrate_mhs'],
                    'Found Blocks': state['found_blocks'],
                    'Getworks': state['getworks'],
                    'Accepted': state['accepted_shares'],
                    'Rejected': state['rejected_shares'],
                    'Stale': state['stale_shares'],
                    'Hardware Errors': state['hardware_errors'],
                    'Utility': state['utility'],
                    'Discarded': 0,
                    'GPU Rejected': state['rejected_shares'],
                    'Network Blocks': 1000 + state['found_blocks'],
                    'Total MH': state['hashrate_mhs'] * elapsed / 3600,
                    'Work Utility': state['utility'],
                    'Best Share': 0,
                }
            ],
            'id': 1,
        }

    def _get_devs_response(self, command: str) -> Dict[str, Any]:
        """Generate devs command response."""
        state = self._miner_state
        gpu_count = state['gpu_count']

        devs = []
        for i in range(gpu_count):
            devs.append({
                'GPU': i,
                'Enabled': 'Y',
                'Status': 'Alive',
                'Temperature': state['gpu_temp'],
                'Fan Speed': state['gpu_fan_speed'],
                'Fan Percent': state['gpu_fan_speed'],
                'GPU Clock': state['gpu_clock'],
                'Memory Clock': state['memory_clock'],
                'GPU Voltage': state['gpu_voltage'],
                'GPU Activity': 100,
                'Powertune': 100,
                'MHS av': state['hashrate_mhs'],
                'MHS 5s': state['hashrate_mhs'],
                'Accepted': state['accepted_shares'] // gpu_count,
                'Rejected': state['rejected_shares'] // gpu_count,
                'Hardware Errors': state['hardware_errors'] // gpu_count,
                'Utility': state['utility'] / gpu_count,
                'GPU Rejected': state['rejected_shares'] // gpu_count,
            })

        return {
            'STATUS': {
                'SUCCESS': True,
                'msg': 'Command sent to miner',
                'When': time.strftime('%y%m%d %H:%M:%S'),
                'code': 11,
            },
            'DEVS': devs,
            'id': 1,
        }

    def _get_devsdetail_response(self, command: str) -> Dict[str, Any]:
        """Generate devsdetail command response."""
        state = self._miner_state

        return {
            'STATUS': {
                'SUCCESS': True,
                'msg': 'Command sent to miner',
                'When': time.strftime('%y%m%d %H:%M:%S'),
                'code': 11,
            },
            'DEVSDETAIL': [
                {
                    'GPU': i,
                    'Name': 'NVIDIA GeForce RTX 3080',
                    'Intensity': '20',
                    'Device ID': i,
                    'GPU Activity': 100,
                    'GPU Power': 220,
                    'GPU Fan Percent': state['gpu_fan_speed'],
                    'GPU Temp': state['gpu_temp'],
                    'GPU Clock': state['gpu_clock'],
                    'GPU Voltage': state['gpu_voltage'],
                    'Memory Clock': state['memory_clock'],
                    'GPU Mem Temp': state['gpu_temp'] + 5,
                    'PCIE Link': 'Gen3 x16',
                }
                for i in range(state['gpu_count'])
            ],
            'id': 1,
        }

    def _get_threads_response(self, command: str) -> Dict[str, Any]:
        """Generate threads command response."""
        state = self._miner_state

        threads = []
        for i in range(state['gpu_count']):
            threads.append({
                'Thread ID': i,
                'Status': 'Mining',
                'GPU': i,
                'Algorithm': 'neoscrypt',
                'Intensity': '20',
            })

        return {
            'STATUS': {
                'SUCCESS': True,
                'msg': 'Command sent to miner',
                'When': time.strftime('%y%m%d %H:%M:%S'),
                'code': 11,
            },
            'THREADS': threads,
            'id': 1,
        }

    def _get_pools_response(self, command: str) -> Dict[str, Any]:
        """Generate pools command response."""
        return {
            'STATUS': {
                'SUCCESS': True,
                'msg': 'Command sent to miner',
                'When': time.strftime('%y%m%d %H:%M:%S'),
                'code': 11,
            },
            'POOLS': [
                {
                    'POOL': 0,
                    'URL': 'stratum+tcp://pool.example.com:3333',
                    'Status': 'Alive',
                    'Priority': 0,
                    'Quota': 0,
                    'Long Poll': 'N',
                    'Getworks': 10,
                    'Accepted': 100,
                    'Rejected': 2,
                    'Stale': 0,
                    'Discarded': 0,
                    'Remote Failures': 0,
                    'Local Failures': 0,
                    'Work Utility': 1.0,
                }
            ],
            'id': 1,
        }

    def __enter__(self) -> 'MockAPIServer':
        """Context manager entry."""
        self.start()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb) -> None:
        """Context manager exit."""
        self.stop()
