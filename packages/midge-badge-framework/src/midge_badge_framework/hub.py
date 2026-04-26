import asyncio
import logging
from collections.abc import Callable
from ctypes import c_uint8
from threading import Thread
from time import sleep, time_ns

from .connection import MidgeBadgeClient
from .protocol import (
    INTERFACE_MAX_FILE_NAME,
    BadgeAssignment,
    CmdEraseFileRequest,
    CmdEraseSDRequest,
    CmdGetFreeSDSpaceRequest,
    CmdGetFWVersionRequest,
    CmdSetupExperimentRequest,
    CmdSetupExperimentResponse,
    CmdStartIMURequest,
    CmdStartMicRequest,
    CmdStartScanRequest,
    CmdStatusRequest,
    CmdStatusResponse,
    CmdStopIMURequest,
    CmdStopMicRequest,
    CmdStopScanRequest,
    MidgeBadgeCommand,
)
from .schema import BadgeSchema, ExperimentSchema, GroupSchema

logger = logging.getLogger(__name__)


class MidgeBadgeHubException(Exception):
    def __init__(self, message: str):
        super().__init__(message)


class MidgeBadgeHubInitException(MidgeBadgeHubException):
    def __init__(self, message: str, trace: list[tuple[BadgeSchema, MidgeBadgeCommand]]):
        super().__init__(message)
        self.trace = trace


class MidgeBadgeCmdExecutionException(MidgeBadgeHubException):
    def __init__(self, message: str, badge: BadgeSchema, resp: MidgeBadgeCommand):
        super().__init__(message)
        self.badge = badge
        self.resp = resp


