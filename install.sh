#!/bin/bash
#
# Digipet Installation Script
# One-line install: curl -sSL https://raw.githubusercontent.com/YOUR_USERNAME/digipet/main/install.sh | bash
#

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Banner
echo -e "${GREEN}"
cat << "EOF"
╔═══════════════════════════════════════╗
║                                       ║
║       🦖  D I G I P E T  🦖          ║
║                                       ║
║   Autonomous Pentesting Platform      ║
║              Installer                ║
║                                       ║
╚═══════════════════════════════════════╝
EOF
echo -e "${NC}"

# Check if running as root for optional components
if [ "$EUID" -eq 0 ]; then
    SUDO=""
    echo -e "${YELLOW}[!] Running as root${NC}"
else
    SUDO="sudo"
    echo -e "${BLUE}[*] Running as user, will use sudo for system packages${NC}"
fi

# Detect OS
if [ -f /etc/os-release ]; then
    . /etc/os-release
    OS=$ID
else
    echo -e "${RED}[!] Cannot detect OS${NC}"
    exit 1
fi

echo -e "${BLUE}[*] Detected OS: $OS${NC}"

# Installation directory
INSTALL_DIR="${INSTALL_DIR:-$HOME/digipet}"
echo -e "${BLUE}[*] Installation directory: $INSTALL_DIR${NC}"

# Function to check if command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Install system dependencies
echo -e "${GREEN}[+] Installing system dependencies...${NC}"

if [[ "$OS" == "debian" || "$OS" == "ubuntu" || "$OS" == "raspbian" ]]; then
    $SUDO apt-get update -qq
    $SUDO apt-get install -y python3 python3-pip python3-dev git curl net-tools

    # Optional: nmap for advanced scanning
    read -p "Install nmap for active scanning? (y/n) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        $SUDO apt-get install -y nmap
        echo -e "${GREEN}[+] nmap installed${NC}"
    fi

    # Optional: Responder for credential harvesting
    read -p "Install Responder for credential harvesting? (requires root, y/n) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        if command_exists responder; then
            echo -e "${GREEN}[+] Responder already installed${NC}"
        else
            echo -e "${YELLOW}[*] Installing Responder from GitHub...${NC}"
            cd /tmp
            git clone https://github.com/lgandx/Responder
            cd Responder
            $SUDO python3 setup.py install
            cd -
            rm -rf /tmp/Responder
            echo -e "${GREEN}[+] Responder installed${NC}"
        fi
    fi

elif [[ "$OS" == "arch" || "$OS" == "manjaro" ]]; then
    $SUDO pacman -Sy --noconfirm python python-pip git curl net-tools

    read -p "Install nmap? (y/n) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        $SUDO pacman -S --noconfirm nmap
    fi

elif [[ "$OS" == "fedora" || "$OS" == "rhel" || "$OS" == "centos" ]]; then
    $SUDO dnf install -y python3 python3-pip python3-devel git curl net-tools

    read -p "Install nmap? (y/n) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        $SUDO dnf install -y nmap
    fi
else
    echo -e "${YELLOW}[!] Unknown OS, attempting to continue...${NC}"
fi

# Clone or update repository
echo -e "${GREEN}[+] Setting up Digipet...${NC}"

if [ -d "$INSTALL_DIR" ]; then
    echo -e "${YELLOW}[*] Directory exists, updating...${NC}"
    cd "$INSTALL_DIR"
    git pull
else
    echo -e "${BLUE}[*] Cloning repository...${NC}"
    git clone https://github.com/YOUR_USERNAME/digipet.git "$INSTALL_DIR"
    cd "$INSTALL_DIR"
fi

# Install Python dependencies
echo -e "${GREEN}[+] Installing Python dependencies...${NC}"
pip3 install --user -r requirements.txt

# Create systemd service (optional)
read -p "Create systemd service for auto-start on boot? (y/n) " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    echo -e "${BLUE}[*] Creating systemd service...${NC}"

    cat << EOF | $SUDO tee /etc/systemd/system/digipet.service > /dev/null
[Unit]
Description=Digipet Autonomous Pentesting Platform
After=network.target

[Service]
Type=simple
User=$USER
WorkingDirectory=$INSTALL_DIR
ExecStart=/usr/bin/python3 $INSTALL_DIR/digipet_daemon.py --prep
Restart=on-failure
RestartSec=10

