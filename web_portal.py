"""
Web portal for Digipet - monitoring and configuration interface.
"""

from flask import Flask, jsonify, request, render_template, send_from_directory
from flask_cors import CORS
import bcrypt
from pathlib import Path
from functools import wraps
import socket
import requests

# Import Digipet modules
from modules.database import DigipetDatabase
from modules.evolution import EvolutionEngine, EvolutionConfig
from modules.config import DigipetConfig
from modules.orchestrator import AttackOrchestrator
from modules.reporting import ReportGenerator
from modules.notifications import NotificationManager


app = Flask(__name__,
            static_folder='web_static',
            template_folder='web_templates')
CORS(app)

# Global instances (will be initialized in main)
db = None
evolution = None
config = None
orchestrator = None
scheduler = None
reporter = None
notifier = None
analytics = None


# ==================== AUTHENTICATION ====================

def check_auth(username, password):
    """Check if username/password is valid."""
    if not config.web_portal_config.auth_enabled:
        return True

    if username != config.web_portal_config.username:
        return False

    # Check password hash
    password_hash = config.web_portal_config.password_hash
    if not password_hash:
        # No password set, allow any
        return True

    try:
        return bcrypt.checkpw(password.encode('utf-8'), password_hash.encode('utf-8'))
    except:
        return False


def requires_auth(f):
    """Decorator for routes requiring authentication."""
    @wraps(f)
    def decorated(*args, **kwargs):
        if not config.web_portal_config.auth_enabled:
            return f(*args, **kwargs)

        auth = request.authorization
        if not auth or not check_auth(auth.username, auth.password):
            return jsonify({'error': 'Authentication required'}), 401

        return f(*args, **kwargs)

    return decorated


# ==================== HELPER FUNCTIONS ====================

def check_internet_connectivity():
    """
    Check if the system has internet connectivity.

    Returns:
        bool: True if internet is accessible, False otherwise
    """
    try:
        # Try to resolve a reliable DNS name
        socket.create_connection(("1.1.1.1", 53), timeout=3)
        return True
    except OSError:
        pass

    try:
        # Fallback: try HTTP request to a reliable endpoint
        response = requests.get("http://clients3.google.com/generate_204", timeout=3)
        return response.status_code == 204
    except:
        return False


# ==================== WEB ROUTES ====================

@app.route('/')
def index():
    """Main dashboard page - new modern interface."""
    # Always serve dashboard - JavaScript will handle auth redirect
    return render_template('dashboard_v2.html')


@app.route('/dashboard_classic')
def dashboard_classic():
    """Classic dashboard page."""
    return render_template('dashboard.html')


@app.route('/login')
def login_page():
    """Login page."""
    return render_template('login.html')


@app.route('/config')
def config_page():
    """Configuration page."""
    return render_template('config.html')


@app.route('/host/<ip>')
def host_details_page(ip):
    """Host details page - full view of a specific host."""
    # Get host data
    host = db.get_host_by_ip(ip)
    if not host:
        return "Host not found", 404

    # Get services for this host
    services = db.get_services_for_host(host['id'])

    # Get vulnerabilities for this host
    all_vulns = db.get_vulnerabilities()
    vulnerabilities = [v for v in all_vulns if v.get('host_id') == host['id']]

    # Get credentials for this host
    all_creds = db.get_credentials()
    credentials = [c for c in all_creds if c.get('host_id') == host['id']]

    return render_template('host_details.html',
                         host=host,
                         services=services,
                         vulnerabilities=vulnerabilities,
                         credentials=credentials)


@app.route('/api/sprite')
@requires_auth
def api_sprite():
    """Get current evolution sprite as base64 image."""
    import base64
    from io import BytesIO

    try:
        # Get current evolution stage
        current_stage = evolution.get_current_stage()

        # For now, use the Neko.png sprite
        # In the future, load sprite based on evolution stage
        from modules.sprites import load_sprites
        sprites = load_sprites("Neko.png")

        # Get a frame (middle of animation for static display)
        frame_index = len(sprites) // 2
        sprite_img = sprites[frame_index]

        # Convert to base64
        buffered = BytesIO()
        sprite_img.save(buffered, format="PNG")
        img_str = base64.b64encode(buffered.getvalue()).decode()

        return jsonify({
            'sprite_data': f'data:image/png;base64,{img_str}',
            'evolution_name': current_stage.name,
            'level': current_stage.level
        })

    except Exception as e:
        return jsonify({'error': str(e)}), 500


