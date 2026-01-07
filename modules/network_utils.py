"""
Network utilities - interface detection, network discovery helpers.
"""

import subprocess
import re
import ipaddress
from typing import List, Dict, Optional


def get_network_interfaces() -> List[Dict]:
    """
    Get all available network interfaces with their details.

    Returns:
        List of dicts with interface information
    """
    interfaces = []

    try:
        # Use 'ip addr show' to get interface details
        result = subprocess.run(
            ['ip', '-o', '-4', 'addr', 'show'],
            capture_output=True,
            text=True,
            timeout=5
        )

        for line in result.stdout.splitlines():
            # Parse: 2: eth0    inet 192.168.1.10/24 brd 192.168.1.255 scope global eth0
            match = re.search(
                r'(\d+):\s+(\S+)\s+inet\s+(\d+\.\d+\.\d+\.\d+)/(\d+)',
                line
            )

            if match:
                index, name, ip, prefix = match.groups()

                # Calculate network CIDR
                network = ipaddress.ip_network(f"{ip}/{prefix}", strict=False)

                # Get interface status
                status = 'up' if 'state UP' in line or 'scope global' in line else 'down'

                # Determine interface type
                if name.startswith('lo'):
                    iface_type = 'loopback'
                elif name.startswith(('eth', 'en')):
                    iface_type = 'ethernet'
                elif name.startswith(('wlan', 'wl')):
                    iface_type = 'wifi'
                elif name.startswith(('tun', 'tap', 'tailscale', 'utun')):
                    iface_type = 'vpn'
                elif name.startswith(('docker', 'br-')):
                    iface_type = 'docker'
                elif name.startswith('veth'):
                    iface_type = 'virtual'
                else:
                    iface_type = 'other'

                interfaces.append({
                    'index': int(index),
                    'name': name,
                    'ip': ip,
                    'prefix': int(prefix),
                    'network': str(network),
                    'status': status,
                    'type': iface_type,
                    'is_loopback': iface_type == 'loopback',
                    'is_vpn': iface_type == 'vpn'
                })

    except Exception as e:
        print(f"[NETWORK] Error detecting interfaces: {e}")

    return interfaces


def get_primary_interface() -> Optional[Dict]:
    """
    Get the primary (default route) network interface.

    Returns:
        Dict with interface info, or None if not found
    """
    try:
        # Get default route
        result = subprocess.run(
            ['ip', 'route', 'show', 'default'],
            capture_output=True,
            text=True,
            timeout=5
        )

        # Parse: default via 192.168.1.1 dev eth0 proto dhcp metric 100
        match = re.search(r'dev\s+(\S+)', result.stdout)
        if match:
            primary_iface_name = match.group(1)

            # Get full details for this interface
            interfaces = get_network_interfaces()
            for iface in interfaces:
                if iface['name'] == primary_iface_name:
                    return iface

    except Exception as e:
        print(f"[NETWORK] Error detecting primary interface: {e}")

    return None


def get_scannable_networks() -> List[Dict]:
    """
    Get networks suitable for scanning (non-loopback, active).

    Returns:
        List of network dicts suitable for scanning
    """
    interfaces = get_network_interfaces()

    scannable = []
    for iface in interfaces:
        if iface['is_loopback']:
            continue  # Skip loopback

        if iface['status'] != 'up':
            continue  # Skip inactive

        scannable.append({
            'name': iface['name'],
            'network': iface['network'],
            'ip': iface['ip'],
            'type': iface['type'],
            'is_vpn': iface['is_vpn'],
            'recommended': iface['type'] in ['ethernet', 'wifi'],  # Physical networks recommended
            'description': _get_interface_description(iface)
        })

    return scannable


def _get_interface_description(iface: Dict) -> str:
    """Generate a human-readable description of the interface."""
    type_desc = {
        'ethernet': 'Wired Ethernet',
        'wifi': 'WiFi',
        'vpn': 'VPN Tunnel',
        'docker': 'Docker Network',
        'virtual': 'Virtual Interface',
        'other': 'Other Interface'
    }

    desc = type_desc.get(iface['type'], iface['type'].title())

    if iface['is_vpn']:
        desc += ' (Layer 3 - requires active scanning)'

    return f"{desc} - {iface['ip']}"


def suggest_scan_target() -> Optional[str]:
    """
    Suggest the best network target for scanning.

    Returns:
        Network CIDR string (e.g., "192.168.1.0/24") or None
    """
    # Get primary interface
    primary = get_primary_interface()

    if primary and not primary['is_loopback']:
        return primary['network']

    # Fallback: first non-loopback interface
    interfaces = get_network_interfaces()
    for iface in interfaces:
        if not iface['is_loopback'] and iface['status'] == 'up':
            return iface['network']

    return None


def validate_network_cidr(cidr: str) -> bool:
    """Validate a network CIDR notation."""
    try:
        ipaddress.ip_network(cidr, strict=False)
        return True
    except:
        return False


def is_ip_in_network(ip: str, network: str) -> bool:
    """Check if an IP address is in a given network."""
    try:
        ip_obj = ipaddress.ip_address(ip)
        network_obj = ipaddress.ip_network(network, strict=False)
        return ip_obj in network_obj
    except:
        return False


def get_network_info(cidr: str) -> Optional[Dict]:
    """Get detailed information about a network CIDR."""
    try:
        network = ipaddress.ip_network(cidr, strict=False)

        return {
            'network': str(network),
            'network_address': str(network.network_address),
            'broadcast': str(network.broadcast_address),
            'netmask': str(network.netmask),
            'prefix': network.prefixlen,
            'num_hosts': network.num_addresses - 2,  # Exclude network and broadcast
            'first_host': str(network.network_address + 1),
            'last_host': str(network.broadcast_address - 1),
            'is_private': network.is_private,
            'is_multicast': network.is_multicast
        }
    except Exception as e:
        return None
