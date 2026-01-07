# Troubleshooting Guide

## Common Issues and Solutions

### 🔒 Web Portal Shows "401 UNAUTHORIZED" Errors

**Symptoms:**
- Browser console shows `401 (UNAUTHORIZED)` errors
- Dashboard doesn't load data
- All API requests fail

**Cause:**
Authentication is enabled but no password is set, or you're accessing without credentials.

**Solutions:**

**Option 1: Disable Authentication (Development)**
```bash
# Edit ~/.digipet/config.json
{
  "web_portal_config": {
    "auth_enabled": false
  }
}

# Or restart without password
python3 digipet_daemon.py --prep
```

**Option 2: Set a Password (Recommended for Production)**
```bash
python3 digipet_daemon.py --web-password yourSecretPassword
```

**Option 3: Use Login Page**
If auth is enabled, navigate to the web portal and you'll see a login page:
- Username: `admin` (fixed)
- Password: (the password you set with --web-password)
- Credentials are stored in your browser session

---

### 📡 No Hosts Discovered

**Symptoms:**
- Dashboard shows 0 hosts
- Network scan isn't finding anything

**Possible Causes & Solutions:**

**1. Orchestrator Not Started:**
```bash
# Make sure orchestrator is running
# Check web dashboard - click "Start" button
# Or use auto-start mode
python3 digipet_daemon.py --auto-start
```

**2. Wrong Network Interface:**
```bash
# Check your network interfaces
ip addr show

# Update config if needed
# Edit ~/.digipet/config.json
{
  "attack_config": {
    "target_network": "192.168.1.0/24"  # Your actual network
  }
}
```

**3. Scanning Through VPN (Tailscale, WireGuard, etc.):**
If you're scanning a network through a VPN:
```bash
# Passive ARP scanning won't work through VPN
# You must rely on active nmap scanning

# Option 1: Use faster nmap args for VPN scanning
# Edit ~/.digipet/config.json:
{
  "attack_config": {
    "nmap_args": "-sn",  # Ping scan only, much faster
    "target_network": "192.168.8.0/24"
  }
}

# Option 2: For full service scanning through VPN
{
  "attack_config": {
    "nmap_args": "-sS -T4",  # SYN scan, aggressive timing
    "target_network": "192.168.8.0/24"
  }
}

# Then start Digipet and watch terminal output
python3 digipet_daemon.py --web-password yourpass

# You should see:
# [RECON] Starting network reconnaissance...
# [ARP] Checked ARP cache: 0 entries, 0 new hosts discovered
# [NMAP] Running scan: nmap -sn 192.168.8.0/24
# [NMAP] Scan complete. Discovered 30 hosts
```

**Note:** VPN networks (Tailscale, ZeroTier, WireGuard) are Layer 3 only.
Passive ARP scanning won't find hosts. You MUST enable and run active nmap scanning.

**4. Firewall Blocking:**
```bash
# Temporary disable firewall for testing (BE CAREFUL!)
sudo ufw disable

# Or allow specific traffic
sudo ufw allow from 192.168.1.0/24
```

**5. Need Root for Some Features:**
```bash
# Some network operations require root
sudo python3 digipet_daemon.py --prep
```

---

### 🔍 Nmap Not Working

**Symptoms:**
- Active scans don't find services
- Log shows "nmap not installed"

**Solution:**
```bash
# Install nmap
sudo apt install nmap

# Verify installation
which nmap
nmap --version
```

---

### 📧 Responder Not Starting

**Symptoms:**
- No credentials being captured
- Log shows "Responder not installed"

**Solutions:**

**Install Responder:**
```bash
# Option 1: Package manager
sudo apt install responder

# Option 2: From source
git clone https://github.com/lgandx/Responder
cd Responder
sudo python3 setup.py install
```

**Run with Sudo:**
```bash
# Responder needs raw socket access
sudo python3 digipet_daemon.py --prep
```

