# Changelog

All notable changes to Digipet will be documented in this file.

## [2.4.1] - 2026-01-07

### Added - Host Details and Connectivity
- **Host Details Page**: Comprehensive full-page view for individual hosts
  - Complete host information display with metadata
  - Detailed services table with port, protocol, version, and banner information
  - Vulnerabilities section with severity indicators (Critical/High/Medium/Low)
  - Credentials display with copy-to-clipboard functionality
  - Responsive design matching dashboard theme
  - One-click navigation from dashboard host table
- **Internet Connectivity Monitor**: Real-time connectivity status indicator
  - Visual status indicator in dashboard header (green/red)
  - Automatic connectivity checks using DNS and HTTP probes
  - Updates every 10 seconds with dashboard refresh
  - Helps monitor network isolation during pentesting

### Changed
- Host "View" button now navigates to full details page instead of popup alert
- Dashboard host table now includes service and vulnerability counts from API

### Fixed
- Priority targets API error handling now displays user-friendly error messages
- Null pointer exception in hostname checking for priority target scoring

## [2.4.0] - 2026-01-06

### Added - Modern Web Interface
- **Complete Dashboard Redesign**: Modern, user-friendly web interface with enhanced UX
  - Quick actions panel with one-click operations
  - Real-time metrics dashboard with animated visualizations
  - Interactive network map showing host status at a glance
  - Advanced search and filtering capabilities
  - Smart recommendations panel with contextual insights
  - Dark/light theme toggle with persistent preferences
  - Fully mobile responsive design
- **Enhanced User Experience**:
  - Priority targets display showing high-value hosts
  - Live activity feed with recent discoveries
  - Real-time scan progress with detailed statistics
  - Improved host table with inline filtering
  - Network coverage analytics visualization
- **Quick Actions**:
  - One-click quick scan, smart scan, and deep scan
  - Direct access to vulnerable hosts and credentials
  - Instant report generation

### Changed
- Default dashboard now uses modern interface (classic available at `/dashboard_classic`)
- Improved mobile responsiveness across all screens
- Better visual hierarchy and information density

## [2.3.0] - 2026-01-06

### Added - Performance Optimizations
- **Parallel Scanning**: Multi-threaded port scanning across multiple hosts simultaneously
  - 10-20x faster scanning depending on stealth profile
  - Configurable worker count (20 workers in aggressive, 3 in stealth)
  - Thread pool executor for efficient resource usage
- **Smart Target Prioritization**: Intelligent host selection based on value
  - Scores hosts by service count, credentials, vulnerabilities, and age
  - Prioritizes high-value targets (servers, hosts with creds/vulns)
  - Identifies stale data needing refresh
- **Incremental Scanning**: Delta scanning to avoid redundant work
  - Only scans new/unscanned hosts by default
  - Tracks network coverage percentage
  - Dramatically reduces scan time on subsequent runs
- **Smart Scan API**: Unified intelligent scanning endpoint
  - Combines all optimization strategies
  - Automatic mode selection (full vs incremental)
  - Returns comprehensive statistics

### API Endpoints Added
- `POST /api/scan/smart` - Run smart incremental scan
- `POST /api/scan/parallel` - Parallel port scan on specific targets
- `GET /api/scan/priority-targets` - Get prioritized scan targets
- `GET /api/scan/unscanned` - List unscanned hosts in network

### Improved
- Orchestrator reconnaissance now uses parallel scanning by default
- Scan times reduced by 80-90% on typical networks
- Better resource utilization with thread pooling
- More intelligent targeting reduces wasted scans

## [2.2.0] - 2026-01-06

### Added
- **Data Management**: Complete database management with backup, vacuum, and selective clearing
- **Network Intelligence**: Automatic interface detection and network suggestions
- **Analytics Engine**: Comprehensive scan statistics and insights
- **Generic Evolution System**: Replaced Digimon-specific names with generic progression (Hatchling → Master)
- Web UI for data management in config page
- 35+ new API endpoints for analytics and data management

### Changed
- Display now opt-in with `--screen` flag instead of `--no-display`
- Evolution stages renamed to avoid copyright issues
- Project files cleaned up and organized

### Removed
- Unnecessary documentation files (NEW_FEATURES.md, NEW_IMPROVEMENTS.md, FEATURE_UPDATE_v2.2.md)
- Unused Python files (main.py, digipet_cli.py)
- All Digimon-specific references

## [2.1.0] - 2026-01-06

### Added
- Real-time scan progress tracking with visual feedback
- Task scheduler with automated scanning support
- Enhanced error handling with automatic recovery
- Progress bar in web dashboard
- Scheduler API endpoints

### Improved
- Orchestrator stability with exponential backoff retry
- Error recovery mechanisms
- Auto-pause protection after repeated failures

## [2.0.0] - Previous Release

### Added
- Professional reporting (PDF/HTML/JSON/CSV)
- Real-time notifications (Email/Slack/Discord)
- Powerful CLI tool for quick queries
- VPN network scanning support
- Multiple export formats

### Improved
- Web portal UI enhancements
- Database performance optimizations
- Configuration management

## [1.0.0] - Initial Release

### Added
- Autonomous penetration testing platform
- Evolution/XP progression system
- Network reconnaissance capabilities
- Credential harvesting with Responder
- Vulnerability scanning
- Web dashboard interface
- E-ink display support for Raspberry Pi
- SQLite database for storing discoveries
- Stealth profiles (Aggressive, Normal, Stealth, Passive)