@app.route('/api/status')
@requires_auth
def api_status():
    """Get overall system status."""
    stats = db.get_stats()
    evolution_status = evolution.get_full_status()
    orchestrator_status = orchestrator.get_status()

    # Check internet connectivity
    internet_connected = check_internet_connectivity()

    return jsonify({
        'stats': stats,
        'evolution': evolution_status,
        'orchestrator': orchestrator_status,
        'internet_connected': internet_connected,
        'config': {
            'current_profile': config.current_profile_name,
            'profile_settings': config.get_current_profile().__dict__,
            'attack_config': config.attack_config.__dict__
        }
    })


# ==================== EVOLUTION ENDPOINTS ====================

@app.route('/api/evolution')
@requires_auth
def api_evolution():
    """Get evolution status."""
    return jsonify(evolution.get_full_status())


@app.route('/api/evolution/reset', methods=['POST'])
@requires_auth
def api_evolution_reset():
    """Reset evolution to baby stage."""
    evolution.reset()
    return jsonify({'success': True, 'message': 'Evolution reset to baby stage'})


# ==================== HOST ENDPOINTS ====================

@app.route('/api/hosts')
@requires_auth
def api_hosts():
    """Get all discovered hosts with service/vulnerability counts."""
    status = request.args.get('status')
    hosts = db.get_hosts(status=status)

    # Enrich hosts with counts
    for host in hosts:
        host_id = host['id']
        host['service_count'] = len(db.get_services_for_host(host_id))

        # Get vulnerability count
        vulns = [v for v in db.get_vulnerabilities() if v.get('host_id') == host_id]
        host['vulnerability_count'] = len(vulns)

        # Get credential count
        creds = [c for c in db.get_credentials() if c.get('host_id') == host_id]
        host['credential_count'] = len(creds)

    return jsonify({'hosts': hosts, 'count': len(hosts)})


@app.route('/api/hosts/<ip>')
@requires_auth
def api_host_details(ip):
    """Get details for a specific host."""
    host = db.get_host_by_ip(ip)
    if not host:
        return jsonify({'error': 'Host not found'}), 404

    services = db.get_services_for_host(host['id'])
    credentials = db.get_credentials(host_id=host['id'])
    vulnerabilities = db.get_vulnerabilities(host_id=host['id'])

    return jsonify({
        'host': host,
        'services': services,
        'credentials': credentials,
        'vulnerabilities': vulnerabilities
    })


# ==================== SERVICE ENDPOINTS ====================

@app.route('/api/services')
@requires_auth
def api_services():
    """Get all discovered services."""
    services = db.get_all_services()
    return jsonify({'services': services, 'count': len(services)})


# ==================== CREDENTIAL ENDPOINTS ====================

@app.route('/api/credentials')
@requires_auth
def api_credentials():
    """Get all captured credentials."""
    credentials = db.get_credentials()
    return jsonify({'credentials': credentials, 'count': len(credentials)})


@app.route('/api/credentials/add', methods=['POST'])
@requires_auth
def api_credentials_add():
    """Manually add a credential."""
    data = request.json

    cred_id = orchestrator.credential_harvester.manual_add_credential(
        username=data.get('username'),
        password=data.get('password'),
        hash_value=data.get('hash_value'),
        hash_type=data.get('hash_type'),
        host_ip=data.get('host_ip'),
        service=data.get('service')
    )

    return jsonify({'success': True, 'credential_id': cred_id})


@app.route('/api/credentials/summary')
@requires_auth
def api_credentials_summary():
    """Get credential capture summary."""
    summary = orchestrator.credential_harvester.get_captured_credentials()
    return jsonify(summary)


# ==================== VULNERABILITY ENDPOINTS ====================

