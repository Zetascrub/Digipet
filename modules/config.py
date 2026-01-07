"""
Configuration system for Digipet - manages attack profiles and settings.
"""

import json
from pathlib import Path
from typing import Dict, Optional
from dataclasses import dataclass, asdict, field


@dataclass
class StealthProfile:
    """Stealth profile configuration."""
    name: str
    description: str
    scan_delay_min: float  # Minimum seconds between scans
    scan_delay_max: float  # Maximum seconds between scans
    max_parallel_scans: int  # Number of concurrent scans
    randomize_timing: bool  # Add random jitter to delays
    port_scan_rate: int  # Packets per second for port scanning
    enabled_modules: list = field(default_factory=list)


@dataclass
class AttackConfig:
    """Attack module configuration."""
    network_recon_enabled: bool = True
    credential_harvesting_enabled: bool = True
    vulnerability_scanning_enabled: bool = True

    # Network reconnaissance settings
    nmap_enabled: bool = True
    nmap_args: str = "-sV -sC"  # Default nmap arguments
    target_network: str = "auto"  # "auto" or specific CIDR

    # Credential harvesting settings
    responder_enabled: bool = True
    responder_interface: str = "wlan0"
    capture_ntlm: bool = True
    capture_kerberos: bool = True

    # Vulnerability scanning settings
    vuln_scan_enabled: bool = True
    cve_database_path: str = "/opt/digipet/cve-db"


@dataclass
class DisplayConfig:
    """E-ink display configuration."""
    enabled: bool = True
    update_interval: int = 4  # Seconds between display updates
    show_attack_progress: bool = True
    show_evolution_status: bool = True
    sprite_animation: bool = True


@dataclass
class WebPortalConfig:
    """Web portal configuration."""
    enabled: bool = True
    host: str = "0.0.0.0"
    port: int = 8080
    auth_enabled: bool = False  # Disabled by default, enable with --web-password
    username: str = "admin"
    password_hash: str = ""  # bcrypt hash


class DigipetConfig:
    """Main configuration manager for Digipet."""

    STEALTH_PROFILES = {
        'aggressive': StealthProfile(
            name='Aggressive',
            description='Fast scans, maximum intel gathering speed',
            scan_delay_min=0.1,
            scan_delay_max=0.5,
            max_parallel_scans=10,
            randomize_timing=False,
            port_scan_rate=1000,
            enabled_modules=['network_recon', 'credential_harvesting', 'vulnerability_scanning']
        ),
        'normal': StealthProfile(
            name='Normal',
            description='Balanced speed and stealth',
            scan_delay_min=1.0,
            scan_delay_max=3.0,
            max_parallel_scans=5,
            randomize_timing=True,
            port_scan_rate=300,
            enabled_modules=['network_recon', 'credential_harvesting', 'vulnerability_scanning']
        ),
        'stealth': StealthProfile(
            name='Stealth',
            description='Slow, careful scans to avoid detection',
            scan_delay_min=5.0,
            scan_delay_max=15.0,
            max_parallel_scans=2,
            randomize_timing=True,
            port_scan_rate=50,
            enabled_modules=['network_recon', 'credential_harvesting']
        ),
        'passive': StealthProfile(
            name='Passive',
            description='Only passive reconnaissance, no active scanning',
            scan_delay_min=10.0,
            scan_delay_max=30.0,
            max_parallel_scans=1,
            randomize_timing=True,
            port_scan_rate=0,
            enabled_modules=['credential_harvesting']
        )
    }

    def __init__(self, config_file: Path):
        self.config_file = config_file
        self.current_profile_name = 'normal'
        self.attack_config = AttackConfig()
        self.display_config = DisplayConfig()
        self.web_portal_config = WebPortalConfig()
        self._load_config()

    def _load_config(self):
        """Load configuration from file or create defaults."""
        if self.config_file.exists():
            with open(self.config_file, 'r') as f:
                data = json.load(f)

                self.current_profile_name = data.get('current_profile', 'normal')

                if 'attack_config' in data:
                    self.attack_config = AttackConfig(**data['attack_config'])

                if 'display_config' in data:
                    self.display_config = DisplayConfig(**data['display_config'])

                if 'web_portal_config' in data:
                    self.web_portal_config = WebPortalConfig(**data['web_portal_config'])
        else:
            self.save_config()

    def save_config(self):
        """Persist configuration to disk."""
        self.config_file.parent.mkdir(parents=True, exist_ok=True)

        data = {
            'current_profile': self.current_profile_name,
            'attack_config': asdict(self.attack_config),
            'display_config': asdict(self.display_config),
            'web_portal_config': asdict(self.web_portal_config)
        }

        with open(self.config_file, 'w') as f:
            json.dump(data, f, indent=2)

    def get_current_profile(self) -> StealthProfile:
        """Get the currently active stealth profile."""
        return self.STEALTH_PROFILES.get(self.current_profile_name, self.STEALTH_PROFILES['normal'])

    def set_profile(self, profile_name: str) -> bool:
        """Set the active stealth profile."""
        if profile_name in self.STEALTH_PROFILES:
            self.current_profile_name = profile_name
            self.save_config()
            return True
        return False

    def get_all_profiles(self) -> Dict[str, Dict]:
        """Get all available stealth profiles."""
        return {name: asdict(profile) for name, profile in self.STEALTH_PROFILES.items()}

    def update_attack_config(self, **kwargs):
        """Update attack configuration."""
        for key, value in kwargs.items():
            if hasattr(self.attack_config, key):
                setattr(self.attack_config, key, value)
        self.save_config()

    def update_display_config(self, **kwargs):
        """Update display configuration."""
        for key, value in kwargs.items():
            if hasattr(self.display_config, key):
                setattr(self.display_config, key, value)
        self.save_config()

    def update_web_portal_config(self, **kwargs):
        """Update web portal configuration."""
        for key, value in kwargs.items():
            if hasattr(self.web_portal_config, key):
                setattr(self.web_portal_config, key, value)
        self.save_config()

    def get_full_config(self) -> Dict:
        """Get complete configuration for API/display."""
        return {
            'current_profile': self.current_profile_name,
            'current_profile_settings': asdict(self.get_current_profile()),
            'available_profiles': self.get_all_profiles(),
            'attack_config': asdict(self.attack_config),
            'display_config': asdict(self.display_config),
            'web_portal_config': {
                **asdict(self.web_portal_config),
                'password_hash': '***'  # Don't expose hash
            }
        }

    def is_module_enabled(self, module_name: str) -> bool:
        """Check if a specific attack module is enabled in current profile."""
        profile = self.get_current_profile()
        return module_name in profile.enabled_modules
