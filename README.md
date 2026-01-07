# 🦖 Digipet - Autonomous Penetration Testing Platform

Digipet is an autonomous penetration testing tool inspired by Bjorn. It features a Tamagotchi-like evolution system where your "digital pet" grows and evolves as it discovers hosts, services, credentials, and vulnerabilities.

**🆕 NEW in v2.0:** Professional reporting (PDF/HTML/JSON/CSV), real-time notifications (Email/Slack/Discord), and powerful CLI tool! See [NEW_FEATURES.md](NEW_FEATURES.md) for complete details.

## Features

### 🎮 Evolution System
- **Progressive growth**: Starts as Hatchling and evolves through Learner → Scout → Hunter → Specialist → Master
- **XP-based evolution**: Gain experience by discovering hosts, enumerating services, capturing credentials, and finding vulnerabilities
- **Configurable evolution lines**: Easy to customize with different themes
- **Persistent state**: Evolution progress saved between sessions

### 🔍 Autonomous Pentesting Capabilities
- **Network Reconnaissance**:
  - Passive ARP scanning
  - Active nmap scanning with service detection
  - Quick Python-based port scanning
  - Hostname resolution

- **Credential Harvesting**:
  - Responder integration for NTLM/LLMNR poisoning
  - Automatic credential capture and storage
  - Hash extraction and management

- **Vulnerability Scanning**:
  - Service fingerprinting
  - CVE matching against known vulnerabilities
  - Misconfiguration detection
  - Severity-based prioritization

### 🎛️ Stealth Profiles
- **Aggressive**: Fast scans, maximum intel gathering (1000 pps)
- **Normal**: Balanced speed and stealth (300 pps)
- **Stealth**: Slow, careful scans to avoid detection (50 pps)
- **Passive**: Only passive reconnaissance, no active scanning

### 🌐 Web Portal
- Real-time monitoring dashboard
- View discovered hosts, services, credentials, vulnerabilities
- Control orchestrator (start/stop/pause)
- Change stealth profiles
- Manual reconnaissance and scanning triggers
- Activity log viewer

### 📺 E-ink Display Interface
- Real-time evolution status display
- Network reconnaissance view
- Attack progress monitoring
- System statistics
- Animated sprite support

### 📊 Professional Reporting (NEW!)
- **Multiple Formats**: Markdown, HTML, JSON, CSV
- **Comprehensive Data**: Executive summary, findings, recommendations
- **Automated Generation**: Via CLI or web portal
- **Export & Share**: Perfect for client deliverables

### 🔔 Real-Time Notifications (NEW!)
- **Multi-Channel**: Email, Slack, Discord, Webhooks
- **Smart Alerts**: Critical vulns, credentials, evolution, high-value targets
- **Customizable**: Configure what alerts you want
- **Professional Formatting**: HTML emails, rich embeds

### 💻 Powerful CLI Tool (NEW!)
- **Quick Queries**: Instant access to all data without web browser
- **Beautiful Tables**: ASCII formatted output
- **Fast Searches**: Find hosts, services, vulns instantly
- **Scriptable**: Perfect for automation and pipelines

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    Digipet Daemon                        │
├─────────────────────────────────────────────────────────┤
│                                                          │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  │
│  │   Evolution   │  │  Database    │  │    Config     │  │
│  │    Engine     │  │  (SQLite)    │  │   Manager     │  │
│  └──────────────┘  └──────────────┘  └──────────────┘  │
│                                                          │
│  ┌──────────────────────────────────────────────────┐  │
│  │            Attack Orchestrator                    │  │
│  │  (State Machine: Idle→Recon→Scan→Harvest→...)   │  │
│  └──────────────────────────────────────────────────┘  │
│                                                          │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  │
│  │   Network     │  │  Credential  │  │Vulnerability │  │
│  │     Recon     │  │  Harvester   │  │   Scanner    │  │
│  └──────────────┘  └──────────────┘  └──────────────┘  │
│                                                          │
├─────────────────────────────────────────────────────────┤
│                                                          │
│  ┌──────────────┐              ┌──────────────┐        │
│  │  Web Portal   │              │  E-ink       │        │
│  │  (Flask)      │              │  Display     │        │
│  └──────────────┘              └──────────────┘        │
│                                                          │
└─────────────────────────────────────────────────────────┘
```

## Installation

### One-Line Install (Recommended)

```bash
curl -sSL https://raw.githubusercontent.com/YOUR_USERNAME/digipet/main/install.sh | bash
```

This will:
- Install system dependencies
- Clone the repository
- Install Python packages
- Optionally install nmap and Responder
- Set up convenience scripts
- Configure systemd service (optional)

### Manual Installation

#### Prerequisites

**System Packages:**
```bash
# Debian/Ubuntu/Raspberry Pi OS
sudo apt update
sudo apt install -y python3 python3-pip python3-dev git curl net-tools

