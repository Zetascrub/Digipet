# modules/netinfo.py
import subprocess
import socket

def get_ip_address(interface="wlan0"):
    try:
        output = subprocess.check_output(["ip", "addr", "show", interface], text=True)
        for line in output.split("\n"):
            line = line.strip()
            if line.startswith("inet "):
                return line.split()[1].split("/")[0]
        return "No IP"
    except subprocess.CalledProcessError:
        return f"{interface} missing"

def get_gateway():
    try:
        output = subprocess.check_output(["ip", "route"], text=True)
        for line in output.splitlines():
            if line.startswith("default"):
                return line.split()[2]
        return "No gateway"
    except subprocess.CalledProcessError:
        return "Error"

def get_hostname():
    return socket.gethostname()

def get_all_interfaces_with_ips():
    interfaces_with_ips = []
    try:
        output = subprocess.check_output(["ip", "-brief", "addr"], text=True)
        lines = output.strip().splitlines()
        for line in lines:
            parts = line.split()
            if len(parts) >= 3:
                iface = parts[0]
                for part in parts[2:]:
                    if part.startswith("inet"):
                        ip = part.split("/")[0].replace("inet", "").strip()
                        interfaces_with_ips.append((iface, ip))
        return interfaces_with_ips
    except subprocess.CalledProcessError:
        return [("error", "Could not list interfaces")]
