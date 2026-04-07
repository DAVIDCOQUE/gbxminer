# Copyright (c) 2009-2014 The Bitcoin Core developers
"""
Integration tests for GBXminer.

This module provides end-to-end integration tests that verify
component interactions using mock servers and simulated mining workflows.
"""

import json
import pytest
import socket
import sys
import time

sys.path.insert(0, '..')
from utils.mock_api import MockAPIServer
from utils.mock_stratum import MockStratumServer, MockStratumJob


class TestMiningWorkflow:
    """Test complete mining workflow from connection to share submission."""

    def test_full_mining_workflow(self):
        """Test a complete mining workflow: connect, subscribe, authorize, receive job, submit share."""
        # Start mock stratum server
        server = MockStratumServer(port=13400)
        server.start(background=True)
        time.sleep(0.1)

        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect(('127.0.0.1', 13400))
            sock.settimeout(5)

            # Step 1: Subscribe
            subscribe_request = {
                'id': 1,
                'method': 'mining.subscribe',
                'params': []
            }
            sock.sendall((json.dumps(subscribe_request) + '\n').encode('utf-8'))
            response = sock.recv(4096).decode('utf-8').strip()
            subscribe_response = json.loads(response)
            assert subscribe_response['id'] == 1
            assert subscribe_response['error'] is None

            # Step 2: Authorize
            authorize_request = {
                'id': 2,
                'method': 'mining.authorize',
                'params': ['testuser', 'password']
            }
            sock.sendall((json.dumps(authorize_request) + '\n').encode('utf-8'))
            response = sock.recv(4096).decode('utf-8').strip()
            authorize_response = json.loads(response)
            assert authorize_response['id'] == 2
            assert authorize_response['result'] is True

            # Step 3: Submit a share
            submit_request = {
                'id': 3,
                'method': 'mining.submit',
                'params': ['testuser', '0001', '00000000', '63f0c0a0', '00000001']
            }
            sock.sendall((json.dumps(submit_request) + '\n').encode('utf-8'))
            response = sock.recv(4096).decode('utf-8').strip()
            submit_response = json.loads(response)
            assert submit_response['id'] == 3
            assert submit_response['result'] is True

            # Verify server stats
            stats = server.get_stats()
            assert stats['submitted_shares'] == 1
            assert stats['accepted_shares'] == 1

            sock.close()
        finally:
            server.stop()

    def test_mining_workflow_with_multiple_shares(self):
        """Test submitting multiple shares in sequence."""
        server = MockStratumServer(port=13401)
        server.start(background=True)
        time.sleep(0.1)

        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect(('127.0.0.1', 13401))
            sock.settimeout(5)

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

            # Submit multiple shares
            for i in range(5):
                sock.sendall((json.dumps({
                    'id': i + 3,
                    'method': 'mining.submit',
                    'params': ['user', f'{i:04d}', '00000000', '63f0c0a0', f'{i:08x}']
                }) + '\n').encode('utf-8'))
                response = sock.recv(4096).decode('utf-8').strip()
                resp = json.loads(response)
                assert resp['result'] is True

            stats = server.get_stats()
            assert stats['submitted_shares'] == 5
            assert stats['accepted_shares'] == 5

            sock.close()
        finally:
            server.stop()


class TestAPIAndStratumIntegration:
    """Test API and Stratum server working together."""

    def test_api_reports_stratum_activity(self):
        """Test that API reflects stratum mining activity."""
        # Start both servers
        api_server = MockAPIServer(port=14100)
        stratum_server = MockStratumServer(port=13402)

        api_server.start(background=True)
        stratum_server.start(background=True)
        time.sleep(0.1)

        try:
            # Submit some shares to stratum server
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect(('127.0.0.1', 13402))

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
            for i in range(3):
                sock.sendall((json.dumps({
                    'id': i + 3,
                    'method': 'mining.submit',
                    'params': ['user', f'{i:04d}', '00000000', '63f0c0a0', f'{i:08x}']
                }) + '\n').encode('utf-8'))
                sock.recv(4096)

            sock.close()

            # Check stratum stats
            stratum_stats = stratum_server.get_stats()
            assert stratum_stats['submitted_shares'] == 3

            # Query API for summary
            api_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            api_sock.connect(('127.0.0.1', 14100))
            api_sock.sendall(b'summary')
            response = api_sock.recv(4096).decode('utf-8')
            api_data = json.loads(response)
            api_sock.close()

            assert 'SUMMARY' in api_data
            # API should show some accepted shares
            summary = api_data['SUMMARY'][0]
            assert 'Accepted' in summary

        finally:
            api_server.stop()
            stratum_server.stop()


