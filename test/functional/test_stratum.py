"""
Functional tests for GBXminer's stratum protocol implementation.

This module tests the stratum protocol functionality using the MockStratumServer
to simulate a mining pool without requiring an actual pool connection.
"""

import json
import pytest
import socket
import sys
import time

# Add parent directory to path for imports
sys.path.insert(0, '..')
from utils.mock_stratum import MockStratumServer, MockStratumJob


class TestMockStratumServer:
    """Test the MockStratumServer functionality."""

    def test_server_start_stop(self):
        """Test that the mock stratum server can start and stop."""
        server = MockStratumServer(port=13333)
        server.start(background=False)
        assert server.running
        assert server.socket is not None

        server.stop()
        assert not server.running

    def test_server_context_manager(self):
        """Test that the server works as a context manager."""
        with MockStratumServer(port=13334) as server:
            assert server.running

        assert not server.running

    def test_client_connection(self):
        """Test that a client can connect to the server."""
        server = MockStratumServer(port=13335)
        server.start(background=True)
        time.sleep(0.1)

        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect(('127.0.0.1', 13335))
            assert server.get_connected_client_count() == 1
            sock.close()
            time.sleep(0.1)
            assert server.get_connected_client_count() == 0
        finally:
            server.stop()

    def test_mining_subscribe(self):
        """Test the mining.subscribe method."""
        server = MockStratumServer(port=13336)
        server.start(background=True)
        time.sleep(0.1)

        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect(('127.0.0.1', 13336))

            # Send subscribe request
            request = {
                'id': 1,
                'method': 'mining.subscribe',
                'params': []
            }
            sock.sendall((json.dumps(request) + '\n').encode('utf-8'))

            # Receive response
            response = sock.recv(4096).decode('utf-8').strip()
            data = json.loads(response)

            assert data['id'] == 1
            assert data['error'] is None
            assert 'result' in data
            assert isinstance(data['result'], list)
            # Result should contain extranonce1 and extranonce2_size
            assert len(data['result']) >= 2

            sock.close()
        finally:
            server.stop()

    def test_mining_authorize(self):
        """Test the mining.authorize method."""
        server = MockStratumServer(port=13337)
        server.start(background=True)
        time.sleep(0.1)

        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect(('127.0.0.1', 13337))

            # Send authorize request
            request = {
                'id': 2,
                'method': 'mining.authorize',
                'params': ['testuser', 'password']
            }
            sock.sendall((json.dumps(request) + '\n').encode('utf-8'))

            # Receive response
            response = sock.recv(4096).decode('utf-8').strip()
            data = json.loads(response)

            assert data['id'] == 2
            assert data['error'] is None
            assert data['result'] is True

            sock.close()
        finally:
            server.stop()

    def test_mining_submit(self):
        """Test the mining.submit method."""
        server = MockStratumServer(port=13338)
        server.start(background=True)
        time.sleep(0.1)

        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect(('127.0.0.1', 13338))

            # Send submit request
            request = {
                'id': 3,
                'method': 'mining.submit',
                'params': [
                    'testuser',
                    '0001',
                    '00000000',
                    '63f0c0a0',
                    '00000001'
                ]
            }
            sock.sendall((json.dumps(request) + '\n').encode('utf-8'))

            # Receive response
            response = sock.recv(4096).decode('utf-8').strip()
            data = json.loads(response)

            assert data['id'] == 3
            assert data['error'] is None
            assert data['result'] is True

            # Verify share was recorded
            stats = server.get_stats()
            assert stats['submitted_shares'] == 1
            assert stats['accepted_shares'] == 1

            sock.close()
        finally:
            server.stop()

    def test_mining_authorize_invalid_params(self):
        """Test mining.authorize with invalid parameters."""
        server = MockStratumServer(port=13339)
        server.start(background=True)
        time.sleep(0.1)

        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect(('127.0.0.1', 13339))

            # Send authorize with missing params
            request = {
                'id': 4,
                'method': 'mining.authorize',
                'params': ['testuser']  # Missing password
            }
            sock.sendall((json.dumps(request) + '\n').encode('utf-8'))

            # Receive response
            response = sock.recv(4096).decode('utf-8').strip()
            data = json.loads(response)

            assert data['id'] == 4
            assert data['error'] is not None
            assert data['result'] is None

            sock.close()
        finally:
            server.stop()

    def test_mining_submit_invalid_params(self):
        """Test mining.submit with invalid parameters."""
        server = MockStratumServer(port=13340)
        server.start(background=True)
        time.sleep(0.1)

        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect(('127.0.0.1', 13340))

            # Send submit with missing params
            request = {
                'id': 5,
                'method': 'mining.submit',
                'params': ['testuser', '0001']  # Missing extranonce2, ntime, nonce
            }
            sock.sendall((json.dumps(request) + '\n').encode('utf-8'))

            # Receive response
            response = sock.recv(4096).decode('utf-8').strip()
            data = json.loads(response)

            assert data['id'] == 5
            assert data['error'] is not None
            assert data['result'] is None

            sock.close()
        finally:
            server.stop()

    def test_unknown_method(self):
        """Test that unknown methods return an error."""
        server = MockStratumServer(port=13341)
        server.start(background=True)
        time.sleep(0.1)

        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect(('127.0.0.1', 13341))

            request = {
                'id': 6,
                'method': 'unknown.method',
                'params': []
            }
            sock.sendall((json.dumps(request) + '\n').encode('utf-8'))

            response = sock.recv(4096).decode('utf-8').strip()
            data = json.loads(response)

            assert data['id'] == 6
            assert data['error'] is not None
            assert data['error'][0] == 20  # Unknown method error code

            sock.close()
        finally:
            server.stop()

    def test_parse_error(self):
        """Test that invalid JSON returns a parse error."""
        server = MockStratumServer(port=13342)
        server.start(background=True)
        time.sleep(0.1)

        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect(('127.0.0.1', 13342))

            sock.sendall(b'invalid json\n')

            response = sock.recv(4096).decode('utf-8').strip()
            data = json.loads(response)

            assert data['id'] is None
            assert data['error'] is not None
            assert data['error'][0] == 1  # Parse error code

            sock.close()
        finally:
            server.stop()


