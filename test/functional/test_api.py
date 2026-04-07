"""
Functional tests for GBXminer's built-in API.

This module tests the miner's API functionality using the MockAPIServer
to simulate API responses without requiring an actual running miner.
"""

import json
import pytest
import socket
import sys
import time

# Add parent directory to path for imports
sys.path.insert(0, '..')
from utils.mock_api import MockAPIServer


class TestMockAPIServer:
    """Test the MockAPIServer functionality."""

    def test_server_start_stop(self):
        """Test that the mock API server can start and stop."""
        server = MockAPIServer(port=14068)
        server.start(background=False)
        assert server.running
        assert server.socket is not None

        server.stop()
        assert not server.running

    def test_server_context_manager(self):
        """Test that the server works as a context manager."""
        with MockAPIServer(port=14069) as server:
            assert server.running

        assert not server.running

    def test_version_command(self):
        """Test the version API command."""
        server = MockAPIServer(port=14070)
        server.start(background=True)
        time.sleep(0.1)  # Give server time to start

        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect(('127.0.0.1', 14070))
            sock.sendall(b'version')
            response = sock.recv(4096).decode('utf-8')
            sock.close()

            data = json.loads(response)
            assert 'STATUS' in data
            assert 'VERSION' in data
            assert data['STATUS']['SUCCESS'] is True
        finally:
            server.stop()

    def test_summary_command(self):
        """Test the summary API command."""
        server = MockAPIServer(port=14071)
        server.start(background=True)
        time.sleep(0.1)

        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect(('127.0.0.1', 14071))
            sock.sendall(b'summary')
            response = sock.recv(4096).decode('utf-8')
            sock.close()

            data = json.loads(response)
            assert 'STATUS' in data
            assert 'SUMMARY' in data
            assert data['STATUS']['SUCCESS'] is True

            summary = data['SUMMARY'][0]
            assert 'Elapsed' in summary
            assert 'MHS av' in summary
            assert 'Accepted' in summary
            assert 'Rejected' in summary
        finally:
            server.stop()

    def test_devs_command(self):
        """Test the devs API command."""
        server = MockAPIServer(port=14072)
        server.start(background=True)
        time.sleep(0.1)

        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect(('127.0.0.1', 14072))
            sock.sendall(b'devs')
            response = sock.recv(4096).decode('utf-8')
            sock.close()

            data = json.loads(response)
            assert 'STATUS' in data
            assert 'DEVS' in data
            assert isinstance(data['DEVS'], list)
            assert len(data['DEVS']) > 0

            # Check device structure
            device = data['DEVS'][0]
            assert 'GPU' in device
            assert 'Enabled' in device
            assert 'Temperature' in device
        finally:
            server.stop()

    def test_threads_command(self):
        """Test the threads API command."""
        server = MockAPIServer(port=14073)
        server.start(background=True)
        time.sleep(0.1)

        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect(('127.0.0.1', 14073))
            sock.sendall(b'threads')
            response = sock.recv(4096).decode('utf-8')
            sock.close()

            data = json.loads(response)
            assert 'STATUS' in data
            assert 'THREADS' in data
            assert isinstance(data['THREADS'], list)
        finally:
            server.stop()

    def test_pools_command(self):
        """Test the pools API command."""
        server = MockAPIServer(port=14074)
        server.start(background=True)
        time.sleep(0.1)

        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect(('127.0.0.1', 14074))
            sock.sendall(b'pools')
            response = sock.recv(4096).decode('utf-8')
            sock.close()

            data = json.loads(response)
            assert 'STATUS' in data
            assert 'POOLS' in data
            assert isinstance(data['POOLS'], list)
            assert len(data['POOLS']) > 0

            pool = data['POOLS'][0]
            assert 'URL' in pool
            assert 'Status' in pool
        finally:
            server.stop()

    def test_unknown_command(self):
        """Test that unknown commands return an error."""
        server = MockAPIServer(port=14075)
        server.start(background=True)
        time.sleep(0.1)

        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect(('127.0.0.1', 14075))
            sock.sendall(b'unknowncommand')
            response = sock.recv(4096).decode('utf-8')
            sock.close()

            data = json.loads(response)
            assert 'STATUS' in data
            assert data['STATUS']['SUCCESS'] is False
        finally:
            server.stop()

    def test_multiple_commands(self):
        """Test sending multiple commands at once (pipe-separated)."""
        server = MockAPIServer(port=14076)
        server.start(background=True)
        time.sleep(0.1)

        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect(('127.0.0.1', 14076))
            sock.sendall(b'version|summary')
            response = sock.recv(4096).decode('utf-8')
            sock.close()

            data = json.loads(response)
            # Response should be a list when multiple commands are sent
            assert isinstance(data, list)
            assert len(data) == 2
        finally:
            server.stop()

    def test_miner_state_update(self):
        """Test that miner state can be updated."""
        server = MockAPIServer(port=14077)

        # Update state before starting
        server.update_miner_state(
            hashrate_mhs=100.0,
            accepted_shares=200,
            rejected_shares=5
        )

        state = server.get_miner_state()
        assert state['hashrate_mhs'] == 100.0
        assert state['accepted_shares'] == 200
        assert state['rejected_shares'] == 5

    def test_custom_response_handler(self):
        """Test that custom response handlers can be added."""
        server = MockAPIServer(port=14078)

        def custom_handler(cmd):
            return {
                'STATUS': {'SUCCESS': True, 'msg': 'Custom response'},
                'CUSTOM': {'data': 'test'}
            }

        server.set_response_handler('customcmd', custom_handler)

        # Verify handler was registered
        assert 'customcmd' in server._custom_handlers


