from __future__ import annotations

import argparse
import asyncio
import cmd
import pathlib
import shlex
from time import time_ns

from midge_badge_framework.hub import GroupCommandExecResult, MidgeBadgeHub, MidgeBadgeHubException
from midge_badge_framework.schema import BadgeSchema, ExperimentSchema


class HubCLI(cmd.Cmd):
    intro = "Midge Badge Hub CLI. Type help or ? to list commands."
    prompt = "(hub) "

    def __init__(self, yaml_file: str):
        super().__init__()
        self.hub: MidgeBadgeHub = MidgeBadgeHub(yaml_file)

    def onecmd(self, line: str) -> bool | None:
        try:
            return super().onecmd(line)
        except MidgeBadgeHubException as exc:
            print(f"Hub error: {exc}")
            return False

    @staticmethod
    def _split_args(arg: str) -> list[str]:
        return shlex.split(arg) if arg else []

    def _print_selected_badge(self) -> None:
        selected: BadgeSchema | None = self.hub.get_selected_badge()
        if selected is None:
            print("Selected badge: ALL")
            return
        print(f"Selected badge: {selected.name} ({selected.mac})")

    @staticmethod
    def _print_response_rows(rows: list[GroupCommandExecResult] | None) -> None:
        if not rows:
            print("No responses")
            return
        for group_result in rows:
            print(f'{"=" * 30}Group {group_result.group.id} "{group_result.group.name}"{"=" * 30}')
            for badge_result in group_result.badge_results:
                print(f"{' ':20}Badge {badge_result.badge}:")
                for cmd_result in badge_result.responses:
                    print(f"+ {cmd_result}")

    def emptyline(self) -> None:
        # Avoid repeating the previous command on empty input.
        return

    def do_init(self, arg: str) -> None:
        """init: Initialize experiment by connecting to badges and performing checks."""
        _ = arg
        self.hub.init_experiment()
        print("Experiment initialized")

    def do_experiment(self, arg: str) -> None:
        """experiment: Show high-level experiment summary."""
        _ = arg
        experiment = self.hub.get_schema()
        print(f"Experiment {experiment.id}: {experiment.name}")
        print(f"Description: {experiment.description}")
        print(f"Groups: {len(experiment.groups)}")

    def do_list_badges(self, arg: str) -> None:
        """list_badges: List all groups and badges configured in the experiment."""
        _ = arg
        experiment: ExperimentSchema = self.hub.get_schema()
        for group in experiment.groups:
            print(f"Group {group.id}: {group.name}")
            for badge in group.badges:
                badge_label = badge.name if badge.name is not None else f"badge-{badge.id}"
                print(f"  Badge {badge.id}: {badge_label} (MAC: {badge.mac})")

    def do_selected(self, arg: str) -> None:
        """selected: Show currently selected badge, or ALL when broadcasting."""
        _ = arg
        self._print_selected_badge()

    def do_select_name(self, arg: str) -> None:
        """select_name <name>: Select one badge by name."""
        args = self._split_args(arg)
        if len(args) != 1:
            print("usage: select_name <name>")
            return
        name = args[0]
        self.hub.select_badge_by_name(name)
        after = self.hub.get_selected_badge()
        if after is None or after.name != name:
            print(f"Badge not found by name: {name}")
            return
        self._print_selected_badge()

    def do_select_mac(self, arg: str) -> None:
        """select_mac <mac>: Select one badge by MAC address."""
        args = self._split_args(arg)
        if len(args) != 1:
            print("usage: select_mac <mac>")
            return
        mac = args[0]
        self.hub.select_badge_by_mac(mac)
        after = self.hub.get_selected_badge()
        if after is None or after.mac != mac:
            print(f"Badge not found by MAC: {mac}")
            return
        self._print_selected_badge()

    def do_unselect(self, arg: str) -> None:
        """unselect: Clear selected badge and broadcast commands to all badges."""
        _ = arg
        self.hub.unselect_badge()
        self._print_selected_badge()

    def do_status(self, arg: str) -> None:
        """status: Run a status check on selected badge(s)."""
        _ = arg
        status = self.hub.get_status()
        self._print_response_rows(status)

    def do_fw_version(self, arg: str) -> None:
        """fw_version: Read firmware version from selected badge(s)."""
        _ = arg
        self._print_response_rows(self.hub.get_fw_version())

    def do_reset(self, arg: str) -> None:
        """reset: Reset selected badge(s)."""
        _ = arg
        self._print_response_rows(self.hub.reset())

    def do_identify(self, arg: str) -> None:
        """identify: Make selected badge(s) blink LED to identify themselves."""
        _ = arg
        self._print_response_rows(self.hub.identify())

    def do_start_mic(self, arg: str) -> None:
        """start_mic: Start microphone sampling."""
        _ = arg
        self._print_response_rows(self.hub.start_mic())

    def do_stop_mic(self, arg: str) -> None:
        """stop_mic: Stop microphone sampling."""
        _ = arg
        self._print_response_rows(self.hub.stop_mic())

    def do_start_imu(self, arg: str) -> None:
        """start_imu: Start IMU sampling."""
        _ = arg
        self._print_response_rows(self.hub.start_imu())

    def do_stop_imu(self, arg: str) -> None:
        """stop_imu: Stop IMU sampling."""
        _ = arg
        self._print_response_rows(self.hub.stop_imu())

    def do_start_scan(self, arg: str) -> None:
        """start_scan: Start BLE scan sensing."""
        _ = arg
        self._print_response_rows(self.hub.start_scan())

    def do_stop_scan(self, arg: str) -> None:
        """stop_scan: Stop BLE scan sensing."""
        _ = arg
        self._print_response_rows(self.hub.stop_scan())

    def do_start_all(self, arg: str) -> None:
        """start_all: Start all enabled sensors."""
        _ = arg
        self._print_response_rows(self.hub.start_all_sensors())

    def do_stop_all(self, arg: str) -> None:
        """stop_all: Stop all enabled sensors."""
        _ = arg
        self._print_response_rows(self.hub.stop_all_sensors())

    def do_sd_free_space(self, arg: str) -> None:
        """sd_free: Get SD free-space from selected badge(s)."""
        _ = arg
        self._print_response_rows(self.hub.sd_card_get_free_space())

    def do_sd_list(self, arg: str) -> None:
        """sd_list: List files on SD card for the selected badge."""
        _ = arg
        try:
            paths = self.hub.sd_card_list_files(log_list=False)
        except MidgeBadgeHubException as exc:
            print(exc)
            return
        if not paths:
            print("No files")
            return
        for path in paths:
            print(path)

    def do_sd_erase(self, arg: str) -> None:
        """sd_erase: Erase SD card on selected badge(s)."""
        _ = arg
        self._print_response_rows(self.hub.sd_card_erase())

    def do_sd_erase_file(self, arg: str) -> None:
        """sd_erase_file <path>: Erase a file on selected badge(s)."""
        args = self._split_args(arg)
        if len(args) != 1:
            print("usage: sd_erase_file <path>")
            return
        self._print_response_rows(self.hub.sd_card_erase_file(args[0]))

    def do_sd_erase_folder(self, arg: str) -> None:
        """sd_erase_folder <folder/>: Erase a folder recursively on selected badge(s)."""
        args = self._split_args(arg)
        if len(args) != 1:
            print("usage: sd_erase_folder <folder/>")
            return
        self._print_response_rows(self.hub.sd_card_erase_folder(args[0]))

    def do_watch_status_start(self, arg: str) -> None:
        """watch_status_start: Start background periodic status checks."""
        _ = arg

        def callback(results: list[GroupCommandExecResult]) -> None:
            print(f"\n[Status Check at {time_ns() // 1_000_000} ms]")
            self._print_response_rows(results)
            print(f"[End of Status Check]\n{self.prompt}", end="", flush=True)

        self.hub.start_repetitive_status_check(callback=callback)
        print("Started periodic status checks")

    def do_watch_status_stop(self, arg: str) -> None:
        """watch_status_stop: Stop background periodic status checks."""
        _ = arg
        self.hub.stop_repetitive_status_check()
        print("Stopped periodic status checks")

    def do_quit(self, arg: str) -> bool:
        """quit: Exit the CLI."""
        _ = arg
        return True

    def do_exit(self, arg: str) -> bool:
        """exit: Exit the CLI."""
        return self.do_quit(arg)

    def do_EOF(self, arg: str) -> bool:
        """Handle Ctrl-D to exit."""
        print()
        return self.do_quit(arg)

    def default(self, line: str) -> None:
        print(f"Unknown command: {line}")
        print("Type 'help' to list available commands")


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Midge badge experiment orchestration CLI")
    parser.add_argument("yaml_file", help="Path to experiment YAML file")
    parser.add_argument(
        "--run",
        help='Run one command non-interactively and exit (example: --run "list_badges")',
    )
    return parser


async def runner(cli: HubCLI) -> None:
    cli.cmdloop()


def main_sync() -> None:
    parser = _build_parser()
    args = parser.parse_args()
    yaml_path = pathlib.Path(args.yaml_file)
    cli = HubCLI(str(yaml_path))

    if args.run:
        cli.onecmd(args.run)
        return

    asyncio.run(runner(cli))


if __name__ == "__main__":
    main_sync()