@app.route('/api/vulnerabilities')
@requires_auth
def api_vulnerabilities():
    """Get all vulnerabilities."""
    severity = request.args.get('severity')
    vulns = db.get_vulnerabilities(severity=severity)
    return jsonify({'vulnerabilities': vulns, 'count': len(vulns)})


@app.route('/api/vulnerabilities/summary')
@requires_auth
def api_vulnerabilities_summary():
    """Get vulnerability summary."""
    summary = orchestrator.vulnerability_scanner.get_vulnerability_summary()
    return jsonify(summary)


@app.route('/api/vulnerabilities/scan', methods=['POST'])
@requires_auth
def api_vulnerabilities_scan():
    """Trigger a vulnerability scan."""
    results = orchestrator.run_single_scan()
    return jsonify(results)


# ==================== ACTIVITY LOG ENDPOINTS ====================

@app.route('/api/activity')
@requires_auth
def api_activity():
    """Get activity log."""
    limit = int(request.args.get('limit', 100))
    event_type = request.args.get('event_type')

    activities = db.get_activity_log(limit=limit, event_type=event_type)
    return jsonify({'activities': activities, 'count': len(activities)})


# ==================== ORCHESTRATOR ENDPOINTS ====================

@app.route('/api/orchestrator/start', methods=['POST'])
@requires_auth
def api_orchestrator_start():
    """Start autonomous operations."""
    success = orchestrator.start()
    return jsonify({'success': success})


@app.route('/api/orchestrator/stop', methods=['POST'])
@requires_auth
def api_orchestrator_stop():
    """Stop autonomous operations."""
    orchestrator.stop()
    return jsonify({'success': True})


@app.route('/api/orchestrator/pause', methods=['POST'])
@requires_auth
def api_orchestrator_pause():
    """Pause operations."""
    orchestrator.pause()
    return jsonify({'success': True})


@app.route('/api/orchestrator/resume', methods=['POST'])
@requires_auth
def api_orchestrator_resume():
    """Resume operations."""
    orchestrator.resume()
    return jsonify({'success': True})


@app.route('/api/orchestrator/status')
@requires_auth
def api_orchestrator_status():
    """Get orchestrator status."""
    return jsonify(orchestrator.get_status())


@app.route('/api/orchestrator/recon', methods=['POST'])
@requires_auth
def api_orchestrator_recon():
    """Run manual reconnaissance."""
    results = orchestrator.run_single_recon()
    return jsonify(results)


@app.route('/api/scan/smart', methods=['POST'])
@requires_auth
def api_smart_scan():
    """Run smart incremental scan."""
    data = request.json or {}
    full_scan = data.get('full_scan', False)

    stats = orchestrator.network_recon.smart_incremental_scan(full_scan=full_scan)
    return jsonify({'success': True, 'stats': stats})


@app.route('/api/scan/parallel', methods=['POST'])
@requires_auth
def api_parallel_scan():
    """Run parallel port scan on specific targets."""
    data = request.json or {}
    target_ips = data.get('targets', [])
    ports = data.get('ports')
    max_workers = data.get('max_workers')

    if not target_ips:
        return jsonify({'error': 'No targets specified'}), 400

    results = orchestrator.network_recon.parallel_port_scan(
        target_ips=target_ips,
        ports=ports,
        max_workers=max_workers
    )

    return jsonify({'success': True, 'results': results})


@app.route('/api/scan/priority-targets')
@requires_auth
def api_priority_targets():
    """Get prioritized scan targets."""
    limit = int(request.args.get('limit', 20))
    targets = orchestrator.network_recon.get_priority_targets(limit=limit)

    return jsonify({
        'targets': targets,
        'count': len(targets)
    })


@app.route('/api/scan/unscanned')
@requires_auth
def api_unscanned_hosts():
    """Get list of unscanned hosts in target network."""
    network = request.args.get('network')
    unscanned = orchestrator.network_recon.get_unscanned_hosts(target_network=network)

    return jsonify({
        'unscanned': unscanned,
        'count': len(unscanned)
    })


