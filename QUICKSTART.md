# Digipet Quick Start Guide

## 🚀 Getting Started in 5 Minutes

### Step 1: Install (Choose One)

**Option A: One-Line Install (Easiest)**
```bash
curl -sSL https://raw.githubusercontent.com/YOUR_USERNAME/digipet/main/install.sh | bash
```

**Option B: Manual Install**
```bash
git clone https://github.com/YOUR_USERNAME/digipet.git
cd digipet
pip3 install -r requirements.txt
```

### Step 2: Run Digipet

```bash
# RECOMMENDED: PREP MODE (no auto-attack, safe for configuration)
python3 digipet_daemon.py --prep

# Or use the convenience script
./start-prep.sh

# With password protection
python3 digipet_daemon.py --prep --web-password mySecretPassword

# Full auto-start (⚠️ begins attacking immediately!)
python3 digipet_daemon.py --auto-start
```

### Step 3: Access Web Portal

Open your browser to:
```
http://localhost:8080
```

**Login** (if password is set):
- Username: `admin`
- Password: (your password)

## 🎮 What You'll See

### Web Dashboard Features

1. **Evolution Card**: Shows your Digipet's current evolution stage and animated sprite
   - Starts as **Hatchling** (Level 0)
   - Displays current XP and progress to next evolution
   - Shows live sprite animation
2. **Orchestrator Controls**: Start/Stop/Pause autonomous pentesting
3. **Statistics**: Real-time count of discovered hosts, services, credentials, vulnerabilities
4. **Profile Selector**: Switch between Aggressive, Normal, Stealth, or Passive modes
5. **Activity Log**: Live feed of all discoveries and actions
6. **Hosts Table**: All discovered devices on the network

### Evolution Progression

Your Digipet starts as **Hatchling** and evolves based on XP:

- **Hatchling** (Level 0) → 100 XP needed
- **Learner** (Level 1) → 500 XP needed
- **Scout** (Level 2) → 2000 XP needed
- **Hunter** (Level 3) → 5000 XP needed
- **Specialist** (Level 4) → 10000 XP needed
- **Master** (Level 5) - MAX LEVEL!

### Earning XP

- Discover a host: **+10 XP**
- Enumerate a service: **+5 XP**
- Capture credentials: **+50 XP**
- Find vulnerability: **+25 XP**

## 📝 First Run Checklist

### Basic Testing (Safe)

1. Click **"Start"** on the orchestrator
2. Select **"Passive"** or **"Normal"** profile
3. Watch the activity log for discoveries
4. Check the "Hosts" table for discovered devices
5. Monitor your Digipet's XP growth

### Advanced Testing (Requires Tools)

For full functionality, install:

```bash
# Network scanning
sudo apt install nmap

# Credential harvesting (requires root)
sudo apt install responder
# OR
git clone https://github.com/lgandx/Responder
```

Then run with sudo for credential harvesting:
```bash
sudo python3 digipet_daemon.py --auto-start
```

## 🎛️ Profiles Explained

### Passive
- **Best for**: Continuous monitoring, avoiding detection
- **Speed**: Very slow
- **Risk**: Minimal (no active scanning)
- **Modules**: Credential harvesting only

### Stealth
- **Best for**: Real pentests where stealth matters
- **Speed**: Slow (5-15 second delays)
- **Risk**: Low
- **Modules**: Recon + Credential harvesting

### Normal (Recommended)
- **Best for**: Lab environments, CTFs
- **Speed**: Balanced (1-3 second delays)
- **Risk**: Medium
- **Modules**: All enabled

### Aggressive
- **Best for**: Quick scans in controlled environments
- **Speed**: Fast (0.1-0.5 second delays)
- **Risk**: High (easily detected)
- **Modules**: All enabled with maximum speed

## 🖥️ Command Line Options

```bash
# Default: Web portal + Display mock
python3 digipet_daemon.py

# Auto-start attacking
python3 digipet_daemon.py --auto-start

# Custom data directory
python3 digipet_daemon.py --data-dir /opt/digipet-data

# Different web port
python3 digipet_daemon.py --web-port 9000

# Headless (no web interface)
python3 digipet_daemon.py --no-web

# No display (for servers)
python3 digipet_daemon.py --no-display
```

## 📊 Understanding the Data

### Where is data stored?

By default in `~/.digipet/`:
- `digipet.db` - SQLite database with all discoveries
- `evolution.json` - Your Digipet's evolution progress
- `config.json` - Configuration settings

### Viewing data directly

```bash
# Check evolution status
cat ~/.digipet/evolution.json

# Query database
sqlite3 ~/.digipet/digipet.db "SELECT * FROM hosts;"
sqlite3 ~/.digipet/digipet.db "SELECT * FROM credentials;"
```

## 🔧 Troubleshooting

### Port 8080 already in use
```bash
python3 digipet_daemon.py --web-port 9000
```

### Can't discover hosts
- Make sure you're on a network with other devices
- Try running with sudo for better network access
- Check firewall isn't blocking ARP

### Responder won't start
- Needs to run as root: `sudo python3 digipet_daemon.py`
- Check interface name in config (default: wlan0)
- Install responder if missing

### No evolution happening
- Make sure orchestrator is started (click "Start" button)
- Check activity log for discoveries
- XP is only earned from NEW discoveries

## 🎯 Testing Scenarios

### Home Lab Test
```bash
# Safe passive monitoring
python3 digipet_daemon.py --auto-start
# Set profile to "Normal" in web portal
# Watch it discover your home devices
```

### CTF/HackTheBox
```bash
# Aggressive scanning for speed
python3 digipet_daemon.py --auto-start
# Set profile to "Aggressive"
# Rapid host/vuln discovery
```

### Enterprise Pentest
```bash
# Stealthy, authorized testing
sudo python3 digipet_daemon.py --auto-start
# Set profile to "Stealth"
# Enable credential harvesting
# Long-term passive collection
```

## 📱 API Quick Reference

### Check Status
```bash
curl http://localhost:8080/api/status
```

### Start Orchestrator
```bash
curl -X POST http://localhost:8080/api/orchestrator/start
```

### Change Profile
```bash
curl -X POST http://localhost:8080/api/config/profile \
  -H "Content-Type: application/json" \
  -d '{"profile":"stealth"}'
```

### Get Evolution Status
```bash
curl http://localhost:8080/api/evolution
```

### Manual Recon Run
```bash
curl -X POST http://localhost:8080/api/orchestrator/recon
```

## 🎨 Customization Ideas

### Add Your Own Digimon Line

Edit `modules/evolution.py` and create a custom evolution config in `digipet_daemon.py`.

### Add Custom Vulnerability Signatures

Edit `modules/attacks/vulnerability_scanner.py` to add service fingerprints and CVEs you care about.

### Create Custom Stealth Profiles

Edit `modules/config.py` to add new timing profiles.

## ⚠️ Important Notes

1. **Authorization Required**: Only use on networks you own or have permission to test
2. **Root Access**: Some features (Responder, raw sockets) require sudo
3. **Network Impact**: Even "Stealth" mode generates traffic
4. **Data Sensitivity**: Credentials and vulnerabilities are stored unencrypted locally
5. **Legal Compliance**: Ensure compliance with local laws and regulations

## 🎉 You're Ready!

Your Digipet is now running and evolving as it discovers your network. Watch it grow from a tiny Hatchling to the mighty Master!

Questions? Check the main README.md for detailed documentation.

Happy hunting! 🦖