# Optional: nmap for active scanning
sudo apt install -y nmap

# Optional: Responder for credential harvesting
git clone https://github.com/lgandx/Responder
cd Responder
sudo python3 setup.py install
```

#### Install Digipet

1. **Clone and setup**:
```bash
git clone https://github.com/YOUR_USERNAME/digipet.git
cd digipet
pip3 install -r requirements.txt
```

2. **Run the daemon**:
```bash
# PREP MODE (recommended first run - no auto-attack)
python3 digipet_daemon.py --prep

# Or use convenience script
./start-prep.sh

# Full run with web portal
python3 digipet_daemon.py

# Auto-start attacking immediately (⚠️ use with caution!)
python3 digipet_daemon.py --auto-start

# With password protection
python3 digipet_daemon.py --web-password YOUR_PASSWORD

# Custom options
python3 digipet_daemon.py --web-port 9000 --no-display
```

3. **Access the web portal**:
   - Open browser to `http://<device-ip>:8080`
   - Default: `http://localhost:8080`
   - If password is set (--web-password), you'll see a login page
   - Login: Username: `admin` / Password: (your password)

## Usage

### Web Portal

The web portal provides a comprehensive interface for:

- **Dashboard**: Real-time evolution status, orchestrator control, statistics
- **Hosts**: View all discovered hosts with details
- **Services**: Browse enumerated services
- **Credentials**: View captured credentials (hashes, plaintext)
- **Vulnerabilities**: Review found vulnerabilities by severity
- **Configuration**: Change stealth profiles, configure attack modules

### Command Line Options

```bash
usage: digipet_daemon.py [-h] [--data-dir DATA_DIR] [--no-web] [--no-display]
                         [--auto-start] [--prep] [--web-host WEB_HOST]
                         [--web-port WEB_PORT] [--web-password WEB_PASSWORD]

optional arguments:
  --data-dir DATA_DIR              Data directory (default: ~/.digipet)
  --no-web                         Disable web portal
  --no-display                     Disable e-ink display
  --auto-start                     Auto-start orchestrator (⚠️ begins attacking immediately)
  --prep                           PREP mode: services start but NO auto-attack (safe for config)
  --web-host WEB_HOST              Web portal host (default: 0.0.0.0)
  --web-port WEB_PORT              Web portal port (default: 8080)
  --web-password WEB_PASSWORD      Set web portal password
```

**Important**: `--prep` mode is recommended for first-time setup and configuration. It starts all services but does NOT begin autonomous attacks, allowing you to safely configure profiles and settings.

### API Endpoints

The web portal exposes a REST API:

#### Status & Evolution
- `GET /api/status` - Overall system status
- `GET /api/evolution` - Evolution details
- `POST /api/evolution/reset` - Reset to baby stage

#### Discovery Data
- `GET /api/hosts` - List discovered hosts
- `GET /api/hosts/<ip>` - Host details
- `GET /api/services` - List all services
- `GET /api/credentials` - List captured credentials
- `GET /api/vulnerabilities` - List vulnerabilities

#### Orchestrator Control
- `POST /api/orchestrator/start` - Start autonomous mode
- `POST /api/orchestrator/stop` - Stop autonomous mode
- `POST /api/orchestrator/pause` - Pause operations
- `POST /api/orchestrator/resume` - Resume operations
- `POST /api/orchestrator/recon` - Manual recon cycle
- `GET /api/orchestrator/status` - Orchestrator status

#### Configuration
- `GET /api/config` - Get full configuration
- `POST /api/config/profile` - Set stealth profile
- `POST /api/config/attack` - Update attack config
- `POST /api/config/display` - Update display config

## XP Rewards

| Event                  | XP Gained |
|------------------------|-----------|
| Host Discovered        | 10 XP     |
| Service Enumerated     | 5 XP      |
| Credential Captured    | 50 XP     |
| Vulnerability Found    | 25 XP     |
| Exploit Success        | 100 XP    |

## Evolution Stages

| Level | Name          | XP Required | Description                       |
|-------|---------------|-------------|-----------------------------------|
| 0     | Hatchling     | 0           | Just getting started              |
| 1     | Learner       | 100         | Learning the basics               |
| 2     | Scout         | 500         | Ready for reconnaissance          |
| 3     | Hunter        | 2000        | An experienced operator           |
| 4     | Specialist    | 5000        | Advanced capabilities             |
| 5     | Master        | 10000       | Peak performance                  |

## Data Storage

All data is stored in `~/.digipet/` by default:

- `digipet.db` - SQLite database (hosts, services, credentials, vulnerabilities)
- `evolution.json` - Evolution state
- `config.json` - Configuration settings

