"""
Scheduler module - automated task scheduling and execution.
"""

import threading
import time
from datetime import datetime, timedelta
from typing import Dict, List, Callable, Optional
import json
from pathlib import Path


class ScheduledTask:
    """Represents a scheduled task."""

    def __init__(self, task_id: str, name: str, task_func: Callable,
                 interval_minutes: int = 0, cron_schedule: str = None,
                 enabled: bool = True):
        self.task_id = task_id
        self.name = name
        self.task_func = task_func
        self.interval_minutes = interval_minutes
        self.cron_schedule = cron_schedule
        self.enabled = enabled
        self.last_run = None
        self.next_run = None
        self.run_count = 0
        self.last_result = None

        self._calculate_next_run()

    def _calculate_next_run(self):
        """Calculate next execution time."""
        if not self.enabled:
            self.next_run = None
            return

        if self.interval_minutes > 0:
            if self.last_run:
                self.next_run = self.last_run + timedelta(minutes=self.interval_minutes)
            else:
                self.next_run = datetime.now() + timedelta(minutes=self.interval_minutes)
        elif self.cron_schedule:
            # Simple cron implementation (hour:minute format)
            # e.g., "02:00" = run at 2 AM daily
            try:
                hour, minute = map(int, self.cron_schedule.split(':'))
                now = datetime.now()
                next_time = now.replace(hour=hour, minute=minute, second=0, microsecond=0)

                # If time has passed today, schedule for tomorrow
                if next_time <= now:
                    next_time += timedelta(days=1)

                self.next_run = next_time
            except:
                self.next_run = None

    def should_run(self) -> bool:
        """Check if task should run now."""
        if not self.enabled or not self.next_run:
            return False
        return datetime.now() >= self.next_run

    def execute(self):
        """Execute the task."""
        try:
            print(f"[SCHEDULER] Executing task: {self.name}")
            result = self.task_func()
            self.last_result = {'success': True, 'result': result}
            self.last_run = datetime.now()
            self.run_count += 1
            self._calculate_next_run()
            print(f"[SCHEDULER] Task '{self.name}' completed successfully")
            return True
        except Exception as e:
            print(f"[SCHEDULER] Task '{self.name}' failed: {e}")
            self.last_result = {'success': False, 'error': str(e)}
            self.last_run = datetime.now()
            self._calculate_next_run()
            return False

    def to_dict(self) -> Dict:
        """Convert to dictionary for serialization."""
        return {
            'task_id': self.task_id,
            'name': self.name,
            'interval_minutes': self.interval_minutes,
            'cron_schedule': self.cron_schedule,
            'enabled': self.enabled,
            'last_run': self.last_run.isoformat() if self.last_run else None,
            'next_run': self.next_run.isoformat() if self.next_run else None,
            'run_count': self.run_count,
            'last_result': self.last_result
        }


class TaskScheduler:
    """Manages scheduled tasks."""

    def __init__(self, data_dir: Path):
        self.data_dir = data_dir
        self.tasks: Dict[str, ScheduledTask] = {}
        self.running = False
        self.scheduler_thread = None

        # Load saved schedules
        self._load_schedules()

    def add_task(self, task: ScheduledTask):
        """Add a scheduled task."""
        self.tasks[task.task_id] = task
        self._save_schedules()
        print(f"[SCHEDULER] Added task: {task.name}")

    def remove_task(self, task_id: str) -> bool:
        """Remove a scheduled task."""
        if task_id in self.tasks:
            del self.tasks[task_id]
            self._save_schedules()
            print(f"[SCHEDULER] Removed task: {task_id}")
            return True
        return False

    def enable_task(self, task_id: str, enabled: bool = True):
        """Enable or disable a task."""
        if task_id in self.tasks:
            self.tasks[task_id].enabled = enabled
            self.tasks[task_id]._calculate_next_run()
            self._save_schedules()
            print(f"[SCHEDULER] Task {task_id} {'enabled' if enabled else 'disabled'}")

    def get_task(self, task_id: str) -> Optional[ScheduledTask]:
        """Get a task by ID."""
        return self.tasks.get(task_id)

    def get_all_tasks(self) -> List[Dict]:
        """Get all tasks as dictionaries."""
        return [task.to_dict() for task in self.tasks.values()]

    def start(self):
        """Start the scheduler."""
        if self.running:
            return

        self.running = True
        self.scheduler_thread = threading.Thread(target=self._scheduler_loop, daemon=True)
        self.scheduler_thread.start()
        print("[SCHEDULER] Started")

    def stop(self):
        """Stop the scheduler."""
        self.running = False
        if self.scheduler_thread:
            self.scheduler_thread.join(timeout=5)
        print("[SCHEDULER] Stopped")

    def _scheduler_loop(self):
        """Main scheduler loop."""
        while self.running:
            try:
                # Check all tasks
                for task in self.tasks.values():
                    if task.should_run():
                        # Execute in separate thread to not block scheduler
                        exec_thread = threading.Thread(target=task.execute, daemon=True)
                        exec_thread.start()

                # Sleep for 30 seconds before checking again
                time.sleep(30)

            except Exception as e:
                print(f"[SCHEDULER] Error in scheduler loop: {e}")
                time.sleep(60)

    def _save_schedules(self):
        """Save task schedules to disk."""
        try:
            schedule_file = self.data_dir / 'schedules.json'

            # Only save task configuration, not the function reference
            save_data = []
            for task in self.tasks.values():
                save_data.append({
                    'task_id': task.task_id,
                    'name': task.name,
                    'interval_minutes': task.interval_minutes,
                    'cron_schedule': task.cron_schedule,
                    'enabled': task.enabled
                })

            with open(schedule_file, 'w') as f:
                json.dump(save_data, f, indent=2)

        except Exception as e:
            print(f"[SCHEDULER] Error saving schedules: {e}")

    def _load_schedules(self):
        """Load task schedules from disk."""
        try:
            schedule_file = self.data_dir / 'schedules.json'
            if not schedule_file.exists():
                return

            with open(schedule_file, 'r') as f:
                saved_tasks = json.load(f)

            # Note: Tasks must be re-registered with their functions after loading
            # This just loads the configuration
            print(f"[SCHEDULER] Found {len(saved_tasks)} saved task configurations")

        except Exception as e:
            print(f"[SCHEDULER] Error loading schedules: {e}")

    def run_task_now(self, task_id: str) -> bool:
        """Manually trigger a task to run immediately."""
        if task_id in self.tasks:
            task = self.tasks[task_id]
            exec_thread = threading.Thread(target=task.execute, daemon=True)
            exec_thread.start()
            return True
        return False
