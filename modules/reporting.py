"""
Report generation module - creates professional pentest reports.
"""

import json
from pathlib import Path
from datetime import datetime
from typing import Dict, List, Optional


class ReportGenerator:
    """Generates reports in multiple formats."""

    def __init__(self, database, evolution_engine, config):
        self.db = database
        self.evolution = evolution_engine
        self.config = config

    def generate_all_formats(self, output_dir: Path) -> Dict[str, Path]:
        """
        Generate reports in all formats.

        Returns:
            Dict mapping format name to output file path
        """
        output_dir = Path(output_dir)
        output_dir.mkdir(parents=True, exist_ok=True)

        timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')

        reports = {}

        # JSON report
        json_file = output_dir / f'digipet_report_{timestamp}.json'
        self.generate_json_report(json_file)
        reports['json'] = json_file

        # Markdown report
        md_file = output_dir / f'digipet_report_{timestamp}.md'
        self.generate_markdown_report(md_file)
        reports['markdown'] = md_file

        # HTML report
        html_file = output_dir / f'digipet_report_{timestamp}.html'
        self.generate_html_report(html_file)
        reports['html'] = html_file

        # CSV exports
        csv_dir = output_dir / f'digipet_csv_{timestamp}'
        self.generate_csv_exports(csv_dir)
        reports['csv'] = csv_dir

        return reports

    def _gather_data(self) -> Dict:
        """Gather all data for reporting."""
        return {
            'metadata': {
                'generated_at': datetime.now().isoformat(),
                'report_version': '1.0',
                'digipet_version': '1.0.0'
            },
            'evolution': self.evolution.get_full_status(),
            'stats': self.db.get_stats(),
            'hosts': self.db.get_hosts(),
            'services': self.db.get_all_services(),
            'credentials': self.db.get_credentials(),
            'vulnerabilities': self.db.get_vulnerabilities(),
            'activity_log': self.db.get_activity_log(limit=1000),
            'config': {
                'profile': self.config.current_profile_name,
                'profile_settings': self.config.get_current_profile().__dict__
            }
        }

    def generate_json_report(self, output_file: Path) -> Path:
        """Generate comprehensive JSON report."""
        data = self._gather_data()

        with open(output_file, 'w') as f:
            json.dump(data, f, indent=2, default=str)

        return output_file

    def generate_markdown_report(self, output_file: Path) -> Path:
        """Generate Markdown pentest report."""
        data = self._gather_data()

        md = []

        # Header
        md.append("# Digipet Penetration Testing Report")
        md.append("")
        md.append(f"**Generated:** {data['metadata']['generated_at']}")
        md.append(f"**Profile:** {data['config']['profile']}")
        md.append("")

        # Executive Summary
        md.append("## Executive Summary")
        md.append("")
        stats = data['stats']
        md.append(f"- **Hosts Discovered:** {stats['total_hosts']}")
        md.append(f"- **Services Enumerated:** {stats['total_services']}")
        md.append(f"- **Credentials Captured:** {stats['total_credentials']}")
        md.append(f"- **Vulnerabilities Found:** {stats['total_vulnerabilities']}")
        md.append("")

        # Evolution Status
        evo = data['evolution']
        current = evo['current_stage']
        md.append("## Digipet Evolution Status")
        md.append("")
        md.append(f"- **Current Form:** {current['name']} (Level {current['level']})")
        md.append(f"- **Total XP:** {evo['stats']['total_xp']}")
        md.append(f"- **Hosts Discovered:** {evo['stats']['hosts_discovered']}")
        md.append(f"- **Services Enumerated:** {evo['stats']['services_enumerated']}")
        md.append(f"- **Credentials Captured:** {evo['stats']['credentials_captured']}")
        md.append(f"- **Vulnerabilities Found:** {evo['stats']['vulnerabilities_found']}")
        md.append("")

        # Critical Findings
        md.append("## Critical Findings")
        md.append("")

        critical_vulns = [v for v in data['vulnerabilities'] if v.get('severity') == 'CRITICAL']
        if critical_vulns:
            md.append(f"⚠️ **{len(critical_vulns)} CRITICAL vulnerabilities identified:**")
            md.append("")
            for vuln in critical_vulns:
                md.append(f"### {vuln.get('cve_id', 'N/A')} - {vuln['title']}")
                md.append(f"- **Host:** {vuln.get('ip_address', 'N/A')}")
                md.append(f"- **Port:** {vuln.get('port', 'N/A')}")
                md.append(f"- **CVSS:** {vuln.get('cvss_score', 'N/A')}")
                md.append(f"- **Description:** {vuln.get('description', 'N/A')}")
                md.append("")
        else:
            md.append("✅ No critical vulnerabilities found.")
            md.append("")

        # Discovered Hosts
        md.append("## Discovered Hosts")
        md.append("")
        md.append("| IP Address | Hostname | MAC Address | First Seen | Services |")
        md.append("|------------|----------|-------------|------------|----------|")

        for host in data['hosts']:
            services_count = len([s for s in data['services'] if s.get('host_id') == host['id']])
            md.append(
                f"| {host['ip_address']} | "
                f"{host.get('hostname', 'N/A')} | "
                f"{host.get('mac_address', 'N/A')} | "
                f"{host.get('first_seen', 'N/A')} | "
                f"{services_count} |"
            )

        md.append("")

        # Services
        md.append("## Enumerated Services")
        md.append("")
        md.append("| Host | Port | Protocol | Service | Version |")
        md.append("|------|------|----------|---------|---------|")

        for service in data['services'][:50]:  # Limit to 50 for readability
            md.append(
                f"| {service.get('ip_address', 'N/A')} | "
                f"{service.get('port', 'N/A')} | "
                f"{service.get('protocol', 'N/A')} | "
                f"{service.get('service_name', 'N/A')} | "
                f"{service.get('service_version', 'N/A')} |"
            )

        if len(data['services']) > 50:
            md.append("")
            md.append(f"*... and {len(data['services']) - 50} more services*")

        md.append("")

        # Credentials
        if data['credentials']:
            md.append("## Captured Credentials")
            md.append("")
            md.append("⚠️ **Sensitive Information - Handle with Care**")
            md.append("")
            md.append("| Username | Type | Service | Host | Captured At |")
            md.append("|----------|------|---------|------|-------------|")

            for cred in data['credentials']:
                md.append(
                    f"| {cred.get('username', 'N/A')} | "
                    f"{cred.get('hash_type', 'plaintext')} | "
                    f"{cred.get('service', 'N/A')} | "
                    f"{cred.get('ip_address', 'N/A')} | "
                    f"{cred.get('captured_at', 'N/A')} |"
                )

            md.append("")

        # Vulnerabilities Summary
        md.append("## Vulnerabilities by Severity")
        md.append("")

        vuln_by_severity = {}
        for vuln in data['vulnerabilities']:
            severity = vuln.get('severity', 'UNKNOWN')
            vuln_by_severity[severity] = vuln_by_severity.get(severity, 0) + 1

        for severity in ['CRITICAL', 'HIGH', 'MEDIUM', 'LOW']:
            count = vuln_by_severity.get(severity, 0)
            md.append(f"- **{severity}:** {count}")

        md.append("")

        # Recommendations
        md.append("## Recommendations")
        md.append("")

        if critical_vulns:
            md.append("### Immediate Actions Required")
            md.append("1. Patch all CRITICAL vulnerabilities immediately")
            md.append("2. Review and rotate any captured credentials")
            md.append("3. Implement network segmentation")
            md.append("")

        md.append("### General Recommendations")
        md.append("1. Keep all systems and services updated")
        md.append("2. Disable unnecessary services")
        md.append("3. Implement strong authentication mechanisms")
        md.append("4. Monitor network traffic for anomalies")
        md.append("5. Regular security assessments")
        md.append("")

        # Disclaimer
        md.append("---")
        md.append("")
        md.append("*This report was generated by Digipet - Autonomous Penetration Testing Platform*")
        md.append("")
        md.append("**Disclaimer:** This report is confidential and intended solely for authorized personnel. "
                 "The vulnerabilities and credentials listed should be addressed immediately and handled with appropriate security measures.")

        # Write file
        with open(output_file, 'w') as f:
            f.write('\n'.join(md))

        return output_file

    def generate_html_report(self, output_file: Path) -> Path:
        """Generate HTML report with styling."""
        data = self._gather_data()

        html = f"""<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Digipet Penetration Testing Report</title>
    <style>
        body {{
            font-family: Arial, sans-serif;
            line-height: 1.6;
            max-width: 1200px;
            margin: 0 auto;
            padding: 20px;
            background: #f4f4f4;
        }}
        .header {{
            background: #2c3e50;
            color: white;
            padding: 30px;
            text-align: center;
            border-radius: 5px;
        }}
        .section {{
            background: white;
            margin: 20px 0;
            padding: 20px;
            border-radius: 5px;
            box-shadow: 0 2px 5px rgba(0,0,0,0.1);
        }}
        h2 {{
            color: #2c3e50;
            border-bottom: 2px solid #3498db;
            padding-bottom: 10px;
        }}
        table {{
            width: 100%;
            border-collapse: collapse;
            margin: 15px 0;
        }}
        th, td {{
            padding: 12px;
            text-align: left;
            border-bottom: 1px solid #ddd;
        }}
        th {{
            background: #3498db;
            color: white;
        }}
        tr:hover {{
            background: #f5f5f5;
        }}
        .critical {{
            color: #e74c3c;
            font-weight: bold;
        }}
        .high {{
            color: #e67e22;
            font-weight: bold;
        }}
        .medium {{
            color: #f39c12;
        }}
        .low {{
            color: #95a5a6;
        }}
        .stat-card {{
            display: inline-block;
            background: #ecf0f1;
            padding: 20px;
            margin: 10px;
            border-radius: 5px;
            min-width: 150px;
            text-align: center;
        }}
        .stat-number {{
            font-size: 2em;
            font-weight: bold;
            color: #3498db;
        }}
        .warning {{
            background: #fff3cd;
            border-left: 4px solid #ffc107;
            padding: 15px;
            margin: 15px 0;
        }}
    </style>
</head>
<body>
    <div class="header">
        <h1>🦖 Digipet Penetration Testing Report</h1>
        <p>Generated: {data['metadata']['generated_at']}</p>
        <p>Profile: {data['config']['profile']}</p>
    </div>

    <div class="section">
        <h2>Executive Summary</h2>
        <div class="stat-card">
            <div class="stat-number">{data['stats']['total_hosts']}</div>
            <div>Hosts Discovered</div>
        </div>
        <div class="stat-card">
            <div class="stat-number">{data['stats']['total_services']}</div>
            <div>Services Found</div>
        </div>
        <div class="stat-card">
            <div class="stat-number">{data['stats']['total_credentials']}</div>
            <div>Credentials Captured</div>
        </div>
        <div class="stat-card">
            <div class="stat-number">{data['stats']['total_vulnerabilities']}</div>
            <div>Vulnerabilities</div>
        </div>
    </div>
"""

        # Critical findings
        critical_vulns = [v for v in data['vulnerabilities'] if v.get('severity') == 'CRITICAL']
        if critical_vulns:
            html += """
    <div class="section">
        <h2>⚠️ Critical Findings</h2>
        <div class="warning">
            <strong>CRITICAL:</strong> {count} critical vulnerabilities require immediate attention!
        </div>
        <table>
            <tr>
                <th>CVE</th>
                <th>Title</th>
                <th>Host</th>
                <th>Port</th>
                <th>CVSS</th>
            </tr>
""".format(count=len(critical_vulns))

            for vuln in critical_vulns:
                html += f"""
            <tr>
                <td class="critical">{vuln.get('cve_id', 'N/A')}</td>
                <td>{vuln.get('title', 'N/A')}</td>
                <td>{vuln.get('ip_address', 'N/A')}</td>
                <td>{vuln.get('port', 'N/A')}</td>
                <td class="critical">{vuln.get('cvss_score', 'N/A')}</td>
            </tr>
"""

            html += """
        </table>
    </div>
"""

        # Hosts table
        html += """
    <div class="section">
        <h2>Discovered Hosts</h2>
        <table>
            <tr>
                <th>IP Address</th>
                <th>Hostname</th>
                <th>MAC Address</th>
                <th>Services</th>
            </tr>
"""

        for host in data['hosts']:
            services_count = len([s for s in data['services'] if s.get('host_id') == host['id']])
            html += f"""
            <tr>
                <td>{host['ip_address']}</td>
                <td>{host.get('hostname', 'N/A')}</td>
                <td>{host.get('mac_address', 'N/A')}</td>
                <td>{services_count}</td>
            </tr>
"""

        html += """
        </table>
    </div>

    <div class="section">
        <p style="text-align: center; color: #7f8c8d;">
            <em>This report was generated by Digipet - Autonomous Penetration Testing Platform</em>
        </p>
        <p style="text-align: center; color: #e74c3c; font-size: 0.9em;">
            <strong>CONFIDENTIAL:</strong> This document contains sensitive security information. Handle with care.
        </p>
    </div>
</body>
</html>
"""

        with open(output_file, 'w') as f:
            f.write(html)

        return output_file

    def generate_csv_exports(self, output_dir: Path) -> Path:
        """Generate CSV files for each data type."""
        import csv

        output_dir = Path(output_dir)
        output_dir.mkdir(parents=True, exist_ok=True)

        data = self._gather_data()

        # Hosts CSV
        with open(output_dir / 'hosts.csv', 'w', newline='') as f:
            if data['hosts']:
                writer = csv.DictWriter(f, fieldnames=data['hosts'][0].keys())
                writer.writeheader()
                writer.writerows(data['hosts'])

        # Services CSV
        with open(output_dir / 'services.csv', 'w', newline='') as f:
            if data['services']:
                writer = csv.DictWriter(f, fieldnames=data['services'][0].keys())
                writer.writeheader()
                writer.writerows(data['services'])

        # Vulnerabilities CSV
        with open(output_dir / 'vulnerabilities.csv', 'w', newline='') as f:
            if data['vulnerabilities']:
                writer = csv.DictWriter(f, fieldnames=data['vulnerabilities'][0].keys())
                writer.writeheader()
                writer.writerows(data['vulnerabilities'])

        # Credentials CSV (sanitized)
        with open(output_dir / 'credentials.csv', 'w', newline='') as f:
            if data['credentials']:
                # Don't export full hashes in CSV
                sanitized = []
                for cred in data['credentials']:
                    sanitized.append({
                        'username': cred.get('username'),
                        'hash_type': cred.get('hash_type'),
                        'service': cred.get('service'),
                        'host': cred.get('ip_address'),
                        'captured_at': cred.get('captured_at')
                    })
                if sanitized:
                    writer = csv.DictWriter(f, fieldnames=sanitized[0].keys())
                    writer.writeheader()
                    writer.writerows(sanitized)

        return output_dir