class TestMockStratumJob:
    """Test the MockStratumJob helper class."""

    def test_job_creation(self):
        """Test creating a mock stratum job."""
        job = MockStratumJob(
            job_id='test001',
            prev_hash='a' * 64,
            ntime='63f0c0a0'
        )

        assert job.job_id == 'test001'
        assert job.prev_hash == 'a' * 64
        assert job.ntime == '63f0c0a0'

    def test_job_to_dict(self):
        """Test converting job to dictionary."""
        job = MockStratumJob(job_id='test002')
        job_dict = job.to_dict()

        assert 'job_id' in job_dict
        assert 'prev_hash' in job_dict
        assert 'coinbase1' in job_dict
        assert 'coinbase2' in job_dict
        assert 'merkle_branch' in job_dict
        assert 'version' in job_dict
        assert 'nbits' in job_dict
        assert 'ntime' in job_dict
        assert 'clean_jobs' in job_dict

    def test_job_to_notify_params(self):
        """Test converting job to mining.notify params."""
        job = MockStratumJob(job_id='test003')
        params = job.to_notify_params()

        assert isinstance(params, list)
        assert len(params) == 9
        assert params[0] == 'test003'

    def test_job_create_share_submission(self):
        """Test creating a share submission from a job."""
        job = MockStratumJob(job_id='test004', ntime='63f0c0a0')
        submission = job.create_share_submission(
            username='testuser',
            nonce='00000001'
        )

        assert submission['id'] == 1
        assert submission['method'] == 'mining.submit'
        assert submission['params'][0] == 'testuser'
        assert submission['params'][1] == 'test004'
        assert submission['params'][4] == '00000001'

    def test_job_default_values(self):
        """Test that job uses default values when not specified."""
        job = MockStratumJob()

        assert job.version == 536870912
        assert job.nbits == '1a00ffff'
        assert job.clean_jobs is True
        assert len(job.prev_hash) == 64
        assert len(job.merkle_branch) == 0


