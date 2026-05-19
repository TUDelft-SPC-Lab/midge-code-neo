import asyncio
import logging
from collections.abc import Callable
from ctypes import c_uint8
from dataclasses import dataclass
from threading import Lock, Thread
from time import sleep, time_ns

from pydantic import BaseModel, ConfigDict

from .connection import MidgeBadgeClient
from .protocol import (
    INTERFACE_MAX_FILE_NAME,
    BadgeAssignment,
    CmdEraseFileRequest,
    CmdEraseSDRequest,
    CmdGetFreeSDSpaceRequest,
    CmdGetFWVersionRequest,
    CmdIdentifyRequest,
    CmdResetRequest,
    CmdSetupExperimentRequest,
    CmdStartIMURequest,
    CmdStartMicRequest,
    CmdStartScanRequest,
    CmdStatusRequest,
    CmdStopIMURequest,
    CmdStopMicRequest,
    CmdStopScanRequest,
    MidgeBadgeCommand,
)
from .schema import BadgeSchema, ExperimentSchema, GroupSchema

logger = logging.getLogger(__name__)
CONNECTION_TIMEOUT_SECONDS = 10


class MidgeBadgeHubException(Exception):
    def __init__(self, message: str):
        super().__init__(message)


class MidgeBadgeHubInitException(MidgeBadgeHubException):
    def __init__(self, message: str, trace: list[BadgeCmdExecResult]):
        super().__init__(message)
        self.trace = trace


class MidgeBadgeCmdExecutionException(MidgeBadgeHubException):
    def __init__(self, message: str, badge: BadgeSchema, resp: MidgeBadgeCommand):
        super().__init__(message)
        self.badge = badge
        self.resp = resp


CommandPreprocessFunc = Callable[[MidgeBadgeCommand], MidgeBadgeCommand]


@dataclass
class BadgeCmdExecResult:
    badge: BadgeSchema
    responses: list[MidgeBadgeCommand]


@dataclass
class GroupCommandExecResult:
    group: GroupSchema
    badge_results: list[BadgeCmdExecResult]


class CommandEntry(BaseModel):
    model_config = ConfigDict(arbitrary_types_allowed=True)

    cmd: MidgeBadgeCommand
    preprocess_func: CommandPreprocessFunc = lambda cmd: cmd


def _status_timestamp_update(cmd: CmdStatusRequest) -> CmdStatusRequest:
    timestamp_ms = time_ns() // 1_000_000
    cmd.millis_since_epoch = timestamp_ms
    return cmd