[Install]
WantedBy=multi-user.target
EOF

    $SUDO systemctl daemon-reload
    echo -e "${GREEN}[+] Systemd service created${NC}"
    echo -e "${YELLOW}[*] Enable with: sudo systemctl enable digipet${NC}"
    echo -e "${YELLOW}[*] Start with: sudo systemctl start digipet${NC}"
fi

# Create convenience scripts
echo -e "${GREEN}[+] Creating convenience scripts...${NC}"

# Start script
cat << 'EOF' > "$INSTALL_DIR/start.sh"
#!/bin/bash
cd "$(dirname "$0")"
python3 digipet_daemon.py "$@"
EOF
chmod +x "$INSTALL_DIR/start.sh"

# Prep mode script
cat << 'EOF' > "$INSTALL_DIR/start-prep.sh"
#!/bin/bash
cd "$(dirname "$0")"
echo "Starting Digipet in PREP mode (no auto-attack)..."
python3 digipet_daemon.py --prep "$@"
EOF
chmod +x "$INSTALL_DIR/start-prep.sh"

# Add to PATH (optional)
read -p "Add digipet to PATH for easy access? (y/n) " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    SHELL_RC="$HOME/.bashrc"
    if [ -n "$ZSH_VERSION" ]; then
        SHELL_RC="$HOME/.zshrc"
    fi

    if ! grep -q "digipet/start.sh" "$SHELL_RC"; then
        echo "alias digipet='$INSTALL_DIR/start.sh'" >> "$SHELL_RC"
        echo "alias digipet-prep='$INSTALL_DIR/start-prep.sh'" >> "$SHELL_RC"
        echo -e "${GREEN}[+] Aliases added to $SHELL_RC${NC}"
        echo -e "${YELLOW}[*] Run 'source $SHELL_RC' or restart your terminal${NC}"
    fi
fi

# Set web password (optional)
read -p "Set a web portal password now? (y/n) " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    read -s -p "Enter password: " WEB_PASSWORD
    echo
    python3 << EOF
import bcrypt
from pathlib import Path
import json

data_dir = Path.home() / '.digipet'
data_dir.mkdir(parents=True, exist_ok=True)
config_file = data_dir / 'config.json'

config = {}
if config_file.exists():
    with open(config_file, 'r') as f:
        config = json.load(f)

password_hash = bcrypt.hashpw('$WEB_PASSWORD'.encode('utf-8'), bcrypt.gensalt()).decode('utf-8')

if 'web_portal_config' not in config:
    config['web_portal_config'] = {}

config['web_portal_config']['auth_enabled'] = True
config['web_portal_config']['password_hash'] = password_hash
config['web_portal_config']['username'] = 'admin'

with open(config_file, 'w') as f:
    json.dump(config, f, indent=2)

print("[+] Password configured")
EOF
fi

# Installation complete
echo -e "${GREEN}"
cat << "EOF"

╔═══════════════════════════════════════╗
║     Installation Complete! 🎉         ║
╚═══════════════════════════════════════╝

EOF
echo -e "${NC}"

echo -e "${BLUE}Quick Start:${NC}"
echo -e "  1. Prep mode (safe config):     ${GREEN}$INSTALL_DIR/start-prep.sh${NC}"
echo -e "  2. Full run:                    ${GREEN}$INSTALL_DIR/start.sh${NC}"
echo -e "  3. With auto-start:             ${GREEN}$INSTALL_DIR/start.sh --auto-start${NC}"
echo ""
echo -e "${BLUE}Web Portal:${NC}"
echo -e "  Access at: ${GREEN}http://localhost:8080${NC}"
echo ""
echo -e "${BLUE}Documentation:${NC}"
echo -e "  README:       ${GREEN}$INSTALL_DIR/README.md${NC}"
echo -e "  Quick Start:  ${GREEN}$INSTALL_DIR/QUICKSTART.md${NC}"
echo ""
echo -e "${YELLOW}⚠️  IMPORTANT: Only use on networks you own or have permission to test!${NC}"
echo ""

# Offer to start now
read -p "Start Digipet now in PREP mode? (y/n) " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    echo -e "${GREEN}[+] Starting Digipet...${NC}"
    cd "$INSTALL_DIR"
    python3 digipet_daemon.py --prep
fi