class TestConnectionResilience:
    """Test connection resilience and reconnection scenarios."""

    def test_stratum_server_restart(self):
        """Test client behavior when stratum server restarts."""
        # Start server
        server = MockStratumServer(port=13403)
        server.start(background=True)
        time.sleep(0.1)

        try:
            # Connect and subscribe
            sock1 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock1.connect(('127.0.0.1', 13403))
            sock1.sendall((json.dumps({
                'id': 1, 'method': 'mining.subscribe', 'params': []
            }) + '\n').encode('utf-8'))
            sock1.recv(4096)
            sock1.close()

            # Restart server
            server.stop()
            time.sleep(0.1)
            server = MockStratumServer(port=13403)
            server.start(background=True)
            time.sleep(0.1)

            # Reconnect
            sock2 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock2.connect(('127.0.0.1', 13403))
            sock2.sendall((json.dumps({
                'id': 2, 'method': 'mining.subscribe', 'params': []
            }) + '\n').encode('utf-8'))
            response = sock2.recv(4096).decode('utf-8').strip()
            resp = json.loads(response)
            assert resp['error'] is None
            sock2.close()

        finally:
            server.stop()

    def test_multiple_clients_simultaneous(self):
        """Test multiple clients connecting simultaneously."""
        server = MockStratumServer(port=13404)
        server.start(background=True)
        time.sleep(0.1)

        try:
            sockets = []
            for i in range(3):
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.connect(('127.0.0.1', 13404))
                sock.settimeout(2)
                sockets.append(sock)

            assert server.get_connected_client_count() == 3

            # All clients subscribe
            for i, sock in enumerate(sockets):
                sock.sendall((json.dumps({
                    'id': i + 1, 'method': 'mining.subscribe', 'params': []
                }) + '\n').encode('utf-8'))

            # All clients should get responses
            for sock in sockets:
                try:
                    response = sock.recv(4096).decode('utf-8').strip()
                    resp = json.loads(response)
                    assert resp['error'] is None
                except socket.timeout:
                    pass

            # Clean up
            for sock in sockets:
                sock.close()

        finally:
            server.stop()


class TestJobManagement:
    """Test mining job management."""

    def test_job_creation_and_notification(self):
        """Test creating and notifying about new jobs."""
        server = MockStratumServer(port=13405)
        server.start(background=True)
        time.sleep(0.1)

        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect(('127.0.0.1', 13405))
            sock.settimeout(2)

            # Subscribe
            sock.sendall((json.dumps({
                'id': 1, 'method': 'mining.subscribe', 'params': []
            }) + '\n').encode('utf-8'))
            sock.recv(4096)

            # Create a new job
            new_job = MockStratumJob(
                job_id='test_job_001',
                prev_hash='a' * 64,
                ntime='63f0c0a0'
            )
            server.set_current_job(new_job)

            # Broadcast notification
            notify_msg = {
                'id': None,
                'method': 'mining.notify',
                'params': new_job.to_notify_params()
            }
            server.broadcast_message(notify_msg)

            # Client should receive notification
            try:
                data = sock.recv(4096).decode('utf-8')
                assert 'mining.notify' in data
                assert 'test_job_001' in data
            except socket.timeout:
                pass

            sock.close()
        finally:
            server.stop()

    def test_job_clean_jobs_flag(self):
        """Test that clean_jobs flag is properly set in notifications."""
        job = MockStratumJob(job_id='clean_test', clean_jobs=True)
        params = job.to_notify_params()
        # Last parameter should be clean_jobs flag
        assert params[8] is True

        job_dirty = MockStratumJob(job_id='dirty_test', clean_jobs=False)
        params_dirty = job_dirty.to_notify_params()
        assert params_dirty[8] is False