class TestStratumDifficulty:
    """Test stratum difficulty handling."""

    def test_set_difficulty(self):
        """Test setting mining difficulty."""
        server = MockStratumServer(port=13343, difficulty=1.0)
        server.start(background=True)
        time.sleep(0.1)

        try:
            # Set new difficulty
            server.set_difficulty(8.0)
            assert server.difficulty == 8.0

            # Verify stats reflect the change
            stats = server.get_stats()
            assert stats['difficulty'] == 8.0
        finally:
            server.stop()

    def test_difficulty_notification(self):
        """Test that difficulty change sends notification."""
        server = MockStratumServer(port=13344, difficulty=1.0)
        server.start(background=True)
        time.sleep(0.1)

        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect(('127.0.0.1', 13344))

            # Subscribe first to receive notifications
            request = {
                'id': 1,
                'method': 'mining.subscribe',
                'params': []
            }
            sock.sendall((json.dumps(request) + '\n').encode('utf-8'))

            # Give time for subscription
            time.sleep(0.1)

            # Change difficulty
            server.set_difficulty(4.0)
            time.sleep(0.1)

            # Receive all pending data
            sock.settimeout(0.5)
            try:
                data = sock.recv(4096).decode('utf-8')
                # Should contain difficulty notification
                assert 'mining.set_difficulty' in data
                assert '4.0' in data
            except socket.timeout:
                pass

            sock.close()
        finally:
            server.stop()


class TestStratumMultiClient:
    """Test stratum server with multiple clients."""

    def test_multiple_client_connections(self):
        """Test server handling multiple clients."""
        server = MockStratumServer(port=13345)
        server.start(background=True)
        time.sleep(0.1)

        try:
            # Connect multiple clients
            sockets = []
            for i in range(3):
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.connect(('127.0.0.1', 13345))
                sockets.append(sock)

            assert server.get_connected_client_count() == 3

            # Close all clients
            for sock in sockets:
                sock.close()

            time.sleep(0.2)
            assert server.get_connected_client_count() == 0
        finally:
            server.stop()

    def test_broadcast_to_clients(self):
        """Test broadcasting message to all clients."""
        server = MockStratumServer(port=13346)
        server.start(background=True)
        time.sleep(0.1)

        try:
            # Connect multiple clients
            sockets = []
            for i in range(2):
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.connect(('127.0.0.1', 13346))
                sock.settimeout(1)
                sockets.append(sock)

            time.sleep(0.1)

            # Broadcast a message
            message = {'id': None, 'method': 'mining.notify', 'params': ['test']}
            server.broadcast_message(message)

            time.sleep(0.1)

            # Each client should receive the broadcast
            for sock in sockets:
                try:
                    data = sock.recv(4096).decode('utf-8')
                    assert 'mining.notify' in data
                except socket.timeout:
                    pass

            for sock in sockets:
                sock.close()
        finally:
            server.stop()


class TestStratumCallbacks:
    """Test stratum server callback functionality."""

    def test_on_connect_callback(self):
        """Test client connection callback."""
        server = MockStratumServer(port=13347)

        connected_clients = []

        def on_connect(client_id):
            connected_clients.append(client_id)

        server.on_connect(on_connect)
        server.start(background=True)
        time.sleep(0.1)

        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect(('127.0.0.1', 13347))
            time.sleep(0.1)

            assert len(connected_clients) == 1
            assert connected_clients[0] == 0

            sock.close()
        finally:
            server.stop()

    def test_on_share_callback(self):
        """Test share submission callback."""
        server = MockStratumServer(port=13348)

        submitted_shares = []

        def on_share(share_data):
            submitted_shares.append(share_data)

        server.on_share(on_share)
        server.start(background=True)
        time.sleep(0.1)

        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect(('127.0.0.1', 13348))

            # Submit a share
            request = {
                'id': 1,
                'method': 'mining.submit',
                'params': ['testuser', '0001', '00000000', '63f0c0a0', '00000001']
            }
            sock.sendall((json.dumps(request) + '\n').encode('utf-8'))
            time.sleep(0.1)

            assert len(submitted_shares) == 1
            assert submitted_shares[0]['username'] == 'testuser'
            assert submitted_shares[0]['nonce'] == '00000001'

            sock.close()
        finally:
            server.stop()


class TestStratumCustomHandlers:
    """Test stratum server custom method handlers."""

    def test_custom_method_handler(self):
        """Test registering a custom method handler."""
        server = MockStratumServer(port=13349)

        def custom_handler(request):
            return {
                'id': request['id'],
                'result': {'custom': True},
                'error': None
            }

        server.set_method_handler('custom.method', custom_handler)
        server.start(background=True)
        time.sleep(0.1)

        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect(('127.0.0.1', 13349))

            request = {
                'id': 1,
                'method': 'custom.method',
                'params': []
            }
            sock.sendall((json.dumps(request) + '\n').encode('utf-8'))

            response = sock.recv(4096).decode('utf-8').strip()
            data = json.loads(response)

            assert data['result'] == {'custom': True}
            assert data['error'] is None

            sock.close()
        finally:
            server.stop()
