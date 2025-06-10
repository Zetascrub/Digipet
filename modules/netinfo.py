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


def discover_local_devices():
    """
    Parse the kernel's ARP table to quickly list local IP↔MAC entries.
    No root required, instantaneous.
    """
    devices = []
    try:
        output = subprocess.check_output(["ip", "neigh"], text=True)
        for line in output.splitlines():
            parts = line.split()
            # e.g. "192.168.8.1 dev eth0 lladdr aa:bb:cc:dd:ee:ff REACHABLE"
            if len(parts) >= 6 and parts[2] == "dev" and "lladdr" in parts:
                ip      = parts[0]
                iface   = parts[3]
                mac     = parts[parts.index("lladdr") + 1]
                devices.append({"iface": iface, "ip": ip, "mac": mac})
    except subprocess.CalledProcessError:
        # if something goes wrong, just return what we have
        pass

    return devices


def get_ping_latency(host):
     """
     Ping once (-c1) with 1s timeout, return round-trip time string or 'timeout'.
     """
     try:
         out = subprocess.check_output(
             ["ping", "-c", "1", "-W", "1", host],
             stderr=subprocess.DEVNULL,
             text=True
         )
         for line in out.splitlines():
             if "time=" in line:
                 # e.g. "64 bytes from 192.168.8.1: icmp_seq=1 ttl=64 time=2.34 ms"
                 return line.split("time=")[1].split()[0] + "ms"
     except subprocess.CalledProcessError:
         pass
     return "timeout"
