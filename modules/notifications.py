"""
Notification system - alerts for critical findings.
"""

import json
import smtplib
from email.mime.text import MIMEText
from email.mime.multipart import MIMEMultipart
from typing import Dict, Optional
from datetime import datetime
import subprocess


class NotificationManager:
    """Manages notifications via multiple channels."""

    def __init__(self, config):
        self.config = config
        self.notification_config = self._load_notification_config()

    def _load_notification_config(self) -> Dict:
        """Load notification configuration."""
        # Default config
        return {
            'enabled': False,
            'webhook_url': None,
            'email_enabled': False,
            'email_smtp_server': None,
            'email_smtp_port': 587,
            'email_username': None,
            'email_password': None,
            'email_from': None,
            'email_to': [],
            'slack_webhook': None,
            'discord_webhook': None,
            'notify_on': {
                'critical_vuln': True,
                'credential_captured': True,
                'evolution': True,
                'high_value_target': True
            }
        }

    def configure(self, **kwargs):
        """Update notification configuration."""
        self.notification_config.update(kwargs)

    def notify_critical_vulnerability(self, vuln: Dict):
        """Send notification for critical vulnerability."""
        if not self.notification_config['notify_on']['critical_vuln']:
            return

        title = "🚨 Critical Vulnerability Discovered"
        message = (
            f"**CVE:** {vuln.get('cve_id', 'N/A')}\n"
            f"**Title:** {vuln.get('title')}\n"
            f"**Host:** {vuln.get('ip_address', 'N/A')}\n"
            f"**Port:** {vuln.get('port', 'N/A')}\n"
            f"**CVSS:** {vuln.get('cvss_score', 'N/A')}\n"
            f"**Severity:** {vuln.get('severity')}\n\n"
            f"**Description:** {vuln.get('description', 'N/A')}"
        )

        self._send_notification(title, message, level='critical')

    def notify_credential_captured(self, credential: Dict):
        """Send notification for captured credential."""
        if not self.notification_config['notify_on']['credential_captured']:
            return

        title = "🔑 Credential Captured"
        message = (
            f"**Username:** {credential.get('username')}\n"
            f"**Type:** {credential.get('hash_type', 'plaintext')}\n"
            f"**Service:** {credential.get('service', 'Unknown')}\n"
            f"**Host:** {credential.get('ip_address', 'N/A')}\n"
            f"**Captured:** {credential.get('captured_at')}"
        )

        self._send_notification(title, message, level='warning')

    def notify_evolution(self, old_stage: str, new_stage: str, level: int):
        """Send notification for Digipet evolution."""
        if not self.notification_config['notify_on']['evolution']:
            return

        title = f"🦖 Digipet Evolved!"
        message = (
            f"Your Digipet has evolved from **{old_stage}** to **{new_stage}**!\n\n"
            f"**New Level:** {level}\n"
            f"**Time:** {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}"
        )

        self._send_notification(title, message, level='info')

    def notify_high_value_target(self, host: Dict, reason: str):
        """Send notification for high-value target discovery."""
        if not self.notification_config['notify_on']['high_value_target']:
            return

        title = "🎯 High-Value Target Discovered"
        message = (
            f"**Host:** {host.get('ip_address')}\n"
            f"**Hostname:** {host.get('hostname', 'N/A')}\n"
            f"**Reason:** {reason}\n"
            f"**Discovered:** {host.get('first_seen')}"
        )

        self._send_notification(title, message, level='warning')

    def _send_notification(self, title: str, message: str, level: str = 'info'):
        """Send notification via all configured channels."""
        if not self.notification_config['enabled']:
            return

        # Webhook (generic)
        if self.notification_config.get('webhook_url'):
            self._send_webhook(title, message, level)

        # Email
        if self.notification_config.get('email_enabled'):
            self._send_email(title, message, level)

        # Slack
        if self.notification_config.get('slack_webhook'):
            self._send_slack(title, message, level)

        # Discord
        if self.notification_config.get('discord_webhook'):
            self._send_discord(title, message, level)

    def _send_webhook(self, title: str, message: str, level: str):
        """Send generic webhook notification."""
        import requests

        webhook_url = self.notification_config['webhook_url']

        payload = {
            'title': title,
            'message': message,
            'level': level,
            'timestamp': datetime.now().isoformat(),
            'source': 'Digipet'
        }

        try:
            response = requests.post(webhook_url, json=payload, timeout=10)
            response.raise_for_status()
        except Exception as e:
            print(f"[!] Webhook notification failed: {e}")

    def _send_email(self, title: str, message: str, level: str):
        """Send email notification."""
        config = self.notification_config

        if not all([config.get('email_smtp_server'), config.get('email_from'), config.get('email_to')]):
            return

        try:
            msg = MIMEMultipart()
            msg['From'] = config['email_from']
            msg['To'] = ', '.join(config['email_to'])
            msg['Subject'] = f"[Digipet] {title}"

            # Create HTML email
            html_message = f"""
            <html>
            <body style="font-family: Arial, sans-serif;">
                <div style="background: #2c3e50; color: white; padding: 20px;">
                    <h2>{title}</h2>
                </div>
                <div style="padding: 20px; background: #ecf0f1;">
                    <pre style="background: white; padding: 15px; border-left: 4px solid #3498db;">
{message}
                    </pre>
                </div>
                <div style="padding: 10px; text-align: center; color: #7f8c8d; font-size: 0.9em;">
                    <p>Sent by Digipet Autonomous Pentesting Platform</p>
                    <p>{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}</p>
                </div>
            </body>
            </html>
            """

            msg.attach(MIMEText(html_message, 'html'))

            # Send email
            with smtplib.SMTP(config['email_smtp_server'], config['email_smtp_port']) as server:
                if config.get('email_username') and config.get('email_password'):
                    server.starttls()
                    server.login(config['email_username'], config['email_password'])
                server.send_message(msg)

        except Exception as e:
            print(f"[!] Email notification failed: {e}")

    def _send_slack(self, title: str, message: str, level: str):
        """Send Slack webhook notification."""
        import requests

        webhook_url = self.notification_config['slack_webhook']

        # Color based on level
        color_map = {
            'critical': '#e74c3c',
            'warning': '#f39c12',
            'info': '#3498db'
        }

        payload = {
            'username': 'Digipet',
            'icon_emoji': ':dragon:',
            'attachments': [{
                'color': color_map.get(level, '#95a5a6'),
                'title': title,
                'text': message,
                'footer': 'Digipet Pentesting Platform',
                'ts': int(datetime.now().timestamp())
            }]
        }

        try:
            response = requests.post(webhook_url, json=payload, timeout=10)
            response.raise_for_status()
        except Exception as e:
            print(f"[!] Slack notification failed: {e}")

    def _send_discord(self, title: str, message: str, level: str):
        """Send Discord webhook notification."""
        import requests

        webhook_url = self.notification_config['discord_webhook']

        # Color based on level (Discord uses decimal color codes)
        color_map = {
            'critical': 15158332,  # Red
            'warning': 15915330,   # Orange
            'info': 3447003        # Blue
        }

        payload = {
            'username': 'Digipet',
            'avatar_url': 'https://raw.githubusercontent.com/YOUR_USERNAME/digipet/main/assets/Neko.png',
            'embeds': [{
                'title': title,
                'description': message,
                'color': color_map.get(level, 10070709),
                'footer': {
                    'text': 'Digipet Pentesting Platform'
                },
                'timestamp': datetime.now().isoformat()
            }]
        }

        try:
            response = requests.post(webhook_url, json=payload, timeout=10)
            response.raise_for_status()
        except Exception as e:
            print(f"[!] Discord notification failed: {e}")

    def test_notifications(self):
        """Send test notification to verify configuration."""
        title = "🧪 Test Notification"
        message = (
            "This is a test notification from Digipet.\n"
            "If you're seeing this, notifications are working correctly!"
        )

        self._send_notification(title, message, level='info')
        print("[+] Test notification sent")
