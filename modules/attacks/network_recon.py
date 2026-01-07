"""
Network reconnaissance module - active and passive host/service discovery.
"""

import subprocess
import re
import ipaddress
import time
import random
import socket
from concurrent.futures import ThreadPoolExecutor, as_completed
from typing import List, Dict, Optional, Tuple
from pathlib import Path
from datetime import datetime, timedelta


class NetworkRecon:
    """Handles network reconnaissance and service enumeration."""

    def __init__(self, database, evolution_engine, config):
        self.db = database
        self.evolution = evolution_engine
        self.config = config

    def get_local_network(self) -> Optional[str]:
        """Detect the local network CIDR."""
        try:
            # Get default route interface and IP
            result = subprocess.run(
                ['ip', '-o', '-4', 'addr', 'show'],
                capture_output=True,
                text=True,
                timeout=5
            )

            for line in result.stdout.splitlines():
                if 'scope global' in line:
                    # Extract IP and subnet
                    match = re.search(r'inet\s+(\d+\.\d+\.\d+\.\d+/\d+)', line)
                    if match:
                        network = ipaddress.ip_network(match.group(1), strict=False)
                        return str(network)

        except Exception as e:
            print(f"Error detecting local network: {e}")

        return None

    def passive_arp_scan(self) -> List[Dict]:
        """
        Passive host discovery via ARP cache.
        Returns list of discovered hosts.

        Note: This only works for Layer 2 networks. VPN/tunneled networks
        like Tailscale won't show up in ARP cache.
        """
        discovered = []

        try:
            result = subprocess.run(
                ['ip', 'neigh', 'show'],
                capture_output=True,
                text=True,
                timeout=5
            )

            total_entries = 0
            for line in result.stdout.splitlines():
                if line.strip():
                    total_entries += 1

                # Example: 192.168.1.1 dev eth0 lladdr aa:bb:cc:dd:ee:ff REACHABLE
                match = re.match(
                    r'(\d+\.\d+\.\d+\.\d+)\s+dev\s+(\S+)\s+lladdr\s+([0-9a-f:]+)',
                    line,
                    re.IGNORECASE
                )

                if match:
                    ip, interface, mac = match.groups()

                    # Try to resolve hostname
                    hostname = self._resolve_hostname(ip)

                    # Add to database
                    host_id, is_new = self.db.add_host(ip=ip, mac=mac, hostname=hostname)

                    if is_new:
                        discovered.append({
                            'ip': ip,
                            'mac': mac,
                            'hostname': hostname,
                            'host_id': host_id
                        })

            print(f"[ARP] Checked ARP cache: {total_entries} entries, {len(discovered)} new hosts discovered")

            # Award XP only for NEW discoveries
            if discovered:
                result = self.evolution.add_xp('host_discovered', count=len(discovered))
                for host in discovered:
                    self.db.log_activity(
                        event_type='host_discovered',
                        target=host['ip'],
                        details=f"MAC: {host['mac']}, Hostname: {host.get('hostname', 'unknown')}",
                        xp_earned=result['xp_gained'] // len(discovered)
                    )

        except Exception as e:
            print(f"[ARP] Error in passive scan: {e}")

        return discovered

    def active_nmap_scan(self, target: str = None, args: str = None) -> List[Dict]:
        """
        Active nmap scan of network or specific host.
        Requires nmap to be installed.

        Args:
            target: Target IP/CIDR (default: auto-detect local network)
            args: Custom nmap arguments (default: from config)

        Returns:
            List of discovered hosts with services
        """
        if not self._check_nmap_installed():
            print("nmap not installed, skipping active scan")
            return []

        attack_config = self.config.attack_config
        profile = self.config.get_current_profile()

        if not target:
            if attack_config.target_network == "auto":
                target = self.get_local_network()
            else:
                target = attack_config.target_network

        if not target:
            print("No target network specified")
            return []

        if not args:
            args = attack_config.nmap_args

        # Build nmap command with rate limiting from profile
        nmap_cmd = ['nmap']

        # Add rate limiting
        if profile.port_scan_rate > 0:
            nmap_cmd.extend(['--max-rate', str(profile.port_scan_rate)])

        # Add custom args
        nmap_cmd.extend(args.split())

        # Add target
        nmap_cmd.append(target)

        print(f"[NMAP] Running scan: {' '.join(nmap_cmd)}")

        try:
            # Apply scan delay before starting
            self._apply_scan_delay()

            result = subprocess.run(
                nmap_cmd,
                capture_output=True,
                text=True,
                timeout=300  # 5 minute timeout
            )

            if result.returncode != 0:
                print(f"[NMAP] Warning: nmap exited with code {result.returncode}")
                if result.stderr:
                    print(f"[NMAP] Error output: {result.stderr}")

            # Debug: Check if we got any output
            output_lines = result.stdout.strip().splitlines()
            if len(output_lines) == 0:
                print(f"[NMAP] Warning: No output from nmap scan")
            else:
                print(f"[NMAP] Received {len(output_lines)} lines of output")

            # Save output for debugging if environment variable set
            import os
            if os.environ.get('DIGIPET_DEBUG'):
                debug_file = Path.home() / '.digipet' / 'nmap_debug.txt'
                with open(debug_file, 'a') as f:
                    f.write(f"\n\n=== Scan at {datetime.now()} ===\n")
                    f.write(f"Command: {' '.join(nmap_cmd)}\n")
                    f.write(f"Output:\n{result.stdout}\n")
                print(f"[NMAP] Debug output saved to {debug_file}")

            discovered = self._parse_nmap_output(result.stdout)
            print(f"[NMAP] Scan complete. Discovered {len(discovered)} hosts")

            return discovered

        except subprocess.TimeoutExpired:
            print("[NMAP] Scan timed out after 5 minutes")
        except Exception as e:
            print(f"[NMAP] Error running scan: {e}")

        return []

    def quick_port_scan(self, target_ip: str, ports: List[int] = None) -> List[Dict]:
        """
        Quick TCP port scan without nmap (using Python sockets).
        Good for when nmap isn't available or for targeted scanning.

        Args:
            target_ip: Target IP address
            ports: List of ports to scan (default: common ports)

        Returns:
            List of open ports with basic info
        """
        import socket

        if not ports:
            # Common ports
            ports = [21, 22, 23, 25, 53, 80, 110, 111, 135, 139, 143, 443, 445, 993, 995, 1723, 3306, 3389, 5900, 8080]

        profile = self.config.get_current_profile()
        open_ports = []

        # Get or create host
        host = self.db.get_host_by_ip(target_ip)
        if host:
            host_id = host['id']
        else:
            host_id = self.db.add_host(ip=target_ip)

        for port in ports:
            # Apply delay between port scans
            self._apply_scan_delay()

            try:
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.settimeout(1)
                result = sock.connect_ex((target_ip, port))

                if result == 0:
                    # Port is open
                    service_id, is_new = self.db.add_service(
                        host_id=host_id,
                        port=port,
                        protocol='tcp'
                    )

                    open_ports.append({
                        'port': port,
                        'host_id': host_id,
                        'service_id': service_id,
                        'is_new': is_new
                    })

                    # Award XP only for NEW services
                    if is_new:
                        xp_result = self.evolution.add_xp('service_enumerated')
                        self.db.log_activity(
                            event_type='service_enumerated',
                            target=f"{target_ip}:{port}",
                            details=f"Open TCP port discovered",
                            xp_earned=xp_result['xp_gained']
                        )

                sock.close()

            except Exception as e:
                print(f"Error scanning {target_ip}:{port} - {e}")

        return open_ports

    def _parse_nmap_output(self, output: str) -> List[Dict]:
        """Parse nmap output and add to database."""
        discovered = []
        current_host = None
        current_host_id = None
        total_services_found = 0

        for line in output.splitlines():
            # Detect new host
            host_match = re.search(r'Nmap scan report for (?:(\S+) \()?(\d+\.\d+\.\d+\.\d+)', line)
            if host_match:
                hostname = host_match.group(1)
                ip = host_match.group(2)

                current_host = ip
                current_host_id, is_new_host = self.db.add_host(ip=ip, hostname=hostname)

                discovered.append({'ip': ip, 'hostname': hostname, 'services': [], 'is_new': is_new_host})

                # Award XP only for NEW host discovery
                if is_new_host:
                    xp_result = self.evolution.add_xp('host_discovered')
                    self.db.log_activity(
                        event_type='host_discovered',
                        target=ip,
                        details=f"Hostname: {hostname or 'unknown'}",
                        xp_earned=xp_result['xp_gained']
                    )

                continue

            # Detect open ports
            port_match = re.match(r'(\d+)/(tcp|udp)\s+open\s+(\S+)(?:\s+(.+))?', line)
            if port_match and current_host_id:
                total_services_found += 1
                port = int(port_match.group(1))
                protocol = port_match.group(2)
                service_name = port_match.group(3)
                version = port_match.group(4)

                service_id, is_new_service = self.db.add_service(
                    host_id=current_host_id,
                    port=port,
                    protocol=protocol,
                    service_name=service_name,
                    version=version
                )

                if discovered:
                    discovered[-1]['services'].append({
                        'port': port,
                        'protocol': protocol,
                        'service': service_name,
                        'version': version,
                        'is_new': is_new_service
                    })

                # Award XP only for NEW service enumeration
                if is_new_service:
                    xp_result = self.evolution.add_xp('service_enumerated')
                    self.db.log_activity(
                        event_type='service_enumerated',
                        target=f"{current_host}:{port}",
                        details=f"{service_name} {version or ''}",
                        xp_earned=xp_result['xp_gained']
                    )

        # Log parsing results
        if total_services_found > 0:
            print(f"[NMAP] Parsed {total_services_found} services from output")
        else:
            print(f"[NMAP] No services found in output (hosts may be up but no open ports)")

        return discovered

    def _resolve_hostname(self, ip: str) -> Optional[str]:
        """Try to resolve hostname from IP."""
        import socket
        try:
            return socket.gethostbyaddr(ip)[0]
        except:
            return None

    def _check_nmap_installed(self) -> bool:
        """Check if nmap is available."""
        try:
            subprocess.run(['which', 'nmap'], capture_output=True, check=True)
            return True
        except:
            return False

    def _apply_scan_delay(self):
        """Apply delay based on stealth profile."""
        profile = self.config.get_current_profile()

        if profile.randomize_timing:
            delay = random.uniform(profile.scan_delay_min, profile.scan_delay_max)
        else:
            delay = profile.scan_delay_min

        time.sleep(delay)

    # ==================== OPTIMIZATION FEATURES ====================

    def get_priority_targets(self, limit: int = None) -> List[Dict]:
        """
        Get prioritized list of hosts to scan based on intelligence.

        Priority factors:
        1. High service count (likely servers)
        2. Recently discovered (may have more to find)
        3. Not scanned recently (stale data)
        4. Has credentials/vulns (high value)

        Args:
            limit: Maximum number of targets to return

        Returns:
            Sorted list of hosts with priority scores
        """
        all_hosts = self.db.get_hosts()

        if not all_hosts:
            return []

        # Score each host
        scored_hosts = []
        now = datetime.now()

        for host in all_hosts:
            score = 0
            host_id = host['id']

            # Factor 1: Service count (more services = higher value target)
            services = self.db.get_services_for_host(host_id)
            service_count = len(services)
            score += service_count * 10

            # Factor 2: Has credentials (very high value)
            creds = [c for c in self.db.get_credentials() if c.get('host_id') == host_id]
            score += len(creds) * 50

            # Factor 3: Has vulnerabilities (high value)
            vulns = [v for v in self.db.get_vulnerabilities() if v.get('host_id') == host_id]
            score += len(vulns) * 30

            # Factor 4: Last scan age (older = needs refresh)
            last_seen = host.get('last_seen')
            if last_seen:
                try:
                    last_seen_dt = datetime.fromisoformat(last_seen)
                    hours_old = (now - last_seen_dt).total_seconds() / 3600

                    if hours_old > 24:  # Stale data
                        score += 20
                    elif hours_old > 12:
                        score += 10
                except:
                    pass

            # Factor 5: Hostname suggests server
            hostname = host.get('hostname')
            if hostname:  # Only check if hostname exists
                hostname_lower = hostname.lower()
                server_keywords = ['server', 'srv', 'web', 'db', 'mail', 'dns', 'dc', 'proxy']
                if any(kw in hostname_lower for kw in server_keywords):
                    score += 25

            scored_hosts.append({
                'host': host,
                'score': score,
                'service_count': service_count,
                'cred_count': len(creds),
                'vuln_count': len(vulns)
            })

        # Sort by score (highest first)
        scored_hosts.sort(key=lambda x: x['score'], reverse=True)

        if limit:
            scored_hosts = scored_hosts[:limit]

        print(f"[PRIORITY] Scored {len(all_hosts)} hosts, top {len(scored_hosts)} selected")
        if scored_hosts:
            top = scored_hosts[0]
            print(f"[PRIORITY] Top target: {top['host']['ip_address']} (score: {top['score']}, "
                  f"services: {top['service_count']}, creds: {top['cred_count']}, vulns: {top['vuln_count']})")

        return scored_hosts

    def get_unscanned_hosts(self, target_network: str = None) -> List[str]:
        """
        Get list of IP addresses in target network that haven't been scanned yet.
        Enables incremental scanning.

        Args:
            target_network: CIDR network (e.g., "192.168.1.0/24")

        Returns:
            List of unscanned IP addresses
        """
        if not target_network:
            attack_config = self.config.attack_config
            if attack_config.target_network == "auto":
                target_network = self.get_local_network()
            else:
                target_network = attack_config.target_network

        if not target_network:
            return []

        try:
            network = ipaddress.ip_network(target_network, strict=False)
        except:
            print(f"[INCREMENTAL] Invalid network: {target_network}")
            return []

        # Get all IPs in network
        all_ips = [str(ip) for ip in network.hosts()]

        # Get scanned IPs from database
        known_hosts = self.db.get_hosts()
        scanned_ips = set(host['ip_address'] for host in known_hosts)

        # Find unscanned
        unscanned = [ip for ip in all_ips if ip not in scanned_ips]

        print(f"[INCREMENTAL] Network {target_network}: {len(scanned_ips)} scanned, "
              f"{len(unscanned)} unscanned ({len(unscanned)/len(all_ips)*100:.1f}% remaining)")

        return unscanned

    def parallel_port_scan(self, target_ips: List[str], ports: List[int] = None,
                          max_workers: int = None) -> Dict[str, List[Dict]]:
        """
        Parallel port scanning across multiple hosts simultaneously.
        MUCH faster than sequential scanning.

        Args:
            target_ips: List of IP addresses to scan
            ports: List of ports to scan (default: common ports)
            max_workers: Number of parallel threads (default: based on profile)

        Returns:
            Dict mapping IP -> list of open ports
        """
        if not ports:
            ports = [21, 22, 23, 25, 53, 80, 110, 111, 135, 139, 143, 443, 445,
                    993, 995, 1723, 3306, 3389, 5432, 5900, 8080, 8443]

        profile = self.config.get_current_profile()

        # Determine worker count based on stealth profile
        if not max_workers:
            if profile.name == 'aggressive':
                max_workers = 20  # Very parallel
            elif profile.name == 'normal':
                max_workers = 10  # Balanced
            elif profile.name == 'stealth':
                max_workers = 3   # Conservative
            else:  # passive
                max_workers = 1   # Sequential

        print(f"[PARALLEL] Scanning {len(target_ips)} hosts with {max_workers} workers, "
              f"{len(ports)} ports each")

        results = {}
        total_scanned = 0
        start_time = time.time()

        # Use thread pool for parallel execution
        with ThreadPoolExecutor(max_workers=max_workers) as executor:
            # Submit all scan jobs
            future_to_ip = {
                executor.submit(self._scan_host_ports, ip, ports): ip
                for ip in target_ips
            }

            # Process results as they complete
            for future in as_completed(future_to_ip):
                ip = future_to_ip[future]
                total_scanned += 1

                try:
                    open_ports = future.result()
                    results[ip] = open_ports

                    if open_ports:
                        print(f"[PARALLEL] {ip}: {len(open_ports)} open ports "
                              f"({total_scanned}/{len(target_ips)})")

                except Exception as e:
                    print(f"[PARALLEL] Error scanning {ip}: {e}")
                    results[ip] = []

        elapsed = time.time() - start_time
        total_ports = sum(len(ports) for ports in results.values())

        print(f"[PARALLEL] Scan complete: {len(target_ips)} hosts, {total_ports} open ports "
              f"found in {elapsed:.1f}s ({len(target_ips)/elapsed:.1f} hosts/sec)")

        return results

    def _scan_host_ports(self, target_ip: str, ports: List[int]) -> List[Dict]:
        """
        Scan all ports on a single host (used by parallel_port_scan).

        Args:
            target_ip: IP address to scan
            ports: List of ports to check

        Returns:
            List of open ports with details
        """
        open_ports = []

        # Get or create host
        host = self.db.get_host_by_ip(target_ip)
        if host:
            host_id = host['id']
        else:
            host_id, _ = self.db.add_host(ip=target_ip)

        for port in ports:
            try:
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.settimeout(1.0)
                result = sock.connect_ex((target_ip, port))

                if result == 0:
                    # Port is open
                    service_id, is_new = self.db.add_service(
                        host_id=host_id,
                        port=port,
                        protocol='tcp'
                    )

                    open_ports.append({
                        'port': port,
                        'host_id': host_id,
                        'service_id': service_id,
                        'is_new': is_new
                    })

                    # Award XP only for NEW services
                    if is_new:
                        xp_result = self.evolution.add_xp('service_enumerated')
                        self.db.log_activity(
                            event_type='service_enumerated',
                            target=f"{target_ip}:{port}",
                            details=f"Open TCP port discovered (parallel scan)",
                            xp_earned=xp_result['xp_gained']
                        )

                sock.close()

            except Exception:
                pass  # Port closed or filtered

        return open_ports

    def smart_incremental_scan(self, full_scan: bool = False) -> Dict:
        """
        Intelligent incremental scanning strategy.

        Strategy:
        1. If full_scan=False: Only scan new/unscanned hosts
        2. Prioritize high-value targets
        3. Use parallel scanning for speed
        4. Return comprehensive results

        Args:
            full_scan: If True, scan entire network. If False, only new hosts.

        Returns:
            Dict with scan results and statistics
        """
        print(f"[SMART SCAN] Starting {'full' if full_scan else 'incremental'} scan")

        start_time = time.time()
        stats = {
            'hosts_scanned': 0,
            'new_hosts': 0,
            'services_found': 0,
            'scan_time': 0,
            'mode': 'full' if full_scan else 'incremental'
        }

        # Determine targets
        if full_scan:
            # Scan all hosts (prioritized)
            priority_targets = self.get_priority_targets()
            target_ips = [t['host']['ip_address'] for t in priority_targets]
            print(f"[SMART SCAN] Full scan mode: {len(target_ips)} hosts (prioritized)")
        else:
            # Incremental: only unscanned hosts
            unscanned = self.get_unscanned_hosts()
            if not unscanned:
                print("[SMART SCAN] No unscanned hosts found, using priority targets")
                priority_targets = self.get_priority_targets(limit=10)
                target_ips = [t['host']['ip_address'] for t in priority_targets]
            else:
                target_ips = unscanned[:50]  # Limit for performance
                print(f"[SMART SCAN] Incremental mode: {len(target_ips)} unscanned hosts")

        if not target_ips:
            print("[SMART SCAN] No targets to scan")
            return stats

        # Use parallel scanning
        results = self.parallel_port_scan(target_ips)

        # Calculate stats
        stats['hosts_scanned'] = len(target_ips)
        stats['new_hosts'] = len([ip for ip in target_ips if results.get(ip)])
        stats['services_found'] = sum(len(ports) for ports in results.values())
        stats['scan_time'] = time.time() - start_time

        print(f"[SMART SCAN] Complete: {stats['hosts_scanned']} hosts, "
              f"{stats['services_found']} services in {stats['scan_time']:.1f}s")

        return stats