class TestErrorRecovery:
    """Test error recovery scenarios."""

    def test_invalid_json_recovery(self):
        """Test server recovery after receiving invalid JSON."""
        server = MockStratumServer(port=13406)
        server.start(background=True)
        time.sleep(0.1)

        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect(('127.0.0.1', 13406))
            sock.settimeout(2)

            # Send invalid JSON
            sock.sendall(b'this is not valid json\n')
            try:
                response = sock.recv(4096).decode('utf-8').strip()
                resp = json.loads(response)
                assert resp['error'] is not None
                assert resp['error'][0] == 1  # Parse error
            except socket.timeout:
                pass

            # Server should still work - send valid request
            sock.sendall((json.dumps({
                'id': 2, 'method': 'mining.subscribe', 'params': []
            }) + '\n').encode('utf-8'))
            response = sock.recv(4096).decode('utf-8').strip()
            resp = json.loads(response)
            assert resp['error'] is None

            sock.close()
        finally:
            server.stop()

    def test_unknown_method_recovery(self):
        """Test server recovery after unknown method."""
        server = MockStratumServer(port=13407)
        server.start(background=True)
        time.sleep(0.1)

        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect(('127.0.0.1', 13407))
            sock.settimeout(2)

            # Send unknown method
            sock.sendall((json.dumps({
                'id': 1, 'method': 'unknown.method', 'params': []
            }) + '\n').encode('utf-8'))
            response = sock.recv(4096).decode('utf-8').strip()
            resp = json.loads(response)
            assert resp['error'] is not None

            # Server should still work
            sock.sendall((json.dumps({
                'id': 2, 'method': 'mining.subscribe', 'params': []
            }) + '\n').encode('utf-8'))
            response = sock.recv(4096).decode('utf-8').strip()
            resp = json.loads(response)
            assert resp['error'] is None

            sock.close()
        finally:
            server.stop()


class TestConcurrentOperations:
    """Test concurrent operations."""

    def test_api_queries_during_mining(self):
        """Test API queries work during active mining."""
        api_server = MockAPIServer(port=14101)
        stratum_server = MockStratumServer(port=13408)

        api_server.start(background=True)
        stratum_server.start(background=True)
        time.sleep(0.1)

        try:
            # Start mining
            mining_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            mining_sock.connect(('127.0.0.1', 13408))

            mining_sock.sendall((json.dumps({
                'id': 1, 'method': 'mining.subscribe', 'params': []
            }) + '\n').encode('utf-8'))
            mining_sock.recv(4096)

            mining_sock.sendall((json.dumps({
                'id': 2, 'method': 'mining.authorize',
                'params': ['user', 'pass']
            }) + '\n').encode('utf-8'))
            mining_sock.recv(4096)

            # Submit shares while querying API
            for i in range(3):
                # Submit share
                mining_sock.sendall((json.dumps({
                    'id': i + 3,
                    'method': 'mining.submit',
                    'params': ['user', f'{i:04d}', '00000000', '63f0c0a0', f'{i:08x}']
                }) + '\n').encode('utf-8'))
                mining_sock.recv(4096)

                # Query API
                api_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                api_sock.connect(('127.0.0.1', 14101))
                api_sock.sendall(b'summary')
                api_response = api_sock.recv(4096).decode('utf-8')
                api_data = json.loads(api_response)
                api_sock.close()

                assert 'SUMMARY' in api_data

            mining_sock.close()
        finally:
            api_server.stop()
            stratum_server.stop()