@app.route('/api/orchestrator/progress')
@requires_auth
def api_orchestrator_progress():
    """Get real-time scan progress."""
    return jsonify(orchestrator.current_progress)


@app.route('/api/orchestrator/errors')
@requires_auth
def api_orchestrator_errors():
    """Get error status."""
    return jsonify(orchestrator.get_error_status())


@app.route('/api/orchestrator/errors/clear', methods=['POST'])
@requires_auth
def api_orchestrator_clear_errors():
    """Clear error count."""
    orchestrator.clear_errors()
    return jsonify({'success': True, 'message': 'Error count cleared'})


# ==================== CONFIG ENDPOINTS ====================

@app.route('/api/config')
@requires_auth
def api_config():
    """Get full configuration."""
    return jsonify(config.get_full_config())


@app.route('/api/config/profile', methods=['POST'])
@requires_auth
def api_config_set_profile():
    """Set stealth profile."""
    data = request.json
    profile_name = data.get('profile')

    success = config.set_profile(profile_name)

    if success:
        return jsonify({'success': True, 'profile': profile_name})
    else:
        return jsonify({'error': 'Invalid profile name'}), 400


@app.route('/api/config/attack', methods=['POST'])
@requires_auth
def api_config_attack():
    """Update attack configuration."""
    data = request.json
    config.update_attack_config(**data)
    return jsonify({'success': True})


@app.route('/api/config/display', methods=['POST'])
@requires_auth
def api_config_display():
    """Update display configuration."""
    data = request.json
    config.update_display_config(**data)
    return jsonify({'success': True})


@app.route('/api/config/save', methods=['POST'])
@requires_auth
def api_config_save():
    """Manually save configuration to disk."""
    config.save_config()
    return jsonify({'success': True, 'message': 'Configuration saved'})


@app.route('/api/system/interfaces')
@requires_auth
def api_system_interfaces():
    """Get available network interfaces with enhanced details."""
    from modules.network_utils import get_network_interfaces, get_primary_interface

    interfaces = get_network_interfaces()
    primary = get_primary_interface()

    return jsonify({
        'interfaces': interfaces,
        'primary': primary,
        'count': len(interfaces)
    })


@app.route('/api/system/networks/scannable')
@requires_auth
def api_scannable_networks():
    """Get networks suitable for scanning."""
    from modules.network_utils import get_scannable_networks

    networks = get_scannable_networks()
    return jsonify({'networks': networks, 'count': len(networks)})


@app.route('/api/system/networks/suggest')
@requires_auth
def api_suggest_network():
    """Suggest best network target for scanning."""
    from modules.network_utils import suggest_scan_target, get_network_info

    suggestion = suggest_scan_target()

    if suggestion:
        info = get_network_info(suggestion)
        return jsonify({
            'suggested_network': suggestion,
            'info': info
        })
    else:
        return jsonify({'error': 'No suitable network found'}), 404


@app.route('/api/system/networks/info/<path:cidr>')
@requires_auth
def api_network_info(cidr):
    """Get detailed information about a network CIDR."""
    from modules.network_utils import get_network_info

    info = get_network_info(cidr)

    if info:
        return jsonify(info)
    else:
        return jsonify({'error': 'Invalid network CIDR'}), 400


@app.route('/api/export/hosts/csv')
@requires_auth
def api_export_hosts_csv():
    """Export hosts as CSV."""
    import csv
    from io import StringIO

    hosts = db.get_hosts()
    output = StringIO()
    writer = csv.writer(output)
    writer.writerow(['IP Address', 'Hostname', 'MAC Address', 'Vendor', 'First Seen', 'Last Seen', 'Status'])

    for host in hosts:
        writer.writerow([
            host['ip_address'],
            host.get('hostname', ''),
            host.get('mac_address', ''),
            host.get('vendor', ''),
            host.get('first_seen', ''),
            host.get('last_seen', ''),
            host.get('status', '')
        ])

    from flask import make_response
    response = make_response(output.getvalue())
    response.headers['Content-Type'] = 'text/csv'
    response.headers['Content-Disposition'] = 'attachment; filename=digipet_hosts.csv'
    return response


