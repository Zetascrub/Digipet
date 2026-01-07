"""
Evolution system for Digipet - manages growth stages and XP progression.
"""

import json
from pathlib import Path
from typing import Dict, Optional
from dataclasses import dataclass, asdict
from datetime import datetime


@dataclass
class EvolutionStage:
    """Represents a single evolution stage."""
    name: str
    level: int
    xp_required: int
    sprite_file: str
    description: str


@dataclass
class DigipetStats:
    """Current stats and state of the Digipet."""
    current_stage: int
    total_xp: int
    hosts_discovered: int
    services_enumerated: int
    credentials_captured: int
    vulnerabilities_found: int
    evolution_timestamp: str
    birth_timestamp: str


class EvolutionConfig:
    """Configurable evolution line - default progression from novice to master."""

    DEFAULT_LINE = [
        EvolutionStage(
            name="Hatchling",
            level=0,
            xp_required=0,
            sprite_file="stage0.png",
            description="Stage 0 - Just getting started"
        ),
        EvolutionStage(
            name="Learner",
            level=1,
            xp_required=100,
            sprite_file="stage1.png",
            description="Stage 1 - Learning the basics"
        ),
        EvolutionStage(
            name="Scout",
            level=2,
            xp_required=500,
            sprite_file="stage2.png",
            description="Stage 2 - Ready for reconnaissance"
        ),
        EvolutionStage(
            name="Hunter",
            level=3,
            xp_required=2000,
            sprite_file="stage3.png",
            description="Stage 3 - An experienced operator"
        ),
        EvolutionStage(
            name="Specialist",
            level=4,
            xp_required=5000,
            sprite_file="stage4.png",
            description="Stage 4 - Advanced capabilities"
        ),
        EvolutionStage(
            name="Master",
            level=5,
            xp_required=10000,
            sprite_file="stage5.png",
            description="Stage 5 - Peak performance"
        )
    ]

    def __init__(self, custom_line: Optional[list] = None):
        self.evolution_line = custom_line if custom_line else self.DEFAULT_LINE

    def get_stage(self, level: int) -> Optional[EvolutionStage]:
        """Get evolution stage by level."""
        for stage in self.evolution_line:
            if stage.level == level:
                return stage
        return None

    def get_stage_by_xp(self, xp: int) -> EvolutionStage:
        """Determine stage based on total XP."""
        current_stage = self.evolution_line[0]
        for stage in self.evolution_line:
            if xp >= stage.xp_required:
                current_stage = stage
            else:
                break
        return current_stage


class EvolutionEngine:
    """Manages evolution state and XP progression."""

    # XP values for different discoveries
    XP_REWARDS = {
        'host_discovered': 10,
        'service_enumerated': 5,
        'credential_captured': 50,
        'vulnerability_found': 25,
        'exploit_success': 100
    }

    def __init__(self, state_file: Path, evolution_config: Optional[EvolutionConfig] = None):
        self.state_file = state_file
        self.config = evolution_config if evolution_config else EvolutionConfig()
        self.stats = self._load_state()

    def _load_state(self) -> DigipetStats:
        """Load evolution state from disk or create new."""
        if self.state_file.exists():
            with open(self.state_file, 'r') as f:
                data = json.load(f)
                return DigipetStats(**data)
        else:
            # Create new Digipet
            now = datetime.now().isoformat()
            return DigipetStats(
                current_stage=0,
                total_xp=0,
                hosts_discovered=0,
                services_enumerated=0,
                credentials_captured=0,
                vulnerabilities_found=0,
                evolution_timestamp=now,
                birth_timestamp=now
            )

    def save_state(self):
        """Persist evolution state to disk."""
        self.state_file.parent.mkdir(parents=True, exist_ok=True)
        with open(self.state_file, 'w') as f:
            json.dump(asdict(self.stats), f, indent=2)

    def add_xp(self, event_type: str, count: int = 1) -> Dict:
        """
        Add XP for a discovery event.

        Returns dict with: {'xp_gained', 'total_xp', 'evolved', 'new_stage'}
        """
        if event_type not in self.XP_REWARDS:
            return {'error': f'Unknown event type: {event_type}'}

        xp_per_event = self.XP_REWARDS[event_type]
        xp_gained = xp_per_event * count

        old_stage = self.get_current_stage()
        self.stats.total_xp += xp_gained

        # Update stat counters
        if event_type == 'host_discovered':
            self.stats.hosts_discovered += count
        elif event_type == 'service_enumerated':
            self.stats.services_enumerated += count
        elif event_type == 'credential_captured':
            self.stats.credentials_captured += count
        elif event_type == 'vulnerability_found':
            self.stats.vulnerabilities_found += count

        new_stage = self.config.get_stage_by_xp(self.stats.total_xp)

        evolved = False
        if new_stage.level > old_stage.level:
            evolved = True
            self.stats.current_stage = new_stage.level
            self.stats.evolution_timestamp = datetime.now().isoformat()

        self.save_state()

        return {
            'xp_gained': xp_gained,
            'total_xp': self.stats.total_xp,
            'evolved': evolved,
            'new_stage': new_stage.name if evolved else None,
            'current_level': new_stage.level
        }

    def get_current_stage(self) -> EvolutionStage:
        """Get current evolution stage."""
        return self.config.get_stage(self.stats.current_stage) or self.config.evolution_line[0]

    def get_next_stage(self) -> Optional[EvolutionStage]:
        """Get next evolution stage if available."""
        current = self.get_current_stage()
        if current.level < len(self.config.evolution_line) - 1:
            return self.config.get_stage(current.level + 1)
        return None

    def get_progress_to_next(self) -> Dict:
        """Calculate progress to next evolution."""
        current = self.get_current_stage()
        next_stage = self.get_next_stage()

        if not next_stage:
            return {
                'is_max_level': True,
                'current_xp': self.stats.total_xp,
                'progress_percent': 100
            }

        xp_for_next = next_stage.xp_required
        xp_current_stage = current.xp_required
        xp_needed = xp_for_next - xp_current_stage
        xp_progress = self.stats.total_xp - xp_current_stage

        progress_percent = min(100, (xp_progress / xp_needed * 100)) if xp_needed > 0 else 100

        return {
            'is_max_level': False,
            'current_xp': self.stats.total_xp,
            'xp_to_next': xp_for_next - self.stats.total_xp,
            'next_stage_name': next_stage.name,
            'progress_percent': round(progress_percent, 1)
        }

    def get_full_status(self) -> Dict:
        """Get complete evolution status for display/API."""
        current_stage = self.get_current_stage()
        progress = self.get_progress_to_next()

        return {
            'current_stage': {
                'name': current_stage.name,
                'level': current_stage.level,
                'description': current_stage.description
            },
            'stats': asdict(self.stats),
            'progress': progress,
            'xp_rewards': self.XP_REWARDS
        }

    def reset(self):
        """Reset to initial stage (for testing or new deployment)."""
        now = datetime.now().isoformat()
        self.stats = DigipetStats(
            current_stage=0,
            total_xp=0,
            hosts_discovered=0,
            services_enumerated=0,
            credentials_captured=0,
            vulnerabilities_found=0,
            evolution_timestamp=now,
            birth_timestamp=now
        )
        self.save_state()
