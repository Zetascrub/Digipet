#!/usr/bin/env python3
"""
Digipet Daemon - Main entry point for the autonomous pentesting system.

This daemon initializes all components and runs both the e-ink display
and web portal interfaces.
"""

import sys
import signal
import threading
from pathlib import Path

# Import Digipet modules
from modules.database import DigipetDatabase
from modules.evolution import EvolutionEngine, EvolutionConfig
from modules.config import DigipetConfig
from modules.orchestrator import AttackOrchestrator
from modules.scheduler import TaskScheduler
import web_portal


# Global instances
db = None
evolution = None
config = None
orchestrator = None
scheduler = None
display_thread = None


def signal_handler(sig, frame):
    """Handle shutdown signals gracefully."""
    print("\n[*] Shutting down Digipet...")

    if orchestrator:
        orchestrator.stop()

    if scheduler:
        scheduler.stop()

    sys.exit(0)


def init_digipet(data_dir: Path = None):
    """
    Initialize all Digipet components.

    Args:
        data_dir: Directory for storing data (default: ~/.digipet/)
    """
    global db, evolution, config, orchestrator, scheduler

    # Setup data directory
    if not data_dir:
        data_dir = Path.home() / '.digipet'

    data_dir.mkdir(parents=True, exist_ok=True)

    print(f"[*] Initializing Digipet with data directory: {data_dir}")

    # Initialize database
    db_path = data_dir / 'digipet.db'
    db = DigipetDatabase(db_path)
    print(f"[+] Database initialized: {db_path}")

    # Initialize evolution system
    evolution_state_file = data_dir / 'evolution.json'
    evolution_config = EvolutionConfig()  # Uses Agumon line by default
    evolution = EvolutionEngine(evolution_state_file, evolution_config)
    print(f"[+] Evolution system initialized")
    print(f"    Current stage: {evolution.get_current_stage().name} (Level {evolution.stats.current_stage})")
    print(f"    Total XP: {evolution.stats.total_xp}")

    # Initialize configuration
    config_file = data_dir / 'config.json'
    config = DigipetConfig(config_file)
    print(f"[+] Configuration loaded")
    print(f"    Current profile: {config.current_profile_name}")

    # Initialize attack orchestrator
    orchestrator = AttackOrchestrator(db, evolution, config)
    print(f"[+] Attack orchestrator initialized")

    # Initialize task scheduler
    scheduler = TaskScheduler(data_dir)
    print(f"[+] Task scheduler initialized")

    # Initialize web portal
    web_portal.init_web_portal(db, evolution, config, orchestrator, scheduler)
    print(f"[+] Web portal initialized")

    return {
        'database': db,
        'evolution': evolution,
        'config': config,
        'orchestrator': orchestrator,
        'scheduler': scheduler
    }


def run_display_interface():
    """
    Run the e-ink display interface.

    This runs the integrated display that shows evolution and attack status.
    """
    try:
        import main_integrated

        # Run the integrated display with all our components
        main_integrated.run_display(db, evolution, config, orchestrator)

    except Exception as e:
        print(f"[!] Error running display interface: {e}")
        import traceback
        traceback.print_exc()


def main():
    """Main entry point."""
    print("""
    ╔═══════════════════════════════════════╗
    ║                                       ║
    ║       🦖  D I G I P E T  🦖          ║
    ║                                       ║
    ║   Autonomous Pentesting Platform      ║
    ║                                       ║
    ╚═══════════════════════════════════════╝
    """)

    # Setup signal handlers
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)

    # Parse command line arguments
    import argparse
    parser = argparse.ArgumentParser(description='Digipet Autonomous Pentesting Platform')
    parser.add_argument('--data-dir', type=Path, help='Data directory (default: ~/.digipet)')
    parser.add_argument('--no-web', action='store_true', help='Disable web portal')
    parser.add_argument('--screen', action='store_true', help='Enable e-ink display interface')
    parser.add_argument('--auto-start', action='store_true', help='Auto-start orchestrator immediately')
    parser.add_argument('--prep', action='store_true', help='Prep mode: start services but do NOT auto-attack (safe for config)')
    parser.add_argument('--web-host', default='0.0.0.0', help='Web portal host')
    parser.add_argument('--web-port', type=int, default=8080, help='Web portal port')
    parser.add_argument('--web-password', type=str, help='Set web portal password (optional)')

    args = parser.parse_args()

    # Initialize Digipet
    components = init_digipet(args.data_dir)

    # Set web password if provided
    if args.web_password:
        import bcrypt
        password_hash = bcrypt.hashpw(args.web_password.encode('utf-8'), bcrypt.gensalt()).decode('utf-8')
        components['config'].update_web_portal_config(
            auth_enabled=True,
            password_hash=password_hash
        )
        print(f"[+] Web portal password set")

    # Handle auto-start vs prep mode
    if args.auto_start and not args.prep:
        print("\n[*] Auto-starting orchestrator...")
        components['orchestrator'].start()
    elif args.prep:
        print("\n[*] PREP MODE: Services started but attacks DISABLED")
        print("[*] Use the web portal to configure settings safely")
        print("[*] Click 'Start' in the web portal when ready to begin attacks")
    elif args.auto_start and args.prep:
        print("\n[!] WARNING: --auto-start and --prep are mutually exclusive")
        print("[*] Using PREP MODE (attacks disabled for safety)")

    # Start display interface in separate thread if enabled
    if args.screen:
        display_thread = threading.Thread(
            target=run_display_interface,
            daemon=True
        )
        display_thread.start()
        print("[+] E-ink display interface started (use --screen flag to enable)")

    # Start web portal (blocking)
    if not args.no_web:
        print(f"\n[*] Starting web portal on http://{args.web_host}:{args.web_port}")
        print(f"[*] Open this URL in your browser to access the control panel")
        print(f"\n")
        web_portal.run_web_portal(host=args.web_host, port=args.web_port)
    else:
        # If no web portal, just keep running
        print("[*] Running in headless mode (no web portal)")
        print("[*] Press Ctrl+C to stop")
        try:
            while True:
                import time
                time.sleep(1)
        except KeyboardInterrupt:
            pass


if __name__ == '__main__':
    main()
