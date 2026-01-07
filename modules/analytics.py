"""
Analytics module - scan statistics, trends, and insights.
"""

from datetime import datetime, timedelta
from typing import Dict, List
from collections import Counter


class ScanAnalytics:
    """Provides analytics and insights from scan data."""

    def __init__(self, database):
        self.db = database

    def get_scan_summary(self) -> Dict:
        """Get comprehensive scan summary with key metrics."""
        stats = self.db.get_stats()

        # Calculate host-to-service ratio
        host_service_ratio = 0
        if stats['total_hosts'] > 0:
            host_service_ratio = round(stats['total_services'] / stats['total_hosts'], 2)

        # Calculate vulnerability rate
        vuln_rate = 0
        if stats['total_services'] > 0:
            vuln_rate = round(stats['total_vulnerabilities'] / stats['total_services'] * 100, 2)

        return {
            'total_hosts': stats['total_hosts'],
            'total_services': stats['total_services'],
            'total_credentials': stats['total_credentials'],
            'total_vulnerabilities': stats['total_vulnerabilities'],
            'avg_services_per_host': host_service_ratio,
            'vulnerability_rate_percent': vuln_rate,
            'vulnerabilities_by_severity': stats.get('vulnerabilities_by_severity', {}),
            'total_xp': stats.get('total_xp_from_activities', 0)
        }

    def get_top_services(self, limit: int = 10) -> List[Dict]:
        """Get most common services discovered."""
        services = self.db.get_all_services()

        # Count service names
        service_counter = Counter()
        for service in services:
            service_name = service.get('service_name', 'unknown')
            service_counter[service_name] += 1

        # Convert to list of dicts
        top_services = []
        for service_name, count in service_counter.most_common(limit):
            top_services.append({
                'service': service_name,
                'count': count,
                'percentage': round(count / len(services) * 100, 1) if services else 0
            })

        return top_services

    def get_top_ports(self, limit: int = 10) -> List[Dict]:
        """Get most common open ports."""
        services = self.db.get_all_services()

        # Count ports
        port_counter = Counter()
        port_services = {}

        for service in services:
            port = service.get('port')
            service_name = service.get('service_name', 'unknown')

            if port:
                port_counter[port] += 1
                if port not in port_services:
                    port_services[port] = service_name

        # Convert to list of dicts
        top_ports = []
        for port, count in port_counter.most_common(limit):
            top_ports.append({
                'port': port,
                'service': port_services.get(port, 'unknown'),
                'count': count,
                'percentage': round(count / len(services) * 100, 1) if services else 0
            })

        return top_ports

    def get_host_distribution(self) -> Dict:
        """Get distribution of hosts by service count."""
        hosts = self.db.get_hosts()

        distribution = {
            'no_services': 0,
            'low (1-5)': 0,
            'medium (6-15)': 0,
            'high (16-30)': 0,
            'very_high (31+)': 0
        }

        for host in hosts:
            services = self.db.get_services_for_host(host['id'])
            service_count = len(services)

            if service_count == 0:
                distribution['no_services'] += 1
            elif service_count <= 5:
                distribution['low (1-5)'] += 1
            elif service_count <= 15:
                distribution['medium (6-15)'] += 1
            elif service_count <= 30:
                distribution['high (16-30)'] += 1
            else:
                distribution['very_high (31+)'] += 1

        return distribution

    def get_vulnerability_trends(self) -> Dict:
        """Get vulnerability severity distribution and trends."""
        vulns = self.db.get_vulnerabilities()

        severity_counts = Counter()
        for vuln in vulns:
            severity_counts[vuln.get('severity', 'UNKNOWN')] += 1

        return {
            'total': len(vulns),
            'by_severity': dict(severity_counts),
            'critical_count': severity_counts.get('CRITICAL', 0),
            'high_count': severity_counts.get('HIGH', 0),
            'medium_count': severity_counts.get('MEDIUM', 0),
            'low_count': severity_counts.get('LOW', 0)
        }

    def get_recent_activity(self, hours: int = 24) -> Dict:
        """Get activity statistics for recent time period."""
        cutoff_time = datetime.now() - timedelta(hours=hours)
        cutoff_str = cutoff_time.isoformat()

        activities = self.db.get_activity_log(limit=1000)

        recent_activities = [
            a for a in activities
            if a.get('timestamp', '') >= cutoff_str
        ]

        # Count by event type
        event_counts = Counter()
        xp_total = 0

        for activity in recent_activities:
            event_counts[activity.get('event_type', 'unknown')] += 1
            xp_total += activity.get('xp_earned', 0)

        return {
            'period_hours': hours,
            'total_activities': len(recent_activities),
            'events_by_type': dict(event_counts),
            'total_xp_earned': xp_total,
            'hosts_discovered': event_counts.get('host_discovered', 0),
            'services_found': event_counts.get('service_enumerated', 0),
            'credentials_captured': event_counts.get('credential_captured', 0)
        }

    def get_credential_statistics(self) -> Dict:
        """Get statistics about captured credentials."""
        creds = self.db.get_credentials()

        hash_types = Counter()
        services = Counter()
        unique_users = set()

        for cred in creds:
            hash_type = cred.get('hash_type', 'plaintext')
            service = cred.get('service', 'unknown')
            username = cred.get('username', '')

            hash_types[hash_type] += 1
            services[service] += 1
            if username:
                unique_users.add(username)

        return {
            'total': len(creds),
            'unique_users': len(unique_users),
            'by_hash_type': dict(hash_types),
            'by_service': dict(services),
            'top_services': [
                {'service': svc, 'count': cnt}
                for svc, cnt in services.most_common(5)
            ]
        }

    def get_scan_efficiency(self) -> Dict:
        """Calculate scanning efficiency metrics."""
        stats = self.db.get_stats()
        activities = self.db.get_activity_log(limit=100)

        # Calculate discovery rates
        hosts = stats['total_hosts']
        services = stats['total_services']
        vulns = stats['total_vulnerabilities']
        creds = stats['total_credentials']

        # Count scan activities
        scan_count = sum(1 for a in activities if 'scan' in a.get('event_type', '').lower())

        efficiency = {
            'hosts_per_scan': round(hosts / scan_count, 2) if scan_count > 0 else 0,
            'services_per_host': round(services / hosts, 2) if hosts > 0 else 0,
            'vulns_per_service': round(vulns / services, 3) if services > 0 else 0,
            'creds_per_scan': round(creds / scan_count, 2) if scan_count > 0 else 0,
            'total_scans': scan_count,
            'value_score': self._calculate_value_score(hosts, services, vulns, creds)
        }

        return efficiency

    def _calculate_value_score(self, hosts: int, services: int, vulns: int, creds: int) -> int:
        """Calculate an overall value score for discoveries."""
        # Weighted scoring
        score = (
            hosts * 10 +           # Each host worth 10 points
            services * 5 +         # Each service worth 5 points
            vulns * 20 +           # Each vulnerability worth 20 points
            creds * 50             # Each credential worth 50 points (high value)
        )
        return score

    def get_network_coverage(self, target_network: str) -> Dict:
        """Analyze network coverage based on target network."""
        from modules.network_utils import get_network_info

        network_info = get_network_info(target_network)
        if not network_info:
            return {'error': 'Invalid network'}

        hosts_discovered = self.db.get_stats()['total_hosts']
        total_possible_hosts = network_info['num_hosts']

        coverage_percent = round(hosts_discovered / total_possible_hosts * 100, 2) if total_possible_hosts > 0 else 0

        return {
            'target_network': target_network,
            'total_possible_hosts': total_possible_hosts,
            'hosts_discovered': hosts_discovered,
            'coverage_percent': coverage_percent,
            'remaining_hosts': max(0, total_possible_hosts - hosts_discovered)
        }

    def get_comprehensive_analytics(self) -> Dict:
        """Get all analytics in one call."""
        return {
            'summary': self.get_scan_summary(),
            'top_services': self.get_top_services(limit=10),
            'top_ports': self.get_top_ports(limit=10),
            'host_distribution': self.get_host_distribution(),
            'vulnerability_trends': self.get_vulnerability_trends(),
            'recent_activity': self.get_recent_activity(hours=24),
            'credential_stats': self.get_credential_statistics(),
            'scan_efficiency': self.get_scan_efficiency()
        }
