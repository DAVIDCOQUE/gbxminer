# Copyright (c) 2026-2026 The GBXMiner developers
"""
Mock stratum server for testing GBXminer's stratum protocol implementation.

This module provides a MockStratumServer class that simulates a mining pool's
stratum protocol for testing purposes without requiring an actual pool connection.
"""

import json
import socket
import threading
import time
from typing import Any, Callable, Dict, List, Optional, Tuple


class MockStratumServer:
    """
    Mock stratum server that simulates a mining pool.

    This server implements the Stratum mining protocol and can be used
    to test the miner's stratum client implementation.
    """

    DEFAULT_HOST = "127.0.0.1"
    DEFAULT_PORT = 3333

    def __init__(
        self,
        host: str = DEFAULT_HOST,
        port: int = DEFAULT_PORT,
        difficulty: float = 1.0,
        auto_start: bool = False
    ):
        """
        Initialize the mock stratum server.

        Args:
            host: Host address to bind to.
            port: Port number to listen on.
            difficulty: Initial mining difficulty.
            auto_start: If True, automatically start the server.
        """
        self.host = host
        self.port = port
        self.difficulty = difficulty
        self.socket: Optional[socket.socket] = None
        self.server_thread: Optional[threading.Thread] = None
        self.running = False

        # Connected clients
        self._clients: Dict[int, socket.socket] = {}
        self._client_lock = threading.Lock()
        self._client_id = 0

        # Current job state
        self._current_job_id = 0
        self._current_job: Optional[Dict[str, Any]] = None
        self._job_lock = threading.Lock()

        # Job history
        self._submitted_shares: List[Dict[str, Any]] = []
        self._accepted_shares = 0
        self._rejected_shares = 0

        # Custom method handlers
        self._method_handlers: Dict[str, Callable[[Dict[str, Any]], Dict[str, Any]]] = {}

        # Connection callback
        self._on_connect_callback: Optional[Callable[[int], None]] = None
        self._on_share_callback: Optional[Callable[[Dict[str, Any]], None]] = None

        if auto_start:
            self.start()

    def start(self, background: bool = True) -> 'MockStratumServer':
        """
        Start the mock stratum server.

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
        """Stop the mock stratum server."""
        self.running = False

        # Close all client connections
        with self._client_lock:
            for client_socket in self._clients.values():
                try:
                    client_socket.close()
                except OSError:
                    pass
            self._clients.clear()

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

                # Register client
                with self._client_lock:
                    client_id = self._client_id
                    self._client_id += 1
                    self._clients[client_id] = client_socket

                if self._on_connect_callback:
                    self._on_connect_callback(client_id)

                # Handle client in a separate thread
                client_thread = threading.Thread(
                    target=self._handle_client,
                    args=(client_id, client_socket),
                    daemon=True
                )
                client_thread.start()
            except OSError:
                if self.running:
                    raise
                break

    def _handle_client(self, client_id: int, client_socket: socket.socket) -> None:
        """
        Handle a client connection.

        Args:
            client_id: Unique client identifier.
            client_socket: The client socket.
        """
        buffer = ""

        try:
            while self.running:
                try:
                    client_socket.settimeout(1.0)
                    data = client_socket.recv(4096).decode('utf-8')

                    if not data:
                        break

                    buffer += data

                    # Process complete messages (delimited by newline)
                    while '\n' in buffer:
                        line, buffer = buffer.split('\n', 1)
                        line = line.strip()

                        if line:
                            response = self._process_message(client_id, line)
                            if response:
                                client_socket.sendall(
                                    (json.dumps(response) + '\n').encode('utf-8')
                                )

                except socket.timeout:
                    continue
                except OSError:
                    break

        finally:
            # Clean up client
            with self._client_lock:
                if client_id in self._clients:
                    del self._clients[client_id]
            try:
                client_socket.close()
            except OSError:
                pass

    def _process_message(self, client_id: int, message: str) -> Optional[Dict[str, Any]]:
        """
        Process a stratum protocol message.

        Args:
            client_id: Unique client identifier.
            message: JSON-RPC message string.

        Returns:
            Response dictionary or None.
        """
        try:
            msg = json.loads(message)
        except json.JSONDecodeError:
            return {
                'id': None,
                'result': None,
                'error': [1, 'Parse error', None]
            }

        # Extract message fields
        msg_id = msg.get('id')
        method = msg.get('method')
        params = msg.get('params', [])

        # Handle known methods
        if method == 'mining.subscribe':
            return self._handle_subscribe(msg_id)

        elif method == 'mining.authorize':
            return self._handle_authorize(msg_id, params)

        elif method == 'mining.submit':
            return self._handle_submit(msg_id, params)

        elif method == 'mining.extranonce.subscribe':
            return self._handle_extranonce_subscribe(msg_id)

        elif method in self._method_handlers:
            return self._method_handlers[method]({'id': msg_id, 'params': params})

        else:
            return {
                'id': msg_id,
                'result': None,
                'error': [20, 'Unknown method', method]
            }

    def _handle_subscribe(self, msg_id: Any) -> Dict[str, Any]:
        """Handle mining.subscribe request."""
        # Generate extranonce1 and extranonce2_size
        extranonce1 = "00000000"
        extranonce2_size = 4

        # Send difficulty set notification
        self._send_notification('mining.set_difficulty', [self.difficulty])

        # Send initial job notification
        self._send_new_job()

        return {
            'id': msg_id,
            'result': [
                [
                    ['mining.set_difficulty', 'mining.notify'],
                ],
                extranonce1,
                extranonce2_size
            ],
            'error': None
        }

    def _handle_authorize(self, msg_id: Any, params: List[str]) -> Dict[str, Any]:
        """Handle mining.authorize request."""
        if len(params) < 2:
            return {
                'id': msg_id,
                'result': False,
                'error': [20, 'Invalid params', None]
            }

        username = params[0]
        password = params[1]

        # Accept all authorizations (this is a mock server)
        return {
            'id': msg_id,
            'result': True,
            'error': None
        }

    def _handle_submit(self, msg_id: Any, params: List[Any]) -> Dict[str, Any]:
        """Handle mining.submit request."""
        if len(params) < 5:
            return {
                'id': msg_id,
                'result': None,
                'error': [20, 'Invalid params', None]
            }

        username = params[0]
        job_id = params[1]
        extranonce2 = params[2]
        ntime = params[3]
        nonce = params[4]

        share_data = {
            'username': username,
            'job_id': job_id,
            'extranonce2': extranonce2,
            'ntime': ntime,
            'nonce': nonce,
            'timestamp': time.time(),
        }

        self._submitted_shares.append(share_data)

        if self._on_share_callback:
            self._on_share_callback(share_data)

        # Accept most shares, reject a small percentage for testing
        # For mock purposes, accept all shares
        self._accepted_shares += 1

        return {
            'id': msg_id,
            'result': True,
            'error': None
        }

    def _handle_extranonce_subscribe(self, msg_id: Any) -> Dict[str, Any]:
        """Handle mining.extranonce.subscribe request."""
        return {
            'id': msg_id,
            'result': True,
            'error': None
        }

    def _send_notification(self, method: str, params: List[Any]) -> None:
        """
        Send a notification to all connected clients.

        Args:
            method: Method name.
            params: Method parameters.
        """
        notification = {
            'id': None,
            'method': method,
            'params': params
        }

        message = (json.dumps(notification) + '\n').encode('utf-8')

        with self._client_lock:
            for client_socket in self._clients.values():
                try:
                    client_socket.sendall(message)
                except OSError:
                    pass

    def _send_new_job(self) -> None:
        """Send a new mining job to all connected clients."""
        with self._job_lock:
            self._current_job_id += 1
            self._current_job = self._generate_job(self._current_job_id)

        job = self._current_job

        params = [
            job['job_id'],
            job['prev_hash'],
            job['coinbase1'],
            job['coinbase2'],
            job['merkle_branch'],
            job['version'],
            job['nbits'],
            job['ntime'],
            job['clean_jobs']
        ]

        self._send_notification('mining.notify', params)

    def _generate_job(self, job_id: int) -> Dict[str, Any]:
        """
        Generate a new mining job.

        Args:
            job_id: Unique job identifier.

        Returns:
            Job dictionary.
        """
        return {
            'job_id': f'{job_id:04x}',
            'prev_hash': '0' * 64,
            'coinbase1': '01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff',
            'coinbase2': 'ffffffffffffffff0100f2052a010000001976a914000000000000000000000000000000000000000088ac00000000',
            'merkle_branch': [],
            'version': 536870912,
            'nbits': '1a00ffff',
            'ntime': format(int(time.time()), '08x'),
            'clean_jobs': True
        }

    def set_difficulty(self, difficulty: float) -> None:
        """
        Set the mining difficulty and notify all clients.

        Args:
            difficulty: New difficulty value.
        """
        self.difficulty = difficulty
        self._send_notification('mining.set_difficulty', [difficulty])

    def send_new_job(self) -> None:
        """Manually trigger sending a new job to all clients."""
        self._send_new_job()

    def get_current_job(self) -> Optional[Dict[str, Any]]:
        """
        Get the current mining job.

        Returns:
            Current job dictionary or None.
        """
        with self._job_lock:
            return self._current_job.copy() if self._current_job else None

    def get_submitted_shares(self) -> List[Dict[str, Any]]:
        """
        Get list of all submitted shares.

        Returns:
            List of submitted share dictionaries.
        """
        return self._submitted_shares.copy()

    def get_stats(self) -> Dict[str, Any]:
        """
        Get server statistics.

        Returns:
            Statistics dictionary.
        """
        return {
            'connected_clients': len(self._clients),
            'submitted_shares': len(self._submitted_shares),
            'accepted_shares': self._accepted_shares,
            'rejected_shares': self._rejected_shares,
            'current_job_id': self._current_job_id,
            'difficulty': self.difficulty,
        }

    def set_method_handler(
        self,
        method: str,
        handler: Callable[[Dict[str, Any]], Dict[str, Any]]
    ) -> None:
        """
        Set a custom handler for a stratum method.

        Args:
            method: Method name.
            handler: Handler function that takes request dict and returns response dict.
        """
        self._method_handlers[method] = handler

    def on_connect(self, callback: Callable[[int], None]) -> None:
        """
        Set callback for client connections.

        Args:
            callback: Function called with client_id when a client connects.
        """
        self._on_connect_callback = callback

    def on_share(self, callback: Callable[[Dict[str, Any]], None]) -> None:
        """
        Set callback for share submissions.

        Args:
            callback: Function called with share data when a share is submitted.
        """
        self._on_share_callback = callback

    def get_connected_client_count(self) -> int:
        """
        Get the number of connected clients.

        Returns:
            Number of connected clients.
        """
        with self._client_lock:
            return len(self._clients)

    def broadcast_message(self, message: Dict[str, Any]) -> None:
        """
        Broadcast a custom message to all connected clients.

        Args:
            message: Message dictionary to broadcast.
        """
        data = (json.dumps(message) + '\n').encode('utf-8')

        with self._client_lock:
            for client_socket in self._clients.values():
                try:
                    client_socket.sendall(data)
                except OSError:
                    pass

    def __enter__(self) -> 'MockStratumServer':
        """Context manager entry."""
        self.start()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb) -> None:
        """Context manager exit."""
        self.stop()


class MockStratumJob:
    """
    Helper class representing a mock stratum mining job.

    This class provides a convenient way to create and manipulate
    stratum job data for testing.
    """

    def __init__(
        self,
        job_id: str = "0001",
        prev_hash: str = None,
        coinbase1: str = None,
        coinbase2: str = None,
        merkle_branch: List[str] = None,
        version: int = 536870912,
        nbits: str = "1a00ffff",
        ntime: str = None,
        clean_jobs: bool = True
    ):
        """
        Initialize a mock stratum job.

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
        """
        self.job_id = job_id
        self.prev_hash = prev_hash or '0' * 64
        self.coinbase1 = coinbase1 or '01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff'
        self.coinbase2 = coinbase2 or 'ffffffffffffffff0100f2052a010000001976a914000000000000000000000000000000000000000088ac00000000'
        self.merkle_branch = merkle_branch or []
        self.version = version
        self.nbits = nbits
        self.ntime = ntime or format(int(time.time()), '08x')
        self.clean_jobs = clean_jobs

    def to_dict(self) -> Dict[str, Any]:
        """
        Convert job to dictionary format.

        Returns:
            Job dictionary.
        """
        return {
            'job_id': self.job_id,
            'prev_hash': self.prev_hash,
            'coinbase1': self.coinbase1,
            'coinbase2': self.coinbase2,
            'merkle_branch': self.merkle_branch,
            'version': self.version,
            'nbits': self.nbits,
            'ntime': self.ntime,
            'clean_jobs': self.clean_jobs
        }

    def to_notify_params(self) -> List[Any]:
        """
        Convert job to mining.notify params format.

        Returns:
            List of parameters for mining.notify notification.
        """
        return [
            self.job_id,
            self.prev_hash,
            self.coinbase1,
            self.coinbase2,
            self.merkle_branch,
            self.version,
            self.nbits,
            self.ntime,
            self.clean_jobs
        ]

    def create_share_submission(
        self,
        username: str = "testuser",
        nonce: str = "00000001"
    ) -> Dict[str, Any]:
        """
        Create a share submission message for this job.

        Args:
            username: Miner username.
            nonce: Found nonce (hex string).

        Returns:
            mining.submit message dictionary.
        """
        return {
            'id': 1,
            'method': 'mining.submit',
            'params': [
                username,
                self.job_id,
                '0' * 8,  # extranonce2
                self.ntime,
                nonce
            ]
        }