@app.route('/api/export/json')
@requires_auth
def api_export_json():
    """Export all data as JSON."""
    data = {
        'hosts': db.get_hosts(),
        'services': db.get_all_services(),
        'credentials': db.get_credentials(),
        'vulnerabilities': db.get_vulnerabilities(),
        'stats': db.get_stats(),
        'evolution': evolution.get_full_status(),
        'exported_at': __import__('datetime').datetime.now().isoformat()
    }

    from flask import make_response
    import json
    response = make_response(json.dumps(data, indent=2))
    response.headers['Content-Type'] = 'application/json'
    response.headers['Content-Disposition'] = 'attachment; filename=digipet_export.json'
    return response


# ==================== STATISTICS ENDPOINTS ====================

@app.route('/api/stats')
@requires_auth
def api_stats():
    """Get database statistics."""
    return jsonify(db.get_stats())


@app.route('/api/database/size')
@requires_auth
def api_database_size():
    """Get database size information."""
    return jsonify(db.get_database_size())


# ==================== ANALYTICS ENDPOINTS ====================

@app.route('/api/analytics/summary')
@requires_auth
def api_analytics_summary():
    """Get scan summary analytics."""
    return jsonify(analytics.get_scan_summary())


@app.route('/api/analytics/top-services')
@requires_auth
def api_analytics_top_services():
    """Get most common services."""
    limit = int(request.args.get('limit', 10))
    return jsonify({'services': analytics.get_top_services(limit=limit)})


@app.route('/api/analytics/top-ports')
@requires_auth
def api_analytics_top_ports():
    """Get most common ports."""
    limit = int(request.args.get('limit', 10))
    return jsonify({'ports': analytics.get_top_ports(limit=limit)})


@app.route('/api/analytics/host-distribution')
@requires_auth
def api_analytics_host_distribution():
    """Get host distribution by service count."""
    return jsonify(analytics.get_host_distribution())


@app.route('/api/analytics/vulnerabilities')
@requires_auth
def api_analytics_vulnerabilities():
    """Get vulnerability trends."""
    return jsonify(analytics.get_vulnerability_trends())


@app.route('/api/analytics/activity')
@requires_auth
def api_analytics_activity():
    """Get recent activity statistics."""
    hours = int(request.args.get('hours', 24))
    return jsonify(analytics.get_recent_activity(hours=hours))


@app.route('/api/analytics/credentials')
@requires_auth
def api_analytics_credentials():
    """Get credential statistics."""
    return jsonify(analytics.get_credential_statistics())


@app.route('/api/analytics/efficiency')
@requires_auth
def api_analytics_efficiency():
    """Get scanning efficiency metrics."""
    return jsonify(analytics.get_scan_efficiency())


@app.route('/api/analytics/coverage')
@requires_auth
def api_analytics_coverage():
    """Get network coverage analysis."""
    target_network = request.args.get('network', config.attack_config.target_network)
    return jsonify(analytics.get_network_coverage(target_network))


@app.route('/api/analytics/comprehensive')
@requires_auth
def api_analytics_comprehensive():
    """Get all analytics in one call."""
    return jsonify(analytics.get_comprehensive_analytics())


# ==================== DATA MANAGEMENT ENDPOINTS ====================

@app.route('/api/data/clear/all', methods=['POST'])
@requires_auth
def api_clear_all_data():
    """Clear all discovered data."""
    data = request.json or {}
    keep_activity_log = data.get('keep_activity_log', True)

    # Stop orchestrator if running
    if orchestrator.running:
        return jsonify({'error': 'Cannot clear data while orchestrator is running. Stop it first.'}), 400

    stats = db.clear_all_data(keep_activity_log=keep_activity_log)
    return jsonify({'success': True, 'stats': stats})


@app.route('/api/data/clear/hosts', methods=['POST'])
@requires_auth
def api_clear_hosts():
    """Clear all hosts."""
    if orchestrator.running:
        return jsonify({'error': 'Cannot clear data while orchestrator is running'}), 400

    count = db.clear_hosts()
    return jsonify({'success': True, 'hosts_deleted': count})