class MidgeBadgeHub:
    def __init__(self, experiment_yaml_path: str, battery_max_voltage_mv: int = 4200):
        self._experiment = ExperimentSchema.load_from_yaml(experiment_yaml_path)
        self._selected_badge: BadgeSchema | None = None
        self._selected_group: GroupSchema | None = None  # only valid if a badge is selected, otherwise None
        self._status_check_repeat: bool = False
        self._sample_id_counter: int = 0
        self.battery_max_voltage_mv = battery_max_voltage_mv
        self._client_session_lock = Lock()

    def init_experiment(self) -> None:
        init_trace = []
        for group in self._experiment.groups:
            for badge in group.badges:
                msg = f"Setting up badge {badge.name} ({badge.mac}) in group {group.name}"
                logger.debug(msg)
                print("\r\033[K", msg, end="")
                badge_assignment = BadgeAssignment()
                badge_assignment.id.group = group.id
                badge_assignment.id.badge = badge.id
                # setup experiment folder
                cmd0 = CommandEntry(cmd=CmdSetupExperimentRequest(badge_assignment, self._experiment.id))
                # first status check
                cmd1 = CommandEntry(cmd=CmdStatusRequest(), preprocess_func=_status_timestamp_update)
                ret = self.__execute_cmds(badge, [cmd0, cmd1])
                # verify responses are not empty
                if ret.responses == []:
                    msg = f'No response from badge: {badge.name} addr: ({badge.mac}) - Group "{group.name}"'
                    logger.error(msg)
                    print("\r\033[K", end="")
                    raise MidgeBadgeHubInitException(msg, init_trace)
                # verify correct setup
                init_trace.append(ret)
                if ret.responses[0].status_code != 0:
                    msg = (
                        f'Failed to setup badge: "{badge.name}" addr: ({badge.mac})'
                        f' - Group "{group.name}": {ret.responses[0].status_code}'
                    )
                    logger.error(msg)
                    print("\r\033[K", end="")
                    raise MidgeBadgeHubInitException(msg, init_trace)
                # TODO: Check individual status flags to verify the badge
                # initialization was succesfull, i.e. all status symbolize a
                # good, idle badge.
                if ret.responses[1].battery_millivolts < self.battery_max_voltage_mv * 0.9:
                    msg = (
                        f"Badge {badge.name} ({badge.mac}) in group {group.name} "
                        f"has low battery: {ret.responses[1].battery_millivolts} mV"
                    )
                    logger.warning(msg)
        print("\r\033[K", end="")

    def __execute_cmds(self, badge: BadgeSchema, cmds: list[CommandEntry]) -> BadgeCmdExecResult:
        with self._client_session_lock:
            mac = None if badge.mac == "any" else badge.mac
            reserved_macs = [b.mac for g in self._experiment.groups for b in g.badges if b.mac != "any"]
            client = MidgeBadgeClient(address=mac, reserved_macs=reserved_macs)
            thread = Thread(target=lambda: asyncio.run(client.start()), daemon=True)
            thread.start()
            try:
                timeout_seconds = CONNECTION_TIMEOUT_SECONDS
                start = time_ns()
                while not client.get_connected() and ((time_ns() - start) / 1_000_000_000) < timeout_seconds:
                    sleep(0.1)

                responses = []
                if not client.get_connected():
                    msg = f"Failed to connect to badge {badge.name} ({badge.mac})"
                    logger.error(msg)
                else:
                    if badge.mac == "any":
                        logger.info(
                            "Badge with MAC 'any' connected, updating badge schema with actual MAC: %s",
                            client.get_address(),
                        )
                        badge.mac = client.get_address()
                    for cmd_entry in cmds:
                        cmd_entry.cmd = cmd_entry.preprocess_func(cmd_entry.cmd)
                        print(
                            "\033[K",
                            f"Executing command for badge {badge.name} ({badge.mac}): {cmd_entry.cmd}",
                            end="\r",
                        )
                        client.send_command(cmd_entry.cmd)
                        resp = client.get_response()
                        responses.append(resp)
            finally:
                client.stop()  # Stop the client after getting the response
                thread.join()  # Wait for the thread to finish
            return BadgeCmdExecResult(badge=badge, responses=responses)

    def get_selected_badge(self) -> BadgeSchema | None:
        """
        Returns the currently selected badge, or None if no badge is selected
        (i.e. commands will be executed for all badges).
        """
        return self._selected_badge

    def get_schema(self) -> ExperimentSchema:
        """
        Returns the experiment schema. Can be used for rendering.
        """
        return self._experiment

    def execute_cmd(
        self, cmd: CommandEntry, filter: Callable[[GroupSchema, BadgeSchema], bool] = lambda _group, _badge: True
    ) -> list[GroupCommandExecResult]:
        """
        Execute one command on the selected badge, or on all badges if none is selected.
        Returns grouped command execution results.
        """
        # TODO Encase all logic in groups, badges
        return self.execute_cmds([cmd], filter=filter)

    def execute_cmds(
        self,
        cmds: list[CommandEntry],
        filter: Callable[[GroupSchema, BadgeSchema], bool] = lambda _group, _badge: True,
    ) -> list[GroupCommandExecResult]:
        responses = []
        if self._selected_badge is not None:
            resp = [self.__execute_cmds(self._selected_badge, cmds)]
            responses.append(GroupCommandExecResult(group=self._selected_group, badge_results=resp))
        else:
            for group in self._experiment.groups:
                badge_results: list[BadgeCmdExecResult] = []
                for badge in group.badges:
                    if filter(group, badge):
                        resp = self.__execute_cmds(badge, cmds)
                        badge_results.append(resp)
                responses.append(GroupCommandExecResult(group=group, badge_results=badge_results))
        print("\r\033[K", end="")
        return responses

    def get_status(self) -> list[GroupCommandExecResult]:
        cmd_obj = CommandEntry(cmd=CmdStatusRequest(), preprocess_func=_status_timestamp_update)

        return self.execute_cmd(cmd_obj)

    def start_mic(self) -> list[GroupCommandExecResult] | None:
        result: list[GroupCommandExecResult] | None = None
        if self._experiment.params.audio is not None:
            cmd: MidgeBadgeCommand = CmdStartMicRequest(
                self._sample_id_counter,
                self._experiment.params.audio.high_freq_hz,
                self._experiment.params.audio.low_freq_decimation,
                self._experiment.params.audio.channels,
            )
            result = self.execute_cmd(CommandEntry(cmd=cmd))
        return result

    def stop_mic(self) -> list[GroupCommandExecResult] | None:
        result: list[GroupCommandExecResult] | None = None
        if self._experiment.params.audio is not None:
            cmd: MidgeBadgeCommand = CmdStopMicRequest()
            result = self.execute_cmd(CommandEntry(cmd=cmd))
        return result

    def start_imu(self) -> list[GroupCommandExecResult] | None:
        result: list[GroupCommandExecResult] | None = None
        if self._experiment.params.imu is not None:
            cmd = CmdStartIMURequest(
                self._sample_id_counter,
                self._experiment.params.imu.accel_range_g,
                self._experiment.params.imu.gyro_range_dps,
                self._experiment.params.imu.sample_rate_hz,
            )
            result = self.execute_cmd(CommandEntry(cmd=cmd))
        return result

    def stop_imu(self) -> list[GroupCommandExecResult] | None:
        result: list[GroupCommandExecResult] | None = None
        if self._experiment.params.imu is not None:
            cmd = CmdStopIMURequest()
            result = self.execute_cmd(CommandEntry(cmd=cmd))
        return result

    def start_scan(self) -> list[GroupCommandExecResult] | None:
        result: list[GroupCommandExecResult] | None = None
        if self._experiment.params.scan is not None:
            cmd = CmdStartScanRequest(
                self._sample_id_counter,
                self._experiment.params.scan.window,
                self._experiment.params.scan.interval,
                0,
            )
            result = self.execute_cmd(CommandEntry(cmd=cmd))
        return result

    def stop_scan(self) -> list[GroupCommandExecResult] | None:
        result: list[GroupCommandExecResult] | None = None
        if self._experiment.params.scan is not None:
            cmd = CmdStopScanRequest()
            result = self.execute_cmd(CommandEntry(cmd=cmd))
        return result

    def start_all_sensors(self) -> list[GroupCommandExecResult]:
        cmds = []
        if self._experiment.params.audio is not None:
            cmd_audio = CommandEntry(
                cmd=CmdStartMicRequest(
                    self._sample_id_counter,
                    self._experiment.params.audio.high_freq_hz,
                    self._experiment.params.audio.low_freq_decimation,
                    self._experiment.params.audio.channels,
                )
            )
            cmds.append(cmd_audio)
        if self._experiment.params.imu is not None:
            cmd_imu = CommandEntry(
                cmd=CmdStartIMURequest(
                    self._sample_id_counter,
                    self._experiment.params.imu.accel_range_g,
                    self._experiment.params.imu.gyro_range_dps,
                    self._experiment.params.imu.sample_rate_hz,
                )
            )
            cmds.append(cmd_imu)
        if self._experiment.params.scan is not None:
            cmd_scan = CommandEntry(
                cmd=CmdStartScanRequest(
                    self._sample_id_counter,
                    self._experiment.params.scan.window,
                    self._experiment.params.scan.interval,
                    0,
                )
            )
            cmds.append(cmd_scan)
        return self.execute_cmds(cmds)

    def stop_all_sensors(self) -> list[GroupCommandExecResult]:
        cmd_audio = CommandEntry(cmd=CmdStopMicRequest())
        cmd_imu = CommandEntry(cmd=CmdStopIMURequest())
        cmd_scan = CommandEntry(cmd=CmdStopScanRequest())
        cmds = []
        if self._experiment.params.audio is not None:
            cmds.append(cmd_audio)
        if self._experiment.params.imu is not None:
            cmds.append(cmd_imu)
        if self._experiment.params.scan is not None:
            cmds.append(cmd_scan)
        return self.execute_cmds(cmds)

    def get_fw_version(self) -> list[GroupCommandExecResult]:
        cmd = CmdGetFWVersionRequest()
        return self.execute_cmd(CommandEntry(cmd=cmd))

    def reset(self) -> list[GroupCommandExecResult]:
        cmd = CmdResetRequest()
        return self.execute_cmd(CommandEntry(cmd=cmd))

    def identify(self) -> list[GroupCommandExecResult]:
        cmd = CmdIdentifyRequest()
        return self.execute_cmd(CommandEntry(cmd=cmd))

    def sd_card_get_free_space(self) -> list[GroupCommandExecResult]:
        cmd = CmdGetFreeSDSpaceRequest()
        return self.execute_cmd(CommandEntry(cmd=cmd))

    def sd_card_erase(self) -> list[GroupCommandExecResult]:
        cmd = CmdEraseSDRequest()
        return self.execute_cmd(CommandEntry(cmd=cmd))

    def sd_card_list_files(self, log_list: bool) -> list[str]:
        """
        Only allowed for a selected badge
        """
        if self._selected_badge is None:
            msg = "No badge selected for listing SD card files"
            logger.error(msg)
            raise MidgeBadgeHubException(msg)
        with self._client_session_lock:
            # Special execution case
            client = MidgeBadgeClient(self._selected_badge.mac)
            thread = Thread(target=lambda: asyncio.run(client.start()), daemon=True)
            thread.start()
            timeout_seconds = CONNECTION_TIMEOUT_SECONDS
            start = time_ns()
            while not client.get_connected() and ((time_ns() - start) / 1_000_000_000) < timeout_seconds:
                sleep(0.05)
            if not client.get_connected():
                msg = f"Failed to connect to badge {self._selected_badge.name} ({self._selected_badge.mac})"
                raise MidgeBadgeHubException(msg)
            files = client.list_files(log_list=log_list)
            client.stop()  # Stop the client after getting the response
            thread.join()  # Wait for the thread to finish
            return files

    def sd_card_erase_file(self, file_name: str) -> list[GroupCommandExecResult]:
        if len(file_name) > (INTERFACE_MAX_FILE_NAME - 1):
            msg = f"File name too long: {file_name} (max {INTERFACE_MAX_FILE_NAME - 1} characters)"
            logger.error(msg)
            return []

        path_bytes = file_name.encode("utf-8")
        path_type = c_uint8 * INTERFACE_MAX_FILE_NAME
        path_bytes = path_type(*path_bytes)
        cmd = CmdEraseFileRequest(path_bytes)
        return self.execute_cmd(CommandEntry(cmd=cmd))

    def sd_card_erase_folder(self, folder_name: str) -> list[GroupCommandExecResult]:
        if len(folder_name) > (INTERFACE_MAX_FILE_NAME - 1):
            msg = f"Folder name too long: {folder_name} (max {INTERFACE_MAX_FILE_NAME - 1} characters)"
            logger.error(msg)
            return []
        if not folder_name.endswith("/"):
            msg = f"Folder name must end with '/': {folder_name}"
            logger.error(msg)
            return []

        # Assume every badge has the same files as any other for multi-badge
        badge_selected: bool = self._selected_badge is not None
        if not badge_selected:
            logger.warning(
                "No badge selected for erasing SD card folder, selecting first badge in experiment schema as ref"
            )
            self._selected_badge = self._experiment.groups[0].badges[0]
        files = self.sd_card_list_files(log_list=False)
        if not badge_selected:
            # reset prev state
            self._selected_badge = None
        cmds = []
        for file_name in files:
            if file_name.startswith(folder_name):
                path_bytes = file_name.encode("utf-8")
                path_type = c_uint8 * INTERFACE_MAX_FILE_NAME
                path_bytes = path_type(*path_bytes)
                cmd_entry = CommandEntry(cmd=CmdEraseFileRequest(path_bytes))
                cmds.append(cmd_entry)
        # execution
        path_bytes = folder_name.encode("utf-8")
        path_type = c_uint8 * INTERFACE_MAX_FILE_NAME
        path_bytes = path_type(*path_bytes)
        cmd_folder = CommandEntry(cmd=CmdEraseFileRequest(path_bytes))
        cmds.append(cmd_folder)
        return self.execute_cmds(cmds)

    def start_repetitive_status_check(self, callback: Callable[[list[GroupCommandExecResult]], None]):
        def status_check_loop():
            while self._status_check_repeat:
                cmd = CmdStatusRequest()
                res = self.execute_cmd(CommandEntry(cmd=cmd, preprocess_func=_status_timestamp_update))
                callback(res)
                sleep(15)  # Check status every 15 seconds

        self._status_check_repeat = True
        Thread(target=status_check_loop, daemon=True).start()
        logger.info("Started repetitive status check")

    def stop_repetitive_status_check(self):
        self._status_check_repeat = False
        logger.info("Stopped repetitive status check")

    def select_badge_by_name(self, name: str) -> bool:
        for group in self._experiment.groups:
            for badge in group.badges:
                if badge.name == name:
                    self._selected_badge = badge
                    self._selected_group = group
                    m = f"Selected badge {badge.name} ({badge.mac}) in group {group.name}"
                    logger.info(m)
                    return True
        return False

    def select_badge_by_mac(self, mac: str) -> bool:
        for group in self._experiment.groups:
            for badge in group.badges:
                if badge.mac == mac:
                    self._selected_badge = badge
                    self._selected_group = group
                    m = f"Selected badge {badge.name} ({badge.mac}) in group {group.name}"
                    logger.info(m)
                    return True
        return False

    def unselect_badge(self):
        self._selected_badge = None
        self._selected_group = None
