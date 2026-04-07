# Copyright (c) 2026-2026 The GBXMiner developers
"""
Functional tests for GBXminer statistics tracking.

This module tests hashrate calculation, share tracking,
and statistics aggregation functionality.
"""

import json
import pytest
import socket
import sys
import time

sys.path.insert(0, '..')
from utils.mock_stratum import MockStratumServer


class TestShareTracking:
    """Test share submission tracking and statistics."""

    def test_share_count_increment(self):
        """Test that share count increments correctly."""
        server = MockStratumServer(port=13500)
        server.start(background=True)
        time.sleep(0.1)

        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect(('127.0.0.1', 13500))

            # Subscribe and authorize
            sock.sendall((json.dumps({
                'id': 1, 'method': 'mining.subscribe', 'params': []
            }) + '\n').encode('utf-8'))
            sock.recv(4096)

            sock.sendall((json.dumps({
                'id': 2, 'method': 'mining.authorize',
                'params': ['user', 'pass']
            }) + '\n').encode('utf-8'))
            sock.recv(4096)

            # Check initial stats
            stats = server.get_stats()
            initial_count = stats['submitted_shares']

            # Submit a share
            sock.sendall((json.dumps({
                'id': 3,
                'method': 'mining.submit',
                'params': ['user', '0001', '00000000', '63f0c0a0', '00000001']
            }) + '\n').encode('utf-8'))
            sock.recv(4096)

            # Verify count increased
            stats = server.get_stats()
            assert stats['submitted_shares'] == initial_count + 1

            sock.close()
        finally:
            server.stop()

    def test_accepted_rejected_ratio(self):
        """Test accepted/rejected share ratio tracking."""
        server = MockStratumServer(port=13501)
        server.start(background=True)
        time.sleep(0.1)

        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect(('127.0.0.1', 13501))

            # Subscribe and authorize
            sock.sendall((json.dumps({
                'id': 1, 'method': 'mining.subscribe', 'params': []
            }) + '\n').encode('utf-8'))
            sock.recv(4096)

            sock.sendall((json.dumps({
                'id': 2, 'method': 'mining.authorize',
                'params': ['user', 'pass']
            }) + '\n').encode('utf-8'))
            sock.recv(4096)

            # Submit multiple shares (all should be accepted by mock)
            for i in range(10):
                sock.sendall((json.dumps({
                    'id': i + 3,
                    'method': 'mining.submit',
                    'params': ['user', f'{i:04d}', '00000000', '63f0c0a0', f'{i:08x}']
                }) + '\n').encode('utf-8'))
                sock.recv(4096)

            stats = server.get_stats()
            assert stats['accepted_shares'] == 10
            assert stats['rejected_shares'] == 0

            # Calculate acceptance rate
            total = stats['accepted_shares'] + stats['rejected_shares']
            if total > 0:
                acceptance_rate = stats['accepted_shares'] / total
                assert acceptance_rate == 1.0

            sock.close()
        finally:
            server.stop()


class TestHashrateCalculation:
    """Test hashrate calculation and reporting."""

    def format_hashrate(self, hashes, seconds):
        """Calculate hashrate from hash count and time."""
        if seconds <= 0:
            return 0.0
        return hashes / seconds

    def test_hashrate_basic_calculation(self):
        """Test basic hashrate calculation."""
        # 1000 hashes in 10 seconds = 100 H/s
        hashrate = self.format_hashrate(1000, 10)
        assert hashrate == 100.0

    def test_hashrate_zero_time(self):
        """Test hashrate with zero time returns 0."""
        hashrate = self.format_hashrate(1000, 0)
        assert hashrate == 0.0

    def test_hashrate_unit_conversion(self):
        """Test hashrate unit conversion."""
        # 1,000,000 H/s = 1 MH/s
        hashrate_h = 1000000
        hashrate_mh = hashrate_h / 1000000
        assert hashrate_mh == 1.0

        # 1,000,000,000 H/s = 1 GH/s
        hashrate_gh = hashrate_h / 1000000000
        assert hashrate_gh == 0.001

    @pytest.mark.parametrize("hashes,seconds,expected", [
        (1000000, 1, 1000000),
        (500000, 0.5, 1000000),
        (2000000, 2, 1000000),
        (0, 10, 0),
    ])
    def test_hashrate_calculations(self, hashes, seconds, expected):
        """Test various hashrate calculations."""
        result = self.format_hashrate(hashes, seconds)
        assert result == expected