class TestAPIResponseValidation:
    """Test API response format and content validation."""

    def test_version_response_format(self):
        """Test that version response has correct format."""
        server = MockAPIServer(port=14079)
        response = server._get_version_response('version')

        assert 'STATUS' in response
        assert 'VERSION' in response
        assert 'id' in response

        status = response['STATUS']
        assert 'SUCCESS' in status
        assert 'msg' in status
        assert 'code' in status

        version = response['VERSION'][0]
        assert 'CGMINER' in version
        assert 'API' in version

    def test_summary_response_format(self):
        """Test that summary response has correct format."""
        server = MockAPIServer(port=14080)
        response = server._get_summary_response('summary')

        assert 'STATUS' in response
        assert 'SUMMARY' in response

        summary = response['SUMMARY'][0]
        required_fields = [
            'Elapsed', 'MHS av', 'Found Blocks', 'Getworks',
            'Accepted', 'Rejected', 'Stale', 'Hardware Errors',
            'Utility', 'Discarded', 'GPU Rejected', 'Network Blocks'
        ]
        for field in required_fields:
            assert field in summary, f"Missing field: {field}"

    def test_devs_response_format(self):
        """Test that devs response has correct format."""
        server = MockAPIServer(port=14081)
        response = server._get_devs_response('devs')

        assert 'STATUS' in response
        assert 'DEVS' in response
        assert isinstance(response['DEVS'], list)

        if len(response['DEVS']) > 0:
            device = response['DEVS'][0]
            required_fields = [
                'GPU', 'Enabled', 'Status', 'Temperature',
                'Fan Speed', 'GPU Clock', 'Memory Clock',
                'GPU Voltage', 'MHS av', 'Accepted', 'Rejected'
            ]
            for field in required_fields:
                assert field in device, f"Missing field: {field}"

    def test_devsdetail_response_format(self):
        """Test that devsdetail response has correct format."""
        server = MockAPIServer(port=14082)
        response = server._get_devsdetail_response('devsdetail')

        assert 'STATUS' in response
        assert 'DEVSDETAIL' in response

        if len(response['DEVSDETAIL']) > 0:
            device = response['DEVSDETAIL'][0]
            required_fields = [
                'GPU', 'Name', 'Intensity', 'Device ID',
                'GPU Temp', 'GPU Clock', 'Memory Clock'
            ]
            for field in required_fields:
                assert field in device, f"Missing field: {field}"


class TestAPIMultiGPU:
    """Test API behavior with multiple GPUs."""

    def test_multi_gpu_devs_response(self):
        """Test devs response with multiple GPUs."""
        server = MockAPIServer(port=14083)
        server.update_miner_state(gpu_count=4)
        response = server._get_devs_response('devs')

        assert len(response['DEVS']) == 4
        for i, device in enumerate(response['DEVS']):
            assert device['GPU'] == i

    def test_multi_gpu_threads_response(self):
        """Test threads response with multiple GPUs."""
        server = MockAPIServer(port=14084)
        server.update_miner_state(gpu_count=3)
        response = server._get_threads_response('threads')

        assert len(response['THREADS']) == 3


class TestAPIErrorHandling:
    """Test API error handling."""

    def test_connection_refused(self):
        """Test behavior when connecting to a non-running server."""
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(1)
        with pytest.raises(ConnectionRefusedError):
            sock.connect(('127.0.0.1', 29999))  # Unlikely port
        sock.close()

    def test_empty_command(self):
        """Test handling of empty command."""
        server = MockAPIServer(port=14085)
        server.start(background=True)
        time.sleep(0.1)

        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect(('127.0.0.1', 14085))
            sock.sendall(b'')
            # Server should not crash, but may not respond
            sock.settimeout(1)
            try:
                response = sock.recv(4096).decode('utf-8')
                # If we get a response, it should be empty or an error
            except socket.timeout:
                pass  # Timeout is acceptable
            finally:
                sock.close()
        finally:
            server.stop()
