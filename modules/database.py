"""
Database module for Digipet - stores discovered targets, credentials, vulnerabilities.
"""

import sqlite3
from pathlib import Path
from datetime import datetime
from typing import List, Dict, Optional
from contextlib import contextmanager


class DigipetDatabase:
    """SQLite database for storing pentesting results."""

    def __init__(self, db_path: Path):
        self.db_path = db_path
        self.db_path.parent.mkdir(parents=True, exist_ok=True)
        self._init_database()

    @contextmanager
    def _get_connection(self):
        """Context manager for database connections."""
        conn = sqlite3.connect(self.db_path)
        conn.row_factory = sqlite3.Row
        try:
            yield conn
            conn.commit()
        except Exception:
            conn.rollback()
            raise
        finally:
            conn.close()

    def _init_database(self):
        """Create database tables if they don't exist."""
        with self._get_connection() as conn:
            cursor = conn.cursor()

            # Discovered hosts
            cursor.execute("""
                CREATE TABLE IF NOT EXISTS hosts (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    ip_address TEXT UNIQUE NOT NULL,
                    mac_address TEXT,
                    hostname TEXT,
                    vendor TEXT,
                    first_seen TEXT NOT NULL,
                    last_seen TEXT NOT NULL,
                    status TEXT DEFAULT 'active'
                )
            """)

            # Services/ports
            cursor.execute("""
                CREATE TABLE IF NOT EXISTS services (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    host_id INTEGER NOT NULL,
                    port INTEGER NOT NULL,
                    protocol TEXT DEFAULT 'tcp',
                    service_name TEXT,
                    service_version TEXT,
                    banner TEXT,
                    discovered_at TEXT NOT NULL,
                    FOREIGN KEY (host_id) REFERENCES hosts (id),
                    UNIQUE(host_id, port, protocol)
                )
            """)

            # Credentials
            cursor.execute("""
                CREATE TABLE IF NOT EXISTS credentials (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    host_id INTEGER,
                    username TEXT,
                    password TEXT,
                    hash_type TEXT,
                    hash_value TEXT,
                    service TEXT,
                    captured_at TEXT NOT NULL,
                    source TEXT,
                    FOREIGN KEY (host_id) REFERENCES hosts (id)
                )
            """)

            # Vulnerabilities
            cursor.execute("""
                CREATE TABLE IF NOT EXISTS vulnerabilities (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    host_id INTEGER NOT NULL,
                    service_id INTEGER,
                    cve_id TEXT,
                    title TEXT NOT NULL,
                    severity TEXT,
                    description TEXT,
                    cvss_score REAL,
                    discovered_at TEXT NOT NULL,
                    FOREIGN KEY (host_id) REFERENCES hosts (id),
                    FOREIGN KEY (service_id) REFERENCES services (id)
                )
            """)

            # Activity log
            cursor.execute("""
                CREATE TABLE IF NOT EXISTS activity_log (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    timestamp TEXT NOT NULL,
                    event_type TEXT NOT NULL,
                    target TEXT,
                    details TEXT,
                    xp_earned INTEGER DEFAULT 0
                )
            """)

            conn.commit()

    # ==================== HOST METHODS ====================

    def add_host(self, ip: str, mac: str = None, hostname: str = None, vendor: str = None):
        """Add or update a host. Returns (host_id, is_new) tuple."""
        now = datetime.now().isoformat()

        with self._get_connection() as conn:
            cursor = conn.cursor()

            # Check if host already exists
            cursor.execute("SELECT id FROM hosts WHERE ip_address = ?", (ip,))
            existing = cursor.fetchone()

            if existing:
                # Update existing host
                host_id = existing[0]
                cursor.execute("""
                    UPDATE hosts
                    SET mac_address = COALESCE(?, mac_address),
                        hostname = COALESCE(?, hostname),
                        vendor = COALESCE(?, vendor),
                        last_seen = ?
                    WHERE id = ?
                """, (mac, hostname, vendor, now, host_id))
                return (host_id, False)  # Not new
            else:
                # Insert new host
                cursor.execute("""
                    INSERT INTO hosts (ip_address, mac_address, hostname, vendor, first_seen, last_seen)
                    VALUES (?, ?, ?, ?, ?, ?)
                """, (ip, mac, hostname, vendor, now, now))
                return (cursor.lastrowid, True)  # New host

    def get_hosts(self, status: str = None) -> List[Dict]:
        """Get all hosts, optionally filtered by status."""
        with self._get_connection() as conn:
            cursor = conn.cursor()

            if status:
                cursor.execute("SELECT * FROM hosts WHERE status = ? ORDER BY last_seen DESC", (status,))
            else:
                cursor.execute("SELECT * FROM hosts ORDER BY last_seen DESC")

            return [dict(row) for row in cursor.fetchall()]

    def get_host_by_ip(self, ip: str) -> Optional[Dict]:
        """Get host by IP address."""
        with self._get_connection() as conn:
            cursor = conn.cursor()
            cursor.execute("SELECT * FROM hosts WHERE ip_address = ?", (ip,))
            row = cursor.fetchone()
            return dict(row) if row else None

    # ==================== SERVICE METHODS ====================

    def add_service(self, host_id: int, port: int, protocol: str = 'tcp',
                   service_name: str = None, version: str = None, banner: str = None):
        """Add or update a service. Returns (service_id, is_new) tuple."""
        now = datetime.now().isoformat()

        with self._get_connection() as conn:
            cursor = conn.cursor()

            # Check if service already exists
            cursor.execute("""
                SELECT id FROM services WHERE host_id = ? AND port = ? AND protocol = ?
            """, (host_id, port, protocol))
            existing = cursor.fetchone()

            if existing:
                # Update existing service
                service_id = existing[0]
                cursor.execute("""
                    UPDATE services
                    SET service_name = COALESCE(?, service_name),
                        service_version = COALESCE(?, service_version),
                        banner = COALESCE(?, banner)
                    WHERE id = ?
                """, (service_name, version, banner, service_id))
                return (service_id, False)  # Not new
            else:
                # Insert new service
                cursor.execute("""
                    INSERT INTO services (host_id, port, protocol, service_name, service_version, banner, discovered_at)
                    VALUES (?, ?, ?, ?, ?, ?, ?)
                """, (host_id, port, protocol, service_name, version, banner, now))
                return (cursor.lastrowid, True)  # New service

    def get_services_for_host(self, host_id: int) -> List[Dict]:
        """Get all services for a host."""
        with self._get_connection() as conn:
            cursor = conn.cursor()
            cursor.execute("SELECT * FROM services WHERE host_id = ? ORDER BY port", (host_id,))
            return [dict(row) for row in cursor.fetchall()]

    def get_all_services(self) -> List[Dict]:
        """Get all services with host info."""
        with self._get_connection() as conn:
            cursor = conn.cursor()
            cursor.execute("""
                SELECT s.*, h.ip_address, h.hostname
                FROM services s
                JOIN hosts h ON s.host_id = h.id
                ORDER BY h.ip_address, s.port
            """)
            return [dict(row) for row in cursor.fetchall()]

    # ==================== CREDENTIAL METHODS ====================

    def add_credential(self, username: str = None, password: str = None,
                      hash_type: str = None, hash_value: str = None,
                      host_id: int = None, service: str = None, source: str = None) -> int:
        """Add a captured credential."""
        now = datetime.now().isoformat()

        with self._get_connection() as conn:
            cursor = conn.cursor()
            cursor.execute("""
                INSERT INTO credentials (host_id, username, password, hash_type, hash_value, service, captured_at, source)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?)
            """, (host_id, username, password, hash_type, hash_value, service, now, source))
            return cursor.lastrowid

    def get_credentials(self, host_id: int = None) -> List[Dict]:
        """Get credentials, optionally filtered by host."""
        with self._get_connection() as conn:
            cursor = conn.cursor()

            if host_id:
                cursor.execute("""
                    SELECT c.*, h.ip_address, h.hostname
                    FROM credentials c
                    LEFT JOIN hosts h ON c.host_id = h.id
                    WHERE c.host_id = ?
                    ORDER BY c.captured_at DESC
                """, (host_id,))
            else:
                cursor.execute("""
                    SELECT c.*, h.ip_address, h.hostname
                    FROM credentials c
                    LEFT JOIN hosts h ON c.host_id = h.id
                    ORDER BY c.captured_at DESC
                """)

            return [dict(row) for row in cursor.fetchall()]

    # ==================== VULNERABILITY METHODS ====================

    def add_vulnerability(self, host_id: int, title: str, severity: str = None,
                         service_id: int = None, cve_id: str = None,
                         description: str = None, cvss_score: float = None) -> int:
        """Add a discovered vulnerability."""
        now = datetime.now().isoformat()

        with self._get_connection() as conn:
            cursor = conn.cursor()
            cursor.execute("""
                INSERT INTO vulnerabilities
                (host_id, service_id, cve_id, title, severity, description, cvss_score, discovered_at)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?)
            """, (host_id, service_id, cve_id, title, severity, description, cvss_score, now))
            return cursor.lastrowid

    def get_vulnerabilities(self, host_id: int = None, severity: str = None) -> List[Dict]:
        """Get vulnerabilities, optionally filtered."""
        with self._get_connection() as conn:
            cursor = conn.cursor()

            query = """
                SELECT v.*, h.ip_address, h.hostname, s.port, s.service_name
                FROM vulnerabilities v
                JOIN hosts h ON v.host_id = h.id
                LEFT JOIN services s ON v.service_id = s.id
                WHERE 1=1
            """
            params = []

            if host_id:
                query += " AND v.host_id = ?"
                params.append(host_id)

            if severity:
                query += " AND v.severity = ?"
                params.append(severity)

            query += " ORDER BY v.discovered_at DESC"

            cursor.execute(query, params)
            return [dict(row) for row in cursor.fetchall()]

    # ==================== ACTIVITY LOG METHODS ====================

    def log_activity(self, event_type: str, target: str = None, details: str = None, xp_earned: int = 0):
        """Log an activity event."""
        now = datetime.now().isoformat()

        with self._get_connection() as conn:
            cursor = conn.cursor()
            cursor.execute("""
                INSERT INTO activity_log (timestamp, event_type, target, details, xp_earned)
                VALUES (?, ?, ?, ?, ?)
            """, (now, event_type, target, details, xp_earned))

    def get_activity_log(self, limit: int = 100, event_type: str = None) -> List[Dict]:
        """Get recent activity logs."""
        with self._get_connection() as conn:
            cursor = conn.cursor()

            if event_type:
                cursor.execute("""
                    SELECT * FROM activity_log
                    WHERE event_type = ?
                    ORDER BY timestamp DESC LIMIT ?
                """, (event_type, limit))
            else:
                cursor.execute("""
                    SELECT * FROM activity_log
                    ORDER BY timestamp DESC LIMIT ?
                """, (limit,))

            return [dict(row) for row in cursor.fetchall()]

    # ==================== STATISTICS METHODS ====================

    def get_stats(self) -> Dict:
        """Get overall statistics."""
        with self._get_connection() as conn:
            cursor = conn.cursor()

            stats = {}

            cursor.execute("SELECT COUNT(*) FROM hosts WHERE status = 'active'")
            stats['total_hosts'] = cursor.fetchone()[0]

            cursor.execute("SELECT COUNT(*) FROM services")
            stats['total_services'] = cursor.fetchone()[0]

            cursor.execute("SELECT COUNT(*) FROM credentials")
            stats['total_credentials'] = cursor.fetchone()[0]

            cursor.execute("SELECT COUNT(*) FROM vulnerabilities")
            stats['total_vulnerabilities'] = cursor.fetchone()[0]

            cursor.execute("""
                SELECT severity, COUNT(*) as count
                FROM vulnerabilities
                GROUP BY severity
            """)
            stats['vulnerabilities_by_severity'] = {row['severity']: row['count'] for row in cursor.fetchall()}

            cursor.execute("SELECT SUM(xp_earned) FROM activity_log")
            result = cursor.fetchone()[0]
            stats['total_xp_from_activities'] = result if result else 0

            return stats

    def clear_all_data(self, keep_activity_log: bool = False) -> Dict:
        """
        Clear all discovered data (hosts, services, credentials, vulnerabilities).

        Args:
            keep_activity_log: If True, preserves activity log history

        Returns:
            Dictionary with counts of deleted records
        """
        with self._get_connection() as conn:
            cursor = conn.cursor()

            # Get counts before deletion
            stats = {}
            cursor.execute("SELECT COUNT(*) FROM hosts")
            stats['hosts_deleted'] = cursor.fetchone()[0]

            cursor.execute("SELECT COUNT(*) FROM services")
            stats['services_deleted'] = cursor.fetchone()[0]

            cursor.execute("SELECT COUNT(*) FROM credentials")
            stats['credentials_deleted'] = cursor.fetchone()[0]

            cursor.execute("SELECT COUNT(*) FROM vulnerabilities")
            stats['vulnerabilities_deleted'] = cursor.fetchone()[0]

            cursor.execute("SELECT COUNT(*) FROM activity_log")
            stats['activities_deleted'] = 0 if keep_activity_log else cursor.fetchone()[0]

            # Delete in correct order (respecting foreign keys)
            cursor.execute("DELETE FROM vulnerabilities")
            cursor.execute("DELETE FROM credentials")
            cursor.execute("DELETE FROM services")
            cursor.execute("DELETE FROM hosts")

            if not keep_activity_log:
                cursor.execute("DELETE FROM activity_log")

            print(f"[DATABASE] Cleared {stats['hosts_deleted']} hosts, {stats['services_deleted']} services, "
                  f"{stats['credentials_deleted']} credentials, {stats['vulnerabilities_deleted']} vulnerabilities")

            return stats

    def clear_hosts(self) -> int:
        """Clear all hosts and their associated data."""
        with self._get_connection() as conn:
            cursor = conn.cursor()
            cursor.execute("SELECT COUNT(*) FROM hosts")
            count = cursor.fetchone()[0]

            # CASCADE should handle related records
            cursor.execute("DELETE FROM hosts")

            print(f"[DATABASE] Cleared {count} hosts")
            return count

    def clear_credentials(self) -> int:
        """Clear all captured credentials."""
        with self._get_connection() as conn:
            cursor = conn.cursor()
            cursor.execute("SELECT COUNT(*) FROM credentials")
            count = cursor.fetchone()[0]

            cursor.execute("DELETE FROM credentials")

            print(f"[DATABASE] Cleared {count} credentials")
            return count

    def clear_vulnerabilities(self) -> int:
        """Clear all discovered vulnerabilities."""
        with self._get_connection() as conn:
            cursor = conn.cursor()
            cursor.execute("SELECT COUNT(*) FROM vulnerabilities")
            count = cursor.fetchone()[0]

            cursor.execute("DELETE FROM vulnerabilities")

            print(f"[DATABASE] Cleared {count} vulnerabilities")
            return count

    def clear_activity_log(self, older_than_days: int = None) -> int:
        """
        Clear activity log entries.

        Args:
            older_than_days: If specified, only delete entries older than this many days
        """
        with self._get_connection() as conn:
            cursor = conn.cursor()

            if older_than_days:
                from datetime import datetime, timedelta
                cutoff_date = (datetime.now() - timedelta(days=older_than_days)).isoformat()
                cursor.execute("SELECT COUNT(*) FROM activity_log WHERE timestamp < ?", (cutoff_date,))
                count = cursor.fetchone()[0]
                cursor.execute("DELETE FROM activity_log WHERE timestamp < ?", (cutoff_date,))
                print(f"[DATABASE] Cleared {count} activity log entries older than {older_than_days} days")
            else:
                cursor.execute("SELECT COUNT(*) FROM activity_log")
                count = cursor.fetchone()[0]
                cursor.execute("DELETE FROM activity_log")
                print(f"[DATABASE] Cleared {count} activity log entries")

            return count

    def backup_database(self, backup_path: Path) -> bool:
        """
        Create a backup of the database.

        Args:
            backup_path: Path where backup should be saved

        Returns:
            True if successful, False otherwise
        """
        import shutil
        try:
            shutil.copy2(self.db_path, backup_path)
            print(f"[DATABASE] Backup created: {backup_path}")
            return True
        except Exception as e:
            print(f"[DATABASE] Backup failed: {e}")
            return False

    def vacuum_database(self) -> bool:
        """Optimize database by vacuuming (reclaiming space)."""
        try:
            with self._get_connection() as conn:
                cursor = conn.cursor()
                cursor.execute("VACUUM")
                print("[DATABASE] Database vacuumed and optimized")
                return True
        except Exception as e:
            print(f"[DATABASE] Vacuum failed: {e}")
            return False

    def get_database_size(self) -> Dict:
        """Get database file size information."""
        import os
        try:
            size_bytes = os.path.getsize(self.db_path)
            size_mb = size_bytes / (1024 * 1024)

            return {
                'size_bytes': size_bytes,
                'size_mb': round(size_mb, 2),
                'size_human': f"{size_mb:.2f} MB" if size_mb >= 1 else f"{size_bytes / 1024:.2f} KB",
                'path': str(self.db_path)
            }
        except Exception as e:
            return {'error': str(e)}