class MidgeBadgeHub:
    def __init__(self, experiment_yaml_path: str, battery_max_voltage_mv: int = 4200):
        self._experiment = ExperimentSchema.load_from_yaml(experiment_yaml_path)
        self._selected_badge: BadgeSchema | None = None
        self._status_check_repeat: bool = False
        self._sample_id_counter: int = 0

        init_trace = []
        for group in self._experiment.groups:
            for badge in group.badges:
                logger.debug(
                    "Setting up badge %s (%s) in group %s",
                    badge.name,
                    badge.mac,
                    group.name,
                )
                badge_assignment = BadgeAssignment()
                badge_assignment.id.group = group.id
                badge_assignment.id.badge = badge.id
                # setup experiment folder
                cmd0 = CmdSetupExperimentRequest(badge_assignment, self._experiment.id)
                resp: CmdSetupExperimentResponse = MidgeBadgeHub.__execute_cmd(badge, cmd0)
                init_trace.append((badge, resp))
                if resp.status_code != 0:
                    msg = (
                        f'Failed to setup badge: "{badge.name}" addr: ({badge.mac})'
                        f' - Group "{group.name}": {resp.status_code}'
                    )
                    logger.error(msg)
                    raise MidgeBadgeHubInitException(msg, init_trace)

                # first status check
                timestamp_ms = time_ns() // 1_000_000
                cmd1 = CmdStatusRequest(timestamp_ms)
                resp: CmdStatusResponse = MidgeBadgeHub.__execute_cmd(badge, cmd1)
                init_trace.append((badge, resp))
                # TODO: Check individual status flags to verify the badge
                # initialization was succesfull, i.e. all status symbolize a
                # good, idle badge.
                if resp.battery_millivolts < battery_max_voltage_mv * 0.9:
                    msg = (
                        f"Badge {badge.name} ({badge.mac}) in group {group.name} "
                        f"has low battery: {resp.battery_millivolts} mV"
                    )
                    logger.warning(msg)

    def __execute_cmd(badge: BadgeSchema, cmd: MidgeBadgeCommand) -> MidgeBadgeCommand:
        client = MidgeBadgeClient(badge.mac)
        thread = Thread(target=lambda: asyncio.run(client.start()), daemon=True)
        thread.start()
        timeout_seconds = 1
        start = time_ns()
        while not client.get_connected() and ((time_ns() - start) / 1_000_000_000) < timeout_seconds:
            sleep(0.05)
        if not client.get_connected():
            msg = f"Failed to connect to badge {badge.name} ({badge.mac})"
            logger.error(msg)
            # Return error response on connection failure
            return CmdStatusResponse(status_code=-1, battery_millivolts=0, free_sd_kilobytes=0)
        client.send_command(cmd)
        resp = client.get_response()
        client.stop()  # Stop the client after getting the response
        thread.join()  # Wait for the thread to finish
        return resp

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
        self,
        cmd: MidgeBadgeCommand,
        cmd_preprocess: Callable[[MidgeBadgeCommand], MidgeBadgeCommand] = lambda x: x,
        filter: Callable[[GroupSchema, BadgeSchema], bool] = lambda _group, _badge: True,
    ) -> list[tuple[BadgeSchema, MidgeBadgeCommand]]:
        responses = []
        if self._selected_badge is not None:
            resp = MidgeBadgeHub.__execute_cmd(self._selected_badge, cmd_preprocess(cmd))
            responses.append((self._selected_badge, resp))
        else:
            for group in self._experiment.groups:
                for badge in group.badges:
                    if filter(group, badge):
                        resp = MidgeBadgeHub.__execute_cmd(badge, cmd_preprocess(cmd))
                        responses.append((badge, resp))
        return responses

    def start_mic(self) -> list[tuple[BadgeSchema, MidgeBadgeCommand]] | None:
        result: list[tuple[BadgeSchema, MidgeBadgeCommand]] | None = None
        if self._experiment.params.audio is not None:
            cmd: MidgeBadgeCommand = CmdStartMicRequest(
                self._sample_id_counter,
                self._experiment.params.audio.high_freq_hz,
                self._experiment.params.audio.low_freq_decimation,
                self._experiment.params.audio.channels,
            )
            result = self.execute_cmd(cmd)
        return result

    def stop_mic(self) -> list[tuple[BadgeSchema, MidgeBadgeCommand]] | None:
        result: list[tuple[BadgeSchema, MidgeBadgeCommand]] | None = None
        if self._experiment.params.audio is not None:
            cmd: MidgeBadgeCommand = CmdStopMicRequest()
            result = self.execute_cmd(cmd)
        return result

    def start_imu(self) -> list[tuple[BadgeSchema, MidgeBadgeCommand]] | None:
        result: list[tuple[BadgeSchema, MidgeBadgeCommand]] | None = None
        if self._experiment.params.imu is not None:
            cmd = CmdStartIMURequest(
                self._sample_id_counter,
                self._experiment.params.imu.accel_range_g,
                self._experiment.params.imu.gyro_range_dps,
                self._experiment.params.imu.sample_rate_hz,
            )
            result = self.execute_cmd(cmd)
        return result

    def stop_imu(self) -> list[tuple[BadgeSchema, MidgeBadgeCommand]] | None:
        result: list[tuple[BadgeSchema, MidgeBadgeCommand]] | None = None
        if self._experiment.params.imu is not None:
            cmd = CmdStopIMURequest()
            result = self.execute_cmd(cmd)
        return result

    def start_scan(self) -> list[tuple[BadgeSchema, MidgeBadgeCommand]] | None:
        result: list[tuple[BadgeSchema, MidgeBadgeCommand]] | None = None
        if self._experiment.params.scan is not None:
            cmd = CmdStartScanRequest(
                self._sample_id_counter,
                self._experiment.params.scan.window,
                self._experiment.params.scan.interval,
                0,
            )
            result = self.execute_cmd(cmd)
        return result

    def stop_scan(self) -> list[tuple[BadgeSchema, MidgeBadgeCommand]] | None:
        result: list[tuple[BadgeSchema, MidgeBadgeCommand]] | None = None
        if self._experiment.params.scan is not None:
            cmd = CmdStopScanRequest()
            result = self.execute_cmd(cmd)
        return result

    def start_all_sensors(self) -> list[tuple[BadgeSchema, MidgeBadgeCommand]]:
        result = []
        audio_resp: list[tuple[BadgeSchema, MidgeBadgeCommand]] | None = self.start_mic()
        if audio_resp is not None:
            result.extend(audio_resp)
        imu_resp: list[tuple[BadgeSchema, MidgeBadgeCommand]] | None = self.start_imu()
        if imu_resp is not None:
            result.extend(imu_resp)
        scan_resp: list[tuple[BadgeSchema, MidgeBadgeCommand]] | None = self.start_scan()
        if scan_resp is not None:
            result.extend(scan_resp)
        return result

    def stop_all_sensors(self) -> list[tuple[BadgeSchema, MidgeBadgeCommand]]:
        result = []
        audio_resp: list[tuple[BadgeSchema, MidgeBadgeCommand]] | None = self.stop_mic()
        if audio_resp is not None:
            result.extend(audio_resp)
        imu_resp: list[tuple[BadgeSchema, MidgeBadgeCommand]] | None = self.stop_imu()
        if imu_resp is not None:
            result.extend(imu_resp)
        scan_resp: list[tuple[BadgeSchema, MidgeBadgeCommand]] | None = self.stop_scan()
        if scan_resp is not None:
            result.extend(scan_resp)
        return result

    def get_fw_version(self) -> list[tuple[BadgeSchema, MidgeBadgeCommand]]:
        cmd = CmdGetFWVersionRequest()
        return self.execute_cmd(cmd)

    def sd_card_get_free_space(self) -> list[tuple[BadgeSchema, MidgeBadgeCommand]]:
        cmd = CmdGetFreeSDSpaceRequest()
        return self.execute_cmd(cmd)

    def sd_card_erase(self) -> list[tuple[BadgeSchema, MidgeBadgeCommand]]:
        cmd = CmdEraseSDRequest()
        return self.execute_cmd(cmd)

    def sd_card_list_files(self, log_list: bool) -> list[str]:
        """
        Only allowed for a selected badge
        """
        if self._selected_badge is None:
            msg = "No badge selected for listing SD card files"
            logger.error(msg)
            raise MidgeBadgeHubException(msg)
        # Special execution case
        client = MidgeBadgeClient(self._selected_badge.mac)
        thread = Thread(target=lambda: asyncio.run(client.start()), daemon=True)
        thread.start()
        timeout_seconds = 2
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

    def sd_card_erase_file(self, file_name: str) -> list[tuple[BadgeSchema, MidgeBadgeCommand]]:
        if len(file_name) > INTERFACE_MAX_FILE_NAME:
            msg = f"File name too long: {file_name} (max {INTERFACE_MAX_FILE_NAME} characters)"
            logger.error(msg)

        path_bytes = file_name.encode("utf-8")
        path_type = c_uint8 * INTERFACE_MAX_FILE_NAME
        path_bytes = path_type(*path_bytes)
        cmd = CmdEraseFileRequest(path_bytes)
        return self.execute_cmd(cmd)

    def sd_card_erase_folder(self, folder_name: str) -> list[tuple[BadgeSchema, MidgeBadgeCommand]]:
        if len(folder_name) > INTERFACE_MAX_FILE_NAME:
            msg = f"Folder name too long: {folder_name} (max {INTERFACE_MAX_FILE_NAME} characters)"
            logger.error(msg)
        if not folder_name.endswith("/"):
            msg = f"Folder name must end with '/': {folder_name}"
            logger.error(msg)
            return []

        # Assume every badge has the same files as any other for multi-badge
        badge_selected: bool = self._selected_badge is not None
        if not badge_selected:
            logger.warning("No badge selected for erasing SD card folder, selecting first badge in experiment schema")
            self._selected_badge = self._experiment.groups[0].badges[0]
        files = self.sd_card_list_files(log_list=False)
        if not badge_selected:
            # reset prev state
            self._selected_badge = None
        result = []
        for file in files:
            if file.startswith(folder_name + "/"):
                result.extend(self.sd_card_erase_file(file))
        # execution
        path_bytes = folder_name.encode("utf-8")
        path_type = c_uint8 * INTERFACE_MAX_FILE_NAME
        path_bytes = path_type(*path_bytes)
        cmd_folder = CmdEraseFileRequest(path_bytes)
        result.extend(self.execute_cmd(cmd_folder))
        return result

    def start_repetitive_status_check(self):
        def __update_status_timestamp(cmd: CmdStatusRequest) -> CmdStatusRequest:
            timestamp_ms = time_ns() // 1_000_000
            cmd.millis_since_epoch = timestamp_ms
            return cmd

        def status_check_loop():
            while self._status_check_repeat:
                cmd = CmdStatusRequest(0)
                self.execute_cmd(cmd, cmd_preprocess=__update_status_timestamp)
                sleep(5)  # Check status every 5 seconds

        self._status_check_repeat = True
        Thread(target=status_check_loop, daemon=True).start()
        logger.info("Started repetitive status check")

    def stop_repetitive_status_check(self):
        self._status_check_repeat = False
        logger.info("Stopped repetitive status check")

    def select_badge_by_name(self, name: str):
        for group in self._experiment.groups:
            for badge in group.badges:
                if badge.name == name:
                    self._selected_badge = badge
                    m = f"Selected badge {badge.name} ({badge.mac}) in group {group.name}"
                    logger.info(m)
                    return True
        return False

    def select_badge_by_mac(self, mac: str):
        for group in self._experiment.groups:
            for badge in group.badges:
                if badge.mac == mac:
                    self._selected_badge = badge
                    m = f"Selected badge {badge.name} ({badge.mac}) in group {group.name}"
                    logger.info(m)
                    return True
        return False

    def unselect_badge(self):
        self._selected_badge = None