**Check Interface:**
```bash
# Make sure wlan0 exists (or update config)
ip addr show

# Update in config if using different interface
{
  "attack_config": {
    "responder_interface": "eth0"  # Your actual interface
  }
}
```

---

### 🖥️ E-ink Display Not Working

**Symptoms:**
- Display shows nothing
- Errors about SPI or GPIO

**Solution:**

**Using Mock Display (Development):**
```bash
# Set environment variable
export USE_MOCK=1
python3 digipet_daemon.py
```

**Hardware Display (Raspberry Pi):**
```bash
# Enable SPI
sudo raspi-config
# Interface Options → SPI → Enable

# Install hardware libraries
sudo apt install python3-rpi.gpio

# Run without mock
export USE_MOCK=0
python3 digipet_daemon.py
```

**Disable Display Entirely:**
```bash
python3 digipet_daemon.py --no-display
```

---

### 🌐 Web Portal Not Accessible

**Symptoms:**
- Can't connect to http://localhost:8080
- Connection refused

**Solutions:**

**1. Check if Running:**
```bash
# Look for python process
ps aux | grep digipet

# Check port
sudo netstat -tulpn | grep 8080
```

**2. Port Already in Use:**
```bash
# Use different port
python3 digipet_daemon.py --web-port 9000
```

**3. Firewall Blocking:**
```bash
# Allow port through firewall
sudo ufw allow 8080/tcp
```

**4. Binding to Wrong Interface:**
```bash
# Try binding to all interfaces
python3 digipet_daemon.py --web-host 0.0.0.0
```

---

### 📊 Reports Not Generating

**Symptoms:**
- CLI command fails
- Web API returns error

**Solutions:**

**1. Check Permissions:**
```bash
# Ensure ~/.digipet/reports directory is writable
ls -la ~/.digipet/
chmod 755 ~/.digipet/reports
```

**2. Missing Dependencies:**
```bash
# Reinstall requirements
pip3 install -r requirements.txt
```

**3. No Data Yet:**
```bash
# Make sure you have some discoveries first
digipet-cli status
# If everything is 0, run some scans first
```

---

### 🔔 Notifications Not Sending

**Symptoms:**
- Test notification fails
- No alerts received

**Solutions:**

**Email Issues:**
```bash
# Check SMTP settings
POST /api/notifications/config
{
  "email_smtp_server": "smtp.gmail.com",
  "email_smtp_port": 587,
  "email_username": "your@gmail.com",
  "email_password": "app_password",  # Use app password, not regular password!
  "email_from": "digipet@yourdomain.com",
  "email_to": ["recipient@example.com"]
}

# For Gmail: Enable "Less secure apps" or use App Password
# https://support.google.com/accounts/answer/185833
```

**Slack/Discord Issues:**
```bash
# Verify webhook URL is correct
curl -X POST YOUR_WEBHOOK_URL \
  -H "Content-Type: application/json" \
  -d '{"text":"Test message"}'

# Check webhook permissions in Slack/Discord
```

**Enable Notifications:**
```bash
POST /api/notifications/config
{
  "enabled": true,
  "notify_on": {
    "critical_vuln": true,
    "credential_captured": true
  }
}
```

---

### 💾 Database Errors

**Symptoms:**
- SQLite errors
- Data not saving
- Corruption messages

**Solutions:**

**1. Reset Database (⚠️ DELETES ALL DATA):**
```bash
rm ~/.digipet/digipet.db
rm ~/.digipet/evolution.json
python3 digipet_daemon.py --prep
```

**2. Backup Database:**
```bash
# Before troubleshooting
cp ~/.digipet/digipet.db ~/.digipet/digipet.db.backup
```

**3. Check Permissions:**
```bash
ls -la ~/.digipet/
chmod 644 ~/.digipet/digipet.db
```

---

### 🐍 Python Errors

**Symptoms:**
- Import errors
- Module not found

**Solutions:**

**1. Wrong Python Version:**
```bash
# Check version (need 3.7+)
python3 --version

# Use correct python
python3.9 digipet_daemon.py --prep
```

