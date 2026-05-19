import asyncio
import cmd
import logging
import time
from ctypes import c_uint8
from threading import Thread

from midge_badge_framework.connection import MidgeBadgeClient
from midge_badge_framework.protocol import (
    INTERFACE_MAX_FILE_NAME,
    BadgeAssignment,
    BadgeID,
    CmdEraseFileRequest,
    CmdEraseSDRequest,
    CmdGetFileCRC32Request,
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
    get_bitfield_width,
)

DEFAULT_IMU_ACC_FSR = 4  # g
DEFAULT_IMU_GYR_FSR = 1000  # dps
DEFAULT_IMU_DATARATE = 50  # Hz
DEFAULT_AUDIO_HIGH_SAMPLE_RATE = 16000  # Hz
DEFAULT_AUDIO_LOW_SAMPLE_RATE_DECIMATION = 16  # divider applied to high sample rate

logging.basicConfig(level=logging.INFO)


class MidgeBadgeConsole(cmd.Cmd):
    def __init__(self):
        super().__init__()
        self.clients: list[MidgeBadgeClient] = []
        self.active_client = None

    def do_dummy(self, _):
        "dummy command"
        print("Dummy Command")

    def do_connect(self, arg):
        """
        Try to connect to a Midge Badge and select as active connection
        Usage:
            connect [any|<mac address>]

        if "any" is selected, the command will look for any Mingle Midge without an active
        connection. Otherwise, a mac address is expected
        """

        if arg == "":
            print("Error: Invalid syntax")
            return

        address = None if arg == "any" else arg
        client = MidgeBadgeClient(address)
        # Launch the client in a new thread
        Thread(target=lambda: asyncio.run(client.start()), daemon=True).start()
        # wait a bit to see if the connection was established
        time.sleep(4)

        if client.get_connected():
            print("Connection successfully started!")
            self.clients.append(client)
            self.active_client = client
        else:
            print("Error: Failed to establish connection")

        return

    def do_list(self, _):
        """
        List addresses of connected Mingle Midges
        """
        print(f"{len(self.clients)} active client(s)")
        for c in self.clients:
            print(f"- {c.get_address()}")

    def do_select(self, arg):
        """
        Select which MidgeBadge to communicate with
        Usage:
            select <address>
        """
        # TODO: Logic for all
        if arg != "":
            for c in self.clients:
                if c.get_address() == arg:
                    self.active_client = c
                    print("Found valid connection, changing selected")
                    return
            print("Error: Unable to find such device")
        else:
            print("Error: Invalid syntax, please provide an address")
            return

    def __common_cmd_execute(self, request: MidgeBadgeCommand):
        """
        Common utility to send commands to the active Midge Badge, request must be already initialized
        """
        if self.active_client is None:
            print("Error: No selected client")
        elif not self.active_client.get_connected():
            print("Error: Connection of selected client was closed")
            self.clients.remove(self.active_client)
            if self.clients != []:
                self.active_client = self.clients[0]
                print(f"Switched active client to {self.active_client.address}")
            else:
                print("Warning: No active clients, connect to a new client before sending another command")

        else:
            self.active_client.execute_command_log_resp(request)

    def do_setup_experiment(self, arg):
        """
        Issue the "setup-experiment" command to the active Midge Badge, which
        initializes the badge's storage for a new experiment and assigns it a badge ID (group + badge number)

        Usage:
            setup_experiment <group_id> <badge_id> <experiment_id>

        Note: group_id, badge_id and experiment_id must be numbers.
              group_id must be between 0 and 15, badge_id must be between 0 and 4095
        """
        args = arg.split()
        if len(args) != 3:
            print("Error: Invalid syntax, expected 3 arguments")
            return
        try:
            group_id = int(args[0], base=0)
            badge_id = int(args[1], base=0)
            experiment_id = int(args[2], base=0)
        except Exception as _:
            print("Error: Invalid syntax, arguments must be numbers")
            return

        group_id_max = 2 ** get_bitfield_width(BadgeID, "group") - 1
        badge_id_max = 2 ** get_bitfield_width(BadgeID, "badge") - 1

        if group_id < 0 or group_id > group_id_max:
            print(f"Error: Invalid group_id, must be between 0 and {group_id_max}")
            return
        if badge_id < 0 or badge_id > badge_id_max:
            print(f"Error: Invalid badge_id, must be between 0 and {badge_id_max}")
            return

        badge_assignment = BadgeAssignment()
        badge_assignment.id.group = group_id
        badge_assignment.id.badge = badge_id

        request = CmdSetupExperimentRequest(badge_assignment, experiment_id)
        self.__common_cmd_execute(request)

    def do_status(self, _):
        """
        Issue the "status" command
        Usage:
            status
        """

        timestamp_ms = time.time_ns() // 1_000_000
        request = CmdStatusRequest(timestamp_ms)
        self.__common_cmd_execute(request)

    def do_get_fw_version(self, _):
        """
        Issue a "get-fw-version" command that requests the Midge Badge to send back its firmware version string
        Usage:
            get_fw_version
        """
        request = CmdGetFWVersionRequest()
        self.__common_cmd_execute(request)

    def do_reset(self, _):
        """
        Issue a "reset" command that tells the active Midge Badge to reset
        Usage:
            reset
        """
        request = CmdResetRequest()
        self.__common_cmd_execute(request)

    def do_identify(self, _):
        """
        Issue an "identify" command that tells the active Midge Badge to blink its LED
        Usage:
            identify
        """
        request = CmdIdentifyRequest()
        self.__common_cmd_execute(request)

    def do_start_mic(self, arg):
        """
        Issue a "start-mic" command that tells the active Midge Badge to start sampling audio
        Usage:
            start_mic <sample_id> <mode>
        """
        args = arg.split()
        try:
            id = int(args[0], 0)
            mode = int(args[1], 0)
        except Exception as _:
            print("Error: Invalid syntax, <sample_id> and <mode> must be numbers")
            return

        request = CmdStartMicRequest(id, DEFAULT_AUDIO_HIGH_SAMPLE_RATE, DEFAULT_AUDIO_LOW_SAMPLE_RATE_DECIMATION, mode)
        self.__common_cmd_execute(request)

    def do_stop_mic(self, _):
        """
        Issue a "stop-mic" command that tells the active Midge Badge to stop sampling audio
        Usage:
            stop_mic
        """
        request = CmdStopMicRequest()
        self.__common_cmd_execute(request)

    def do_start_scan(self, arg):
        """
        Issue a "start-scan" command that tells the active Midge Badge to start scanning for other
        nearby midges and record the signal strength (rssi) to estimate proximity
        Usage:
            start_scan <sample_id>
        """
        try:
            print("TODO other args")
            id = int(arg, 0)
        except Exception as _:
            print("Error: Invalid syntax, <sample_id> must be a number")
            return
        request = CmdStartScanRequest(id, 0x10, 0x10)
        self.__common_cmd_execute(request)

    def do_stop_scan(self, _):
        """
        Issue a "stop-scan" command that tells the active Midge Badge to stop scanning for other
        nearby midges.
        Usage:
            stop_scan
        """
        request = CmdStopScanRequest()
        self.__common_cmd_execute(request)

    def do_start_imu(self, arg):
        """
        Issue a "start-imu" command that tells the active Midge Badge to start IMU sampling
        Usage:
            start_imu <sample_id> <acc_fsr> <gyr_fsr> <datarate>
        """
        args = arg.split()
        if len(args) != 4:
            print("Error: Invalid syntax, expected 4 arguments")
            return

        try:
            sample_id = int(args[0], 0)
            acc_fsr = int(args[1], 0)
            gyr_fsr = int(args[2], 0)
            datarate = int(args[3], 0)
        except Exception as _:
            print("Error: Invalid syntax, arguments must be numbers")
            return

        request = CmdStartIMURequest(sample_id, acc_fsr, gyr_fsr, datarate)
        self.__common_cmd_execute(request)

    def do_start_imu_default(self, arg):
        """
        Issue a "start-imu" command with default IMU settings
        Usage:
            start_imu_default <sample_id>
        Defaults:
            acc_fsr=4, gyr_fsr=1000, datarate=50
        """
        try:
            sample_id = int(arg, 0)
        except Exception as _:
            print("Error: Invalid syntax, <sample_id> must be a number")
            return

        request = CmdStartIMURequest(
            sample_id,
            DEFAULT_IMU_ACC_FSR,
            DEFAULT_IMU_GYR_FSR,
            DEFAULT_IMU_DATARATE,
        )
        self.__common_cmd_execute(request)

    def do_stop_imu(self, _):
        """
        Issue a "stop-imu" command that tells the active Midge Badge to stop IMU sampling
        Usage:
            stop_imu
        """
        request = CmdStopIMURequest()
        self.__common_cmd_execute(request)

    def do_erase_sd(self, _):
        """
        Issue a "erase-sd" command which will re-format the sd card in the Midge Badge
        Usage:
            erase_sd
        """
        request = CmdEraseSDRequest()
        self.__common_cmd_execute(request)

    def do_erase_file(self, arg):
        """
        Issue an "erase-file" command which will delete a file from the Midge Badge's sd card
        Usage:
            erase_file <path_on_badge>

        Note: <path_on_badge> should be the full path as listed by the "list_files" command, including the leading "/"
        """
        path = arg.strip()
        if path == "":
            print("Error: Invalid syntax, expected 1 argument")
            return

        if self.active_client is None:
            print("Error: No selected client")
            return

        path_bytes = path.encode("utf-8")
        if len(path_bytes) >= INTERFACE_MAX_FILE_NAME:
            print("Error: Path is too long")
            return
        path_type = c_uint8 * INTERFACE_MAX_FILE_NAME
        path_bytes = path_type(*path_bytes)
        request = CmdEraseFileRequest(path_bytes)
        self.active_client.execute_command_log_resp(request)

    def do_erase_folder(self, arg):
        """
        Issue an "erase-folder" command which will delete a folder from the Midge Badge's sd card
        Usage:
            erase_folder <path_on_badge>

        Note: <path_on_badge> should be the full path as listed by the "list_files" command, including the leading "/"
        """
        path = arg.strip()
        if path == "":
            print("Error: Invalid syntax, expected 1 argument")
            return

        if self.active_client is None:
            print("Error: No selected client")
            return

        path_bytes = path.encode("utf-8")
        if len(path_bytes) >= INTERFACE_MAX_FILE_NAME:
            print("Error: Path is too long")
            return
        path_type = c_uint8 * INTERFACE_MAX_FILE_NAME
        path_bytes = path_type(*path_bytes)
        # verify this is a folder path
        if not path.endswith("/"):
            print("Error: Invalid syntax, <path_on_badge> must be a folder path (end with '/')")
            return

        # list all files, find those that start with the folder path, and issue erase file command for each of them
        files = self.active_client.list_files(log_list=False)
        for f in files:
            if f.startswith(path):
                print(f"Erasing {f}...")
                f_bytes = f.encode("utf-8")
                f_bytes = path_type(*f_bytes)
                request = CmdEraseFileRequest(f_bytes)
                self.active_client.execute_command_log_resp(request)
        print(f"Erasing folder {path}...")
        self.active_client.execute_command_log_resp(CmdEraseFileRequest(path_bytes))

    def do_get_free_sd_space(self, _):
        """
        Issue a "get-free-sd-space" command that requests the Midge Badge to send back the amount of free space on its
        sd card in bytes
        Usage:
            get_free_sd_space
        """
        request = CmdGetFreeSDSpaceRequest()
        self.__common_cmd_execute(request)

    def do_get_file_crc32(self, arg):
        """
        Issue a "get-file-crc32" command that requests the crc32 of a file stored on the active Midge Badge's sd card
        Usage:
            get_file_crc32 <path_on_badge>

        Note: <path_on_badge> should be the full path as listed by the "list_files" command, including the leading "/"
        """
        path = arg.strip()
        if path == "":
            print("Error: Invalid syntax, expected 1 argument")
            return

        if self.active_client is None:
            print("Error: No selected client")
            return

        path_bytes = path.encode("utf-8")
        if len(path_bytes) >= INTERFACE_MAX_FILE_NAME:
            print("Error: Path is too long")
            return
        path_type = c_uint8 * INTERFACE_MAX_FILE_NAME
        path_bytes = path_type(*path_bytes)
        request = CmdGetFileCRC32Request(path_bytes)
        self.active_client.execute_command_log_resp(request)

    def do_list_files(self, _):
        """
        Issue a series of "get-file-index-info" commands to get the list of files stored on the active Midge Badge's
        sd card
        Usage:
            list_files
        """
        if self.active_client is None:
            print("Error: No selected client")
            return
        if not self.active_client.get_connected():
            print("Error: Connection of selected client was closed")
            self.clients.remove(self.active_client)
            if self.clients != []:
                self.active_client = self.clients[0]
                print(f"Switched active client to {self.active_client.address}")
            else:
                print("Warning: No active clients, connect to a new client before sending another command")
                return

        self.active_client.list_files(log_list=True)

    def do_download_file(self, arg):
        """
        Issue a series of "get-file-chunk" commands to download a file stored on the active Midge Badge's sd card
        Usage:
            download_file <path_on_badge> <output_file>

        Note: <path_on_badge> should be the full path as listed by the "list_files" command, including the leading "/"
        """

        args = arg.split()
        if len(args) != 2:
            print("Error: Invalid syntax, expected 2 arguments")
            return

        path = args[0]
        outfile = args[1]

        if self.active_client is None:
            print("Error: No selected client")
            return

        self.active_client.download_file(path, outfile)

    def do_exit(self, _):
        "exit program"
        for client in self.clients:
            client.stop()
        exit()


async def main():
    MidgeBadgeConsole().cmdloop()


def main_sync():
    asyncio.run(main())


if __name__ == "__main__":
    main_sync()