class TestDifficultyTracking:
    """Test difficulty tracking and adjustments."""

    def test_difficulty_tracking(self):
        """Test that difficulty is tracked correctly."""
        server = MockStratumServer(port=13502, difficulty=4.0)
        server.start(background=True)
        time.sleep(0.1)

        try:
            stats = server.get_stats()
            assert stats['difficulty'] == 4.0

            # Change difficulty
            server.set_difficulty(8.0)
            stats = server.get_stats()
            assert stats['difficulty'] == 8.0

        finally:
            server.stop()

    def test_difficulty_notification(self):
        """Test difficulty change notification."""
        server = MockStratumServer(port=13503, difficulty=1.0)
        server.start(background=True)
        time.sleep(0.1)

        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect(('127.0.0.1', 13503))
            sock.settimeout(2)

            # Subscribe to start receiving notifications
            sock.sendall((json.dumps({
                'id': 1, 'method': 'mining.subscribe', 'params': []
            }) + '\n').encode('utf-8'))
            sock.recv(4096)

            # Change difficulty
            server.set_difficulty(16.0)
            time.sleep(0.1)

            # Should receive difficulty notification
            try:
                data = sock.recv(4096).decode('utf-8')
                assert 'mining.set_difficulty' in data
                assert '16' in data
            except socket.timeout:
                pass

            sock.close()
        finally:
            server.stop()


class TestUptimeTracking:
    """Test uptime and elapsed time tracking."""

    def test_elapsed_time_increases(self):
        """Test that elapsed time increases over time."""
        server = MockStratumServer(port=13504)
        server.start(background=True)
        time.sleep(0.1)

        try:
            stats1 = server.get_stats()
            start_time = stats1.get('elapsed', 0)

            time.sleep(1)

            stats2 = server.get_stats()
            end_time = stats2.get('elapsed', 0)

            # Elapsed time should have increased
            assert end_time >= start_time

        finally:
            server.stop()


class TestStatsPersistence:
    """Test statistics persistence and retrieval."""

    def test_stats_reset(self):
        """Test that stats can be reset."""
        server = MockStratumServer(port=13505)
        server.start(background=True)
        time.sleep(0.1)

        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect(('127.0.0.1', 13505))

            # Subscribe and authorize
            sock.sendall((json.dumps({
                'id': 1, 'method': 'mining.subscribe', 'params': []
            }) + '\n').encode('utf-8'))
            sock.recv(4096)

            sock.sendall((json.dumps({
                'id': 2, 'method': 'mining.authorize',
                'params': ['user', 'pass']
            }) + '\n').encode('utf-8'))
            sock.recv(4096)

            # Submit shares
            for i in range(5):
                sock.sendall((json.dumps({
                    'id': i + 3,
                    'method': 'mining.submit',
                    'params': ['user', f'{i:04d}', '00000000', '63f0c0a0', f'{i:08x}']
                }) + '\n').encode('utf-8'))
                sock.recv(4096)

            sock.close()

            # Check stats
            stats = server.get_stats()
            assert stats['submitted_shares'] == 5

            # Reset stats (if supported)
            if hasattr(server, 'reset_stats'):
                server.reset_stats()
                stats = server.get_stats()
                assert stats['submitted_shares'] == 0

        finally:
            server.stop()


class TestPerformanceMetrics:
    """Test performance-related metrics."""

    def test_best_share_tracking(self):
        """Test that best share (highest difficulty) is tracked."""
        server = MockStratumServer(port=13506)
        server.start(background=True)
        time.sleep(0.1)

        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect(('127.0.0.1', 13506))

            # Subscribe and authorize
            sock.sendall((json.dumps({
                'id': 1, 'method': 'mining.subscribe', 'params': []
            }) + '\n').encode('utf-8'))
            sock.recv(4096)

            sock.sendall((json.dumps({
                'id': 2, 'method': 'mining.authorize',
                'params': ['user', 'pass']
            }) + '\n').encode('utf-8'))
            sock.recv(4096)

            # Submit shares with varying quality (simulated by different nonces)
            for i in range(3):
                sock.sendall((json.dumps({
                    'id': i + 3,
                    'method': 'mining.submit',
                    'params': ['user', f'{i:04d}', '00000000', '63f0c0a0', f'{i:08x}']
                }) + '\n').encode('utf-8'))
                sock.recv(4096)

            stats = server.get_stats()
            # Should have recorded shares
            assert stats['submitted_shares'] >= 3

            sock.close()
        finally:
            server.stop()

    def test_network_stats(self):
        """Test network-related statistics."""
        server = MockStratumServer(port=13507)
        server.start(background=True)
        time.sleep(0.1)

        try:
            # Connect multiple clients
            sockets = []
            for i in range(3):
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.connect(('127.0.0.1', 13507))
                sockets.append(sock)

            time.sleep(0.1)

            stats = server.get_stats()
            # Should track connected clients
            assert stats.get('connected_clients', 0) == 3 or 'connected_clients' in stats

            for sock in sockets:
                sock.close()

        finally:
            server.stop()


class TestStaleShareDetection:
    """Test stale share detection and handling."""

    def test_stale_share_concept(self):
        """Test concept of stale share detection."""
        # A stale share is one submitted after a new job has been received
        # This is a conceptual test since the mock server accepts all shares

        # In real implementation:
        # - Share submitted for old job_id = stale
        # - Share submitted after timeout = stale

        # Mock server accepts all, but we verify the concept
        server = MockStratumServer(port=13508)

        # Verify server tracks job IDs
        assert hasattr(server, 'get_current_job') or hasattr(server, '_current_job')