## Hardware Setup (Raspberry Pi Zero)

### E-ink Display

The project supports Waveshare 2.13" V4 e-ink displays (122x250px).

1. Connect display to SPI pins on Pi Zero
2. Enable SPI: `sudo raspi-config` → Interface Options → SPI → Enable
3. Set `USE_MOCK=0` environment variable to use hardware display

### WiFi Monitor Mode (for advanced attacks)

```bash
# Enable monitor mode on wlan0
sudo ip link set wlan0 down
sudo iw wlan0 set monitor control
sudo ip link set wlan0 up
```

## Customization

### Custom Evolution Line

Edit `modules/evolution.py` to create your own evolution line:

```python
CUSTOM_LINE = [
    EvolutionStage(
        name="YourDigimon",
        level=0,
        xp_required=0,
        sprite_file="yourdigimon.png",
        description="Your custom description"
    ),
    # ... more stages
]

config = EvolutionConfig(custom_line=CUSTOM_LINE)
```

### Adding Vulnerability Signatures

Edit `modules/attacks/vulnerability_scanner.py` to add CVE patterns:

```python
self.vuln_db = {
    'ServiceName X.Y.Z': {
        'cve': 'CVE-YYYY-NNNNN',
        'title': 'Vulnerability Title',
        'severity': 'CRITICAL',
        'cvss': 9.8,
        'description': 'Description here'
    }
}
```

## Security & Legal

⚠️ **IMPORTANT**: This tool is designed for **authorized penetration testing only**.

- Only use on networks you own or have explicit written permission to test
- Unauthorized use may be illegal in your jurisdiction
- Responder and credential harvesting can be detected by security systems
- Use stealth profiles appropriately for authorized engagements

## Troubleshooting

Having issues? See our comprehensive **[TROUBLESHOOTING.md](TROUBLESHOOTING.md)** guide which covers:

- 🔒 Authentication/401 errors
- 📡 No hosts discovered
- 🔍 Nmap not working
- 📧 Responder issues
- 🌐 Web portal problems
- 📊 Report generation
- 🔔 Notification setup
- 💾 Database errors
- And much more!

**Quick fixes:**

### Web Portal Shows 401 Errors
```bash
# Authentication is enabled but no password set
# Solution: Disable auth (it's off by default now)
# Or set a password:
python3 digipet_daemon.py --web-password yourPassword
```

### No Hosts Discovered
```bash
# Make sure orchestrator is started
# Click "Start" button in web portal
# Or use auto-start:
python3 digipet_daemon.py --auto-start
```

### Need Help?
1. Check [TROUBLESHOOTING.md](TROUBLESHOOTING.md)
2. See [QUICKSTART.md](QUICKSTART.md)
3. Review [NEW_FEATURES.md](NEW_FEATURES.md)
4. Open GitHub issue with details

## Project Structure

```
Digipet/
├── digipet_daemon.py          # Main entry point
├── main.py                     # Original display code (legacy)
├── main_integrated.py          # Integrated display with evolution
├── web_portal.py               # Flask web interface
├── requirements.txt            # Python dependencies
├── README.md                   # This file
│
├── modules/
│   ├── constants.py            # Configuration constants
│   ├── config.py               # Configuration management
│   ├── database.py             # SQLite database interface
│   ├── evolution.py            # Evolution engine
│   ├── layout.py               # Display layout rendering
│   ├── netinfo.py              # Network utilities
│   ├── orchestrator.py         # Attack orchestration
│   ├── sprites.py              # Sprite loading
│   └── attacks/
│       ├── network_recon.py    # Network reconnaissance
│       ├── credential_harvester.py  # Credential capture
│       └── vulnerability_scanner.py # Vuln scanning
│
├── web_templates/
│   └── dashboard.html          # Web portal frontend
│
├── assets/
│   └── Neko.png                # Sprite sheet (30 frames)
│
├── epd/
│   └── mock_epd2in13_V4.py     # Mock e-ink display
│
└── utils/
    └── timestamp.py            # Timestamp utilities
```

## Roadmap

- [ ] WiFi deauth attacks
- [ ] WPA handshake capture
- [ ] Multiple evolution lines (different Digimon)
- [ ] Sprite sheets for each evolution stage
- [ ] Export reports (PDF, JSON, Markdown)
- [ ] Integration with Metasploit
- [ ] Machine learning for attack prioritization
- [ ] Bluetooth LE enumeration
- [ ] Web application scanning

## Credits

- Inspired by [Bjorn](https://github.com/infinition/bjorn) - network pentesting tool
- Digimon franchise for evolution concept
- Tamagotchi for the virtual pet mechanics

## License

This project is for educational and authorized security testing purposes only.