**2. Missing Dependencies:**
```bash
# Reinstall everything
pip3 install -r requirements.txt

# Or use virtual environment
python3 -m venv venv
source venv/bin/activate
pip install -r requirements.txt
```

**3. Path Issues:**
```bash
# Run from digipet directory
cd /path/to/digipet
python3 digipet_daemon.py --prep
```

---

### 🔧 CLI Tool Not Working

**Symptoms:**
- `digipet-cli` command not found
- Permission denied

**Solutions:**

**1. Make Executable:**
```bash
chmod +x digipet_cli.py
```

**2. Run with Python:**
```bash
python3 digipet_cli.py status
```

**3. Add to PATH:**
```bash
# Add alias to .bashrc
echo "alias digipet-cli='python3 /path/to/digipet_cli.py'" >> ~/.bashrc
source ~/.bashrc
```

**4. Data Directory Not Found:**
```bash
# Specify data directory
python3 digipet_cli.py --data-dir ~/.digipet status
```

---

### 🚀 Performance Issues

**Symptoms:**
- Slow scans
- High CPU usage
- Web portal sluggish

**Solutions:**

**1. Adjust Stealth Profile:**
```bash
# Use faster profile
# In web portal, select "Aggressive" or "Normal"
# Or via API:
POST /api/config/profile
{"profile": "normal"}
```

**2. Limit Concurrent Scans:**
```bash
# Edit config
{
  "current_profile_settings": {
    "max_parallel_scans": 2  # Reduce from 5 or 10
  }
}
```

**3. Database Optimization:**
```bash
# Vacuum database to optimize
sqlite3 ~/.digipet/digipet.db "VACUUM;"
```

**4. Reduce Terminal Logging:**
Flask request logs and display screen messages are already minimized in v2.0+. If you still see too many logs, redirect output:
```bash
# Redirect to log file
python3 digipet_daemon.py --web-password mypass > digipet.log 2>&1 &

# Or suppress most output
python3 digipet_daemon.py --web-password mypass 2>/dev/null
```

---

### 🔐 Permission Denied Errors

**Symptoms:**
- Can't bind to port
- Can't access network interfaces
- Can't write files

**Solutions:**

**1. Run with Sudo (if needed):**
```bash
sudo python3 digipet_daemon.py --prep
```

**2. Use Non-Privileged Ports:**
```bash
# Use port >1024
python3 digipet_daemon.py --web-port 8080
```

**3. Fix File Permissions:**
```bash
sudo chown -R $USER:$USER ~/.digipet/
chmod -R 755 ~/.digipet/
```

---

## Getting Help

If your issue isn't listed here:

1. **Check Logs:**
   ```bash
   # Terminal output shows detailed errors
   python3 digipet_daemon.py --prep 2>&1 | tee digipet.log
   ```

2. **Enable Debug Mode:**
   ```bash
   # Set environment variable
   export DIGIPET_DEBUG=1
   python3 digipet_daemon.py --prep
   ```

3. **GitHub Issues:**
   - Search existing issues: https://github.com/YOUR_USERNAME/digipet/issues
   - Create new issue with:
     - OS and Python version
     - Full error message
     - Steps to reproduce
     - Output of `digipet-cli status`

4. **Community:**
   - Check documentation: README.md, QUICKSTART.md, NEW_FEATURES.md
   - Review CONTRIBUTING.md for development setup

---

## Quick Diagnostic Commands

```bash
# System info
uname -a
python3 --version

# Check installation
which nmap
which responder
which python3

# Check processes
ps aux | grep digipet
ps aux | grep responder

# Check network
ip addr show
ip route show
netstat -tulpn | grep 8080

# Check data
ls -la ~/.digipet/
sqlite3 ~/.digipet/digipet.db "SELECT COUNT(*) FROM hosts;"

# Check API
curl http://localhost:8080/api/status

# CLI status
python3 digipet_cli.py status
```

---

**Remember:** Most issues are configuration-related. Start in `--prep` mode to safely configure everything before running autonomous attacks!
