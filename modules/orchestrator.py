"""
Autonomous attack orchestrator - coordinates pentesting operations.
"""

import threading
import time
import random
from typing import Dict, List, Optional
from datetime import datetime
from pathlib import Path

from .attacks.network_recon import NetworkRecon
from .attacks.credential_harvester import CredentialHarvester
from .attacks.vulnerability_scanner import VulnerabilityScanner


class AttackOrchestrator:
    """
    Manages autonomous attack chain execution.

    State machine:
    IDLE -> RECON -> SCANNING -> HARVESTING -> ANALYSIS -> IDLE
    """

    STATE_IDLE = 'idle'
    STATE_RECON = 'reconnaissance'
    STATE_SCANNING = 'scanning'
    STATE_HARVESTING = 'harvesting'
    STATE_ANALYSIS = 'analysis'

    def __init__(self, database, evolution_engine, config):
        self.db = database
        self.evolution = evolution_engine
        self.config = config

        # Initialize attack modules
        self.network_recon = NetworkRecon(database, evolution_engine, config)
        self.credential_harvester = CredentialHarvester(database, evolution_engine, config)
        self.vulnerability_scanner = VulnerabilityScanner(database, evolution_engine, config)

        # State management
        self.current_state = self.STATE_IDLE
        self.running = False
        self.paused = False
        self.attack_thread = None

        # Attack session tracking
        self.session_start = None
        self.last_action_time = None
        self.actions_performed = 0

        # Real-time progress tracking
        self.current_progress = {
            'stage': None,
            'current_host': None,
            'hosts_completed': 0,
            'hosts_total': 0,
            'services_found': 0,
            'percent_complete': 0,
            'eta_seconds': 0,
            'status_message': 'Idle'
        }

        # Error tracking and recovery
        self.error_count = 0
        self.max_errors_before_pause = 5
        self.last_error = None
        self.recovery_attempts = 0

    def start(self):
        """Start autonomous attack operations."""
        if self.running:
            print("Orchestrator already running")
            return False

        self.running = True
        self.paused = False
        self.session_start = datetime.now()
        self.current_state = self.STATE_IDLE

        # Start attack thread
        self.attack_thread = threading.Thread(
            target=self._attack_loop,
            daemon=True
        )
        self.attack_thread.start()

        self.db.log_activity(
            event_type='orchestrator_started',
            details=f"Started autonomous operations with profile: {self.config.current_profile_name}"
        )

        print(f"[*] Orchestrator started with '{self.config.current_profile_name}' profile")
        return True

    def stop(self):
        """Stop autonomous operations."""
        self.running = False

        # Stop any running attacks
        if self.credential_harvester.is_running():
            self.credential_harvester.stop_responder()

        self.db.log_activity(
            event_type='orchestrator_stopped',
            details=f"Stopped after {self.actions_performed} actions"
        )

        print("[*] Orchestrator stopped")

    def pause(self):
        """Pause operations."""
        self.paused = True
        print("[*] Orchestrator paused")

    def resume(self):
        """Resume operations."""
        self.paused = False
        print("[*] Orchestrator resumed")

    def _attack_loop(self):
        """Main autonomous attack loop with error recovery."""
        while self.running:
            try:
                if self.paused:
                    time.sleep(1)
                    continue

                # Execute next action based on current state
                if self.current_state == self.STATE_IDLE:
                    self._state_idle()
                elif self.current_state == self.STATE_RECON:
                    self._state_reconnaissance()
                elif self.current_state == self.STATE_SCANNING:
                    self._state_scanning()
                elif self.current_state == self.STATE_HARVESTING:
                    self._state_harvesting()
                elif self.current_state == self.STATE_ANALYSIS:
                    self._state_analysis()

                # Reset error count on successful execution
                self.error_count = 0
                self.recovery_attempts = 0

                # Apply delay between state transitions
                self._apply_cycle_delay()

            except Exception as e:
                self._handle_error(e)

    def _state_idle(self):
        """Idle state - prepare for next attack cycle."""
        print(f"[{self.current_state.upper()}] Preparing next attack cycle...")

        # Check what modules are enabled
        profile = self.config.get_current_profile()

        if 'network_recon' in profile.enabled_modules:
            self.current_state = self.STATE_RECON
        elif 'credential_harvesting' in profile.enabled_modules:
            self.current_state = self.STATE_HARVESTING
        else:
            # Nothing enabled, wait
            time.sleep(30)

    def _state_reconnaissance(self):
        """Reconnaissance state - discover hosts and services with smart scanning."""
        print(f"[{self.current_state.upper()}] Starting network reconnaissance...")

        profile = self.config.get_current_profile()
        attack_config = self.config.attack_config
        import time
        stage_start = time.time()

        # STAGE 1: Passive ARP scan (always safe, instant)
        self.current_progress.update({
            'stage': 'ARP Scan',
            'status_message': 'Scanning local ARP cache...',
            'percent_complete': 10
        })
        print("[RECON] Stage 1: Passive ARP scan...")
        arp_hosts = self.network_recon.passive_arp_scan()
        print(f"[RECON] ARP found {len(arp_hosts)} hosts")

        # STAGE 2: Fast ping sweep (if nmap enabled and not passive mode)
        if attack_config.nmap_enabled and profile.name != 'passive':
            self.current_progress.update({
                'stage': 'Ping Sweep',
                'status_message': f'Discovering hosts on {attack_config.target_network}...',
                'percent_complete': 30
            })
            print("[RECON] Stage 2: Fast ping sweep to discover all hosts...")
            ping_discovered = self.network_recon.active_nmap_scan(args="-sn")  # Ping scan only
            print(f"[RECON] Ping sweep discovered {len(ping_discovered)} hosts")

        # STAGE 3: Smart incremental service scanning with parallelization
        self.current_progress.update({
            'stage': 'Smart Scan',
            'status_message': 'Running intelligent parallel port scan...',
            'percent_complete': 40
        })

        print("[RECON] Stage 3: Smart incremental scan with parallelization...")

        # Use smart scan (incremental by default, full scan if no hosts known)
        all_hosts = self.db.get_hosts()
        use_full_scan = len(all_hosts) == 0  # Full scan if first run

        scan_stats = self.network_recon.smart_incremental_scan(full_scan=use_full_scan)

        # Update progress
        total_hosts = len(self.db.get_hosts())
        self.current_progress.update({
            'stage': 'Complete',
            'hosts_total': total_hosts,
            'hosts_completed': scan_stats['hosts_scanned'],
            'services_found': scan_stats['services_found'],
            'percent_complete': 100,
            'status_message': f'Smart scan complete: {scan_stats["hosts_scanned"]} hosts, '
                             f'{scan_stats["services_found"]} services in {scan_stats["scan_time"]:.1f}s'
        })

        print(f"[RECON] Complete: {total_hosts} total hosts in database, "
              f"{scan_stats['services_found']} new services discovered")

        self.actions_performed += 1
        self.last_action_time = datetime.now()

        # Transition to next state
        if 'vulnerability_scanning' in profile.enabled_modules:
            self.current_state = self.STATE_SCANNING
        elif 'credential_harvesting' in profile.enabled_modules:
            self.current_state = self.STATE_HARVESTING
        else:
            self.current_state = self.STATE_ANALYSIS

    def _state_scanning(self):
        """Scanning state - enumerate vulnerabilities."""
        print(f"[{self.current_state.upper()}] Scanning for vulnerabilities...")

        # Scan all discovered services
        results = self.vulnerability_scanner.scan_all_services()

        print(f"[SCAN] Scanned {results['total_services']} services")
        print(f"[SCAN] Found {results['vulnerabilities_found']} vulnerabilities")
        if results['by_severity'].get('CRITICAL', 0) > 0:
            print(f"[!] WARNING: {results['by_severity']['CRITICAL']} CRITICAL vulnerabilities found!")

        self.actions_performed += 1
        self.last_action_time = datetime.now()

        # Transition to harvesting or analysis
        profile = self.config.get_current_profile()
        if 'credential_harvesting' in profile.enabled_modules:
            self.current_state = self.STATE_HARVESTING
        else:
            self.current_state = self.STATE_ANALYSIS

    def _state_harvesting(self):
        """Harvesting state - capture credentials."""
        print(f"[{self.current_state.upper()}] Starting credential harvesting...")

        attack_config = self.config.attack_config

        # Start Responder if configured and not already running
        if attack_config.responder_enabled and not self.credential_harvester.is_running():
            success = self.credential_harvester.start_responder()
            if success:
                print(f"[HARVEST] Responder started on {attack_config.responder_interface}")
            else:
                print("[HARVEST] Failed to start Responder")

        # Parse any existing Responder logs
        imported = self.credential_harvester.parse_responder_logs()
        if imported > 0:
            print(f"[HARVEST] Imported {imported} credentials from Responder logs")

        # Get current credential summary
        cred_summary = self.credential_harvester.get_captured_credentials()
        print(f"[HARVEST] Total credentials captured: {cred_summary['total']}")
        print(f"[HARVEST] Unique users: {cred_summary['unique_users']}")

        self.actions_performed += 1
        self.last_action_time = datetime.now()

        # Transition to analysis
        self.current_state = self.STATE_ANALYSIS

    def _state_analysis(self):
        """Analysis state - review results and plan next cycle."""
        print(f"[{self.current_state.upper()}] Analyzing results...")

        # Get statistics
        stats = self.db.get_stats()
        evolution_status = self.evolution.get_full_status()

        print(f"[ANALYSIS] Hosts: {stats['total_hosts']}")
        print(f"[ANALYSIS] Services: {stats['total_services']}")
        print(f"[ANALYSIS] Credentials: {stats['total_credentials']}")
        print(f"[ANALYSIS] Vulnerabilities: {stats['total_vulnerabilities']}")
        print(f"[ANALYSIS] Current Evolution: {evolution_status['current_stage']['name']} (Level {evolution_status['current_stage']['level']})")
        print(f"[ANALYSIS] Total XP: {evolution_status['stats']['total_xp']}")

        # Check for evolution
        if evolution_status['stats']['current_stage'] < len(self.config.STEALTH_PROFILES) - 1:
            progress = evolution_status['progress']
            if not progress['is_max_level']:
                print(f"[EVOLUTION] {progress['progress_percent']}% to {progress['next_stage_name']}")

        self.actions_performed += 1
        self.last_action_time = datetime.now()

        # Return to idle for next cycle
        self.current_state = self.STATE_IDLE

    def _apply_cycle_delay(self):
        """Apply delay between attack cycles based on profile."""
        profile = self.config.get_current_profile()

        # Use longer delays between full cycles
        min_delay = profile.scan_delay_min * 5  # Multiply by 5 for cycle delays
        max_delay = profile.scan_delay_max * 5

        if profile.randomize_timing:
            delay = random.uniform(min_delay, max_delay)
        else:
            delay = min_delay

        time.sleep(delay)

    def get_status(self) -> Dict:
        """Get current orchestrator status."""
        uptime = None
        if self.session_start:
            uptime = (datetime.now() - self.session_start).total_seconds()

        return {
            'running': self.running,
            'paused': self.paused,
            'current_state': self.current_state,
            'current_profile': self.config.current_profile_name,
            'session_start': self.session_start.isoformat() if self.session_start else None,
            'uptime_seconds': uptime,
            'actions_performed': self.actions_performed,
            'last_action': self.last_action_time.isoformat() if self.last_action_time else None,
            'responder_running': self.credential_harvester.is_running(),
            'error_status': self.get_error_status()
        }

    def force_state_transition(self, new_state: str) -> bool:
        """Manually force a state transition (for testing/debugging)."""
        valid_states = [
            self.STATE_IDLE,
            self.STATE_RECON,
            self.STATE_SCANNING,
            self.STATE_HARVESTING,
            self.STATE_ANALYSIS
        ]

        if new_state in valid_states:
            self.current_state = new_state
            print(f"[*] Forced state transition to: {new_state}")
            return True

        return False

    def run_single_recon(self) -> Dict:
        """Run a single reconnaissance cycle manually."""
        print("[*] Running manual reconnaissance cycle...")
        self._state_reconnaissance()
        return {
            'hosts': self.db.get_stats()['total_hosts'],
            'services': self.db.get_stats()['total_services']
        }

    def run_single_scan(self) -> Dict:
        """Run a single vulnerability scan manually."""
        print("[*] Running manual vulnerability scan...")
        results = self.vulnerability_scanner.scan_all_services()
        return results

    def _handle_error(self, error: Exception):
        """Handle errors in the attack loop with recovery logic."""
        import traceback

        self.error_count += 1
        self.last_error = {
            'error': str(error),
            'traceback': traceback.format_exc(),
            'timestamp': datetime.now().isoformat(),
            'state': self.current_state
        }

        # Log error to database
        self.db.log_activity(
            event_type='orchestrator_error',
            details=f"Error in {self.current_state} state: {str(error)}"
        )

        print(f"[ERROR] Orchestrator error in {self.current_state} state: {error}")
        print(f"[ERROR] Error count: {self.error_count}/{self.max_errors_before_pause}")

        # Auto-pause if too many errors
        if self.error_count >= self.max_errors_before_pause:
            print(f"[ERROR] Too many consecutive errors ({self.error_count}). Pausing orchestrator.")
            print(f"[ERROR] Last error: {error}")
            self.pause()

            # Log critical error
            self.db.log_activity(
                event_type='orchestrator_auto_paused',
                details=f"Auto-paused after {self.error_count} consecutive errors"
            )

            return

        # Attempt recovery based on error type
        recovery_delay = self._attempt_recovery(error)

        # Sleep before retrying
        time.sleep(recovery_delay)

    def _attempt_recovery(self, error: Exception) -> int:
        """Attempt to recover from error. Returns delay in seconds."""
        self.recovery_attempts += 1

        error_str = str(error).lower()

        # Network/timeout errors - retry with exponential backoff
        if 'timeout' in error_str or 'connection' in error_str or 'network' in error_str:
            delay = min(30, 5 * (2 ** self.recovery_attempts))
            print(f"[RECOVERY] Network error detected. Retrying in {delay}s...")
            return delay

        # Permission errors - likely need root/sudo
        if 'permission' in error_str or 'access denied' in error_str:
            print(f"[RECOVERY] Permission error. Check if running with sufficient privileges.")
            print(f"[RECOVERY] Some features may require root/sudo access.")
            # Transition to next state to skip problematic operation
            self.current_state = self.STATE_ANALYSIS
            return 10

        # Database errors - try to recover connection
        if 'database' in error_str or 'sqlite' in error_str:
            print(f"[RECOVERY] Database error. Attempting to reconnect...")
            try:
                # Database should auto-reconnect, just add delay
                return 5
            except:
                return 10

        # Tool not found errors (nmap, responder, etc.)
        if 'not found' in error_str or 'no such file' in error_str:
            print(f"[RECOVERY] Tool not found. Skipping to next state.")
            # Skip to next state
            self.current_state = self.STATE_ANALYSIS
            return 5

        # Generic error - transition to analysis state and retry
        print(f"[RECOVERY] Unknown error type. Transitioning to analysis state.")
        self.current_state = self.STATE_ANALYSIS
        return 10

    def clear_errors(self):
        """Clear error count and resume normal operation."""
        self.error_count = 0
        self.recovery_attempts = 0
        self.last_error = None
        print("[*] Error count cleared")

    def get_error_status(self) -> Dict:
        """Get current error status."""
        return {
            'error_count': self.error_count,
            'max_errors': self.max_errors_before_pause,
            'last_error': self.last_error,
            'recovery_attempts': self.recovery_attempts
        }