@app.route('/api/data/clear/credentials', methods=['POST'])
@requires_auth
def api_clear_credentials():
    """Clear all credentials."""
    count = db.clear_credentials()
    return jsonify({'success': True, 'credentials_deleted': count})


@app.route('/api/data/clear/vulnerabilities', methods=['POST'])
@requires_auth
def api_clear_vulnerabilities():
    """Clear all vulnerabilities."""
    count = db.clear_vulnerabilities()
    return jsonify({'success': True, 'vulnerabilities_deleted': count})


@app.route('/api/data/clear/activity', methods=['POST'])
@requires_auth
def api_clear_activity():
    """Clear activity log."""
    data = request.json or {}
    older_than_days = data.get('older_than_days')

    count = db.clear_activity_log(older_than_days=older_than_days)
    return jsonify({'success': True, 'activities_deleted': count})


@app.route('/api/database/backup', methods=['POST'])
@requires_auth
def api_backup_database():
    """Create database backup."""
    from datetime import datetime
    backup_dir = Path.home() / '.digipet' / 'backups'
    backup_dir.mkdir(parents=True, exist_ok=True)

    timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
    backup_path = backup_dir / f'digipet_backup_{timestamp}.db'

    success = db.backup_database(backup_path)

    if success:
        return jsonify({'success': True, 'backup_path': str(backup_path)})
    else:
        return jsonify({'error': 'Backup failed'}), 500


@app.route('/api/database/vacuum', methods=['POST'])
@requires_auth
def api_vacuum_database():
    """Optimize database."""
    success = db.vacuum_database()

    if success:
        size_info = db.get_database_size()
        return jsonify({'success': True, 'size': size_info})
    else:
        return jsonify({'error': 'Vacuum failed'}), 500


# ==================== REPORTING ENDPOINTS ====================

@app.route('/api/report/generate', methods=['POST'])
@requires_auth
def api_report_generate():
    """Generate a report in specified format."""
    data = request.json
    report_format = data.get('format', 'markdown')

    output_dir = Path.home() / '.digipet' / 'reports'

    try:
        if report_format == 'all':
            reports = reporter.generate_all_formats(output_dir)
            return jsonify({
                'success': True,
                'reports': {fmt: str(path) for fmt, path in reports.items()}
            })
        elif report_format == 'json':
            path = reporter.generate_json_report(output_dir / 'report.json')
        elif report_format == 'markdown':
            path = reporter.generate_markdown_report(output_dir / 'report.md')
        elif report_format == 'html':
            path = reporter.generate_html_report(output_dir / 'report.html')
        elif report_format == 'csv':
            path = reporter.generate_csv_exports(output_dir / 'csv_export')
        else:
            return jsonify({'error': 'Invalid format'}), 400

        return jsonify({'success': True, 'path': str(path)})

    except Exception as e:
        return jsonify({'error': str(e)}), 500


@app.route('/api/report/download/<report_type>')
@requires_auth
def api_report_download(report_type):
    """Download a generated report."""
    from flask import send_file

    reports_dir = Path.home() / '.digipet' / 'reports'

    if report_type == 'latest':
        # Find most recent report
        reports = list(reports_dir.glob('*.md'))
        if not reports:
            return jsonify({'error': 'No reports found'}), 404
        latest = max(reports, key=lambda p: p.stat().st_mtime)
        return send_file(latest, as_attachment=True)

    return jsonify({'error': 'Invalid report type'}), 400


# ==================== SCHEDULER ENDPOINTS ====================

@app.route('/api/scheduler/tasks')
@requires_auth
def api_scheduler_tasks():
    """Get all scheduled tasks."""
    tasks = scheduler.get_all_tasks()
    return jsonify({'tasks': tasks, 'count': len(tasks)})


@app.route('/api/scheduler/tasks/<task_id>/enable', methods=['POST'])
@requires_auth
def api_scheduler_enable_task(task_id):
    """Enable a scheduled task."""
    data = request.json
    enabled = data.get('enabled', True)
    scheduler.enable_task(task_id, enabled)
    return jsonify({'success': True, 'task_id': task_id, 'enabled': enabled})


@app.route('/api/scheduler/tasks/<task_id>/run', methods=['POST'])
@requires_auth
def api_scheduler_run_task(task_id):
    """Manually run a task immediately."""
    success = scheduler.run_task_now(task_id)
    if success:
        return jsonify({'success': True, 'message': f'Task {task_id} triggered'})
    return jsonify({'error': 'Task not found'}), 404


@app.route('/api/scheduler/start', methods=['POST'])
@requires_auth
def api_scheduler_start():
    """Start the scheduler."""
    scheduler.start()
    return jsonify({'success': True, 'message': 'Scheduler started'})


@app.route('/api/scheduler/stop', methods=['POST'])
@requires_auth
def api_scheduler_stop():
    """Stop the scheduler."""
    scheduler.stop()
    return jsonify({'success': True, 'message': 'Scheduler stopped'})


# ==================== NOTIFICATION ENDPOINTS ====================

@app.route('/api/notifications/config', methods=['GET'])
@requires_auth
def api_notifications_get_config():
    """Get notification configuration."""
    # Sanitize config (don't expose passwords)
    config_copy = notifier.notification_config.copy()
    if 'email_password' in config_copy:
        config_copy['email_password'] = '***' if config_copy['email_password'] else None
    return jsonify(config_copy)


@app.route('/api/notifications/config', methods=['POST'])
@requires_auth
def api_notifications_set_config():
    """Update notification configuration."""
    data = request.json
    try:
        notifier.configure(**data)
        return jsonify({'success': True})
    except Exception as e:
        return jsonify({'error': str(e)}), 500


@app.route('/api/notifications/test', methods=['POST'])
@requires_auth
def api_notifications_test():
    """Send test notification."""
    try:
        notifier.test_notifications()
        return jsonify({'success': True, 'message': 'Test notification sent'})
    except Exception as e:
        return jsonify({'error': str(e)}), 500


# ==================== INITIALIZATION ====================

def init_web_portal(database, evolution_engine, digipet_config, attack_orchestrator, task_scheduler):
    """Initialize web portal with Digipet components."""
    global db, evolution, config, orchestrator, scheduler, reporter, notifier, analytics

    db = database
    evolution = evolution_engine
    config = digipet_config
    orchestrator = attack_orchestrator
    scheduler = task_scheduler

    # Initialize reporter, notifier, and analytics
    reporter = ReportGenerator(db, evolution, config)
    notifier = NotificationManager(config)

    from modules.analytics import ScanAnalytics
    analytics = ScanAnalytics(db)

    # Register default scheduled tasks
    _register_default_tasks()

    print("[*] Web portal initialized")


def _register_default_tasks():
    """Register default scheduled tasks."""
    from modules.scheduler import ScheduledTask

    # Daily full recon task (disabled by default)
    daily_recon = ScheduledTask(
        task_id='daily_recon',
        name='Daily Network Reconnaissance',
        task_func=orchestrator.run_single_recon,
        cron_schedule='02:00',  # 2 AM daily
        enabled=False
    )
    scheduler.add_task(daily_recon)

    # Hourly vulnerability scan (disabled by default)
    hourly_vuln_scan = ScheduledTask(
        task_id='hourly_vuln_scan',
        name='Hourly Vulnerability Scan',
        task_func=orchestrator.run_single_scan,
        interval_minutes=60,
        enabled=False
    )
    scheduler.add_task(hourly_vuln_scan)


def run_web_portal(host='0.0.0.0', port=8080):
    """Run the web portal server."""
    import logging

    # Reduce Flask logging verbosity
    log = logging.getLogger('werkzeug')
    log.setLevel(logging.WARNING)  # Only show warnings and errors

    print(f"[*] Starting web portal on http://{host}:{port}")
    app.run(host=host, port=port, debug=False, threaded=True)


if __name__ == '__main__':
    print("Error: web_portal.py should be imported and initialized, not run directly")
    print("Use digipet_daemon.py to start the full system")
