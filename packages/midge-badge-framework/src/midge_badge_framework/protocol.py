from abc import abstractmethod
from ctypes import Structure, Union, c_int16, c_int32, c_int64, c_uint8, c_uint16, c_uint32, c_uint64
from enum import IntEnum

# analogue of midge_protocol.h

INTERFACE_CMD_DATA_SZ = 512 + 4
INTERFACE_CMD_SZ = INTERFACE_CMD_DATA_SZ + 3
INTERFACE_MAX_FILE_NAME = 12 * 3  # 3 levels of depth, i.e. SD/folder/file, 8.3 names


class MidgeBadgeCommandID(IntEnum):
    CMD_ID_SETUP_EXPERIMENT = ord("A")
    CMD_ID_STATUS = ord("B")
    CMD_ID_GET_FW_VERSION = ord("C")
    CMD_ID_START_MIC = ord("D")
    CMD_ID_STOP_MIC = ord("E")
    CMD_ID_START_SCAN = ord("F")
    CMD_ID_STOP_SCAN = ord("G")
    CMD_ID_START_IMU = ord("H")
    CMD_ID_STOP_IMU = ord("I")
    CMD_ID_ERASE_SD = ord("J")
    CMD_ID_GET_FREE_SD_SPACE = ord("K")
    CMD_ID_GET_FILE_INDEX_INFO = ord("L")
    CMD_ID_GET_FILE_CRC32 = ord("M")
    CMD_ID_DOWNLOAD_FILE_CHUNK = ord("N")


def mingle_midge_batt_mv_to_percent(mv):
    # fmt: off
    v_percentage_vector = [
        0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 4, 4,
        4, 4, 4, 4, 5, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 11, 12, 13, 13, 14, 15, 16, 18, 19, 22, 25, 28, 32, 36, 40,
        44, 47, 51, 53, 56, 58, 60, 62, 64, 66, 67, 69, 71, 72, 74, 76, 77, 79, 81, 82, 84, 85, 85, 86, 86, 86, 87, 88,
        88, 89, 90, 91, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 100,
    ]
    # fmt: on
    batt_meas_low_mv = 3000
    # batt_meas_full_mv = 4200
    batt_meas_mv_to_soc_delta = 11
    batt_meas_mv_to_soc_elements = 111
    vector_index = int((mv - batt_meas_low_mv) / batt_meas_mv_to_soc_delta)
    if vector_index < 0:
        vector_index = 0
    elif vector_index > (batt_meas_mv_to_soc_elements - 1):
        vector_index = batt_meas_mv_to_soc_elements - 1

    return v_percentage_vector[vector_index]


def get_bitfield_width(struct_cls, name):
    for fname, _, *rest in struct_cls._fields_:
        if fname == name:
            # rest will contain the width if it s a bit-field
            return rest[0] if rest else None
    raise KeyError(name)


class MidgeBadgeCommand(Structure):
    @abstractmethod
    def id() -> int:
        pass

    def __str__(self):
        ret = f"{self.__class__.__name__} : "
        for field in self._fields_:
            if field[0] == "battery_millivolts":
                mv = int(getattr(self, field[0]))
                # future proof: Based on the midge badge version, select a
                # correct mv to battery percentage function
                batt_percent = mingle_midge_batt_mv_to_percent(mv)
                ret += f"[battery = {batt_percent} %]"
            elif field[0] == "version_str":
                version_str = bytes(getattr(self, field[0])).decode("utf-8")
                ret += f"[fw_version = {version_str}]"
            else:
                ret += f"[{field[0]} = {getattr(self, field[0])}]"
        return ret


class BadgeID(Structure):
    _fields_ = [
        ("group", c_uint16, 4),  # 4-bit field
        ("badge", c_uint16, 12),  # 12-bit field
    ]

    def __str__(self):
        ret = f"{self.__class__.__name__} : "
        for field in self._fields_:
            ret += f"[{field[0]} = {getattr(self, field[0])}]"
        return ret


class BadgeAssignment(Union):
    _fields_ = [
        ("id", BadgeID),
        ("u16_all", c_uint16),
    ]

    def __str__(self):
        ret = f"{self.__class__.__name__} : "
        for field in self._fields_:
            ret += f"[{field[0]} = {getattr(self, field[0])}]"
        return ret


class CustomAdvertisementData(Structure):
    _pack_ = 1
    _fields_ = [
        ("battery_mv", c_uint16),
        ("active_sensor_bitflags", c_uint16),
        ("badge_assignment", BadgeAssignment),
    ]

    def __str__(self):
        ret = f"{self.__class__.__name__} : "
        for field in self._fields_:
            ret += f"[{field[0]} = {getattr(self, field[0])}]"
        return ret


class CmdSetupExperimentRequest(Structure):
    _pack_ = 1
    _fields_ = [
        ("badge_assignment", BadgeAssignment),
        ("experiment_id", c_uint16),
    ]

    def id() -> int:
        return MidgeBadgeCommandID.CMD_ID_SETUP_EXPERIMENT


class CmdSetupExperimentResponse(Structure):
    _pack_ = 1
    _fields_ = [("status_code", c_uint8)]

    def id() -> int:
        return MidgeBadgeCommandID.CMD_ID_SETUP_EXPERIMENT


class CmdStatusRequest(MidgeBadgeCommand):
    _pack_ = 1
    _fields_ = [("millis_since_epoch", c_uint64)]

    def id() -> int:
        return MidgeBadgeCommandID.CMD_ID_STATUS


class CmdStatusResponse(MidgeBadgeCommand):
    _pack_ = 1
    _fields_ = [
        ("sync_status", c_uint8),
        ("storage_init_status", c_uint8),
        ("audio_init_status", c_uint8),
        ("proximity_init_status", c_uint8),
        ("battery_millivolts", c_int16),
        ("badge_assignment", BadgeAssignment),
        ("sync_error_ms", c_int64),  # ref - interp
    ]

    def id() -> int:
        return MidgeBadgeCommandID.CMD_ID_STATUS


class CmdGetFWVersionRequest(MidgeBadgeCommand):
    _pack_ = 1
    _fields_ = [("reserved", c_uint16)]

    def id() -> int:
        return MidgeBadgeCommandID.CMD_ID_GET_FW_VERSION


class CmdGetFWVersionResponse(MidgeBadgeCommand):
    _pack_ = 1
    _fields_ = [("version_str", c_uint8 * 32)]

    def id() -> int:
        return MidgeBadgeCommandID.CMD_ID_GET_FW_VERSION


class CmdStartMicRequest(MidgeBadgeCommand):
    _pack_ = 1
    _fields_ = [
        ("sample_id", c_uint16),
        ("high_sample_rate", c_uint16),
        ("low_sample_rate_decimation", c_uint16),
        ("mode", c_uint8),
    ]

    def id() -> int:
        return MidgeBadgeCommandID.CMD_ID_START_MIC


class CmdStartMicResponse(MidgeBadgeCommand):
    _pack_ = 1
    _fields_ = [("status_code", c_int32)]

    def id() -> int:
        return MidgeBadgeCommandID.CMD_ID_START_MIC


class CmdStopMicRequest(MidgeBadgeCommand):
    _pack_ = 1
    _fields_ = [("reserved", c_uint16)]

    def id() -> int:
        return MidgeBadgeCommandID.CMD_ID_STOP_MIC


class CmdStopMicResponse(MidgeBadgeCommand):
    _pack_ = 1
    _fields_ = [("reserved", c_int32)]

    def id() -> int:
        return MidgeBadgeCommandID.CMD_ID_STOP_MIC


class CmdStartScanRequest(MidgeBadgeCommand):
    _pack_ = 1
    _fields_ = [("sample_id", c_uint16), ("window", c_uint16), ("interval", c_uint16), ("reserved", c_uint16)]

    def id() -> int:
        return MidgeBadgeCommandID.CMD_ID_START_SCAN


class CmdStartScanResponse(MidgeBadgeCommand):
    _pack_ = 1
    _fields_ = [("status_code", c_int32)]

    def id() -> int:
        return MidgeBadgeCommandID.CMD_ID_START_SCAN


class CmdStopScanRequest(MidgeBadgeCommand):
    _pack_ = 1
    _fields_ = [("reserved", c_uint16)]

    def id() -> int:
        return MidgeBadgeCommandID.CMD_ID_STOP_SCAN


class CmdStopScanResponse(MidgeBadgeCommand):
    _pack_ = 1
    _fields_ = [("status_code", c_int32)]

    def id() -> int:
        return MidgeBadgeCommandID.CMD_ID_STOP_SCAN


class CmdStartIMURequest(MidgeBadgeCommand):
    _pack_ = 1
    _fields_ = [
        ("sample_id", c_uint16),
        ("acc_fsr", c_uint16),
        ("gyr_fsr", c_uint16),
        ("datarate", c_uint16),
    ]

    def id() -> int:
        return MidgeBadgeCommandID.CMD_ID_START_IMU


class CmdStartIMUResponse(MidgeBadgeCommand):
    _pack_ = 1
    _fields_ = [("status_code", c_int32)]

    def id() -> int:
        return MidgeBadgeCommandID.CMD_ID_START_IMU


class CmdStopIMURequest(MidgeBadgeCommand):
    _pack_ = 1
    _fields_ = [("reserved", c_uint16)]

    def id() -> int:
        return MidgeBadgeCommandID.CMD_ID_STOP_IMU


class CmdStopIMUResponse(MidgeBadgeCommand):
    _pack_ = 1
    _fields_ = [("status_code", c_int32)]

    def id() -> int:
        return MidgeBadgeCommandID.CMD_ID_STOP_IMU


class CmdEraseSDRequest(MidgeBadgeCommand):
    _pack_ = 1
    _fields_ = [("reserved", c_uint16)]

    def id() -> int:
        return MidgeBadgeCommandID.CMD_ID_ERASE_SD


class CmdEraseSDResponse(MidgeBadgeCommand):
    _pack_ = 1
    _fields_ = [("status_code", c_int32)]

    def id() -> int:
        return MidgeBadgeCommandID.CMD_ID_ERASE_SD


class CmdGetFreeSDSpaceRequest(MidgeBadgeCommand):
    _pack_ = 1
    _fields_ = [("reserved", c_uint16)]

    def id() -> int:
        return MidgeBadgeCommandID.CMD_ID_GET_FREE_SD_SPACE


class CmdGetFreeSDSpaceResponse(MidgeBadgeCommand):
    _pack_ = 1
    _fields_ = [("free_bytes", c_uint32)]

    def id() -> int:
        return MidgeBadgeCommandID.CMD_ID_GET_FREE_SD_SPACE


class CmdGetFileIndexInfoRequest(MidgeBadgeCommand):
    _pack_ = 1
    _fields_ = [("index", c_int16)]

    def id() -> int:
        return MidgeBadgeCommandID.CMD_ID_GET_FILE_INDEX_INFO


class CmdGetFileIndexInfoResponse(MidgeBadgeCommand):
    _pack_ = 1
    _fields_ = [
        ("size_bytes", c_uint32),
        ("index", c_int16),
        ("path", c_uint8 * INTERFACE_MAX_FILE_NAME),
    ]

    def id() -> int:
        return MidgeBadgeCommandID.CMD_ID_GET_FILE_INDEX_INFO


class CmdGetFileCRC32Request(MidgeBadgeCommand):
    _pack_ = 1
    _fields_ = [("path", c_uint8 * INTERFACE_MAX_FILE_NAME)]

    def id() -> int:
        return MidgeBadgeCommandID.CMD_ID_GET_FILE_CRC32


class CmdGetFileCRC32Response(MidgeBadgeCommand):
    _pack_ = 1
    _fields_ = [("crc32", c_uint32), ("status_code", c_int32)]

    def id() -> int:
        return MidgeBadgeCommandID.CMD_ID_GET_FILE_CRC32


class CmdDownloadFileChunkRequest(MidgeBadgeCommand):
    _pack_ = 1
    _fields_ = [
        ("path", c_uint8 * INTERFACE_MAX_FILE_NAME),
        ("offset", c_uint32),
    ]

    def id() -> int:
        return MidgeBadgeCommandID.CMD_ID_DOWNLOAD_FILE_CHUNK


class CmdDownloadFileChunkResponse(MidgeBadgeCommand):
    _pack_ = 1
    _fields_ = [
        ("data", c_uint8 * (INTERFACE_CMD_DATA_SZ - 4)),
        ("bytes", c_int16),
    ]

    def id() -> int:
        return MidgeBadgeCommandID.CMD_ID_DOWNLOAD_FILE_CHUNK


# DATA
CMD_RESPONSES: list[MidgeBadgeCommand] = [
    CmdSetupExperimentResponse,
    CmdStatusResponse,
    CmdGetFWVersionResponse,
    CmdStartMicResponse,
    CmdStopMicResponse,
    CmdStartScanResponse,
    CmdStopScanResponse,
    CmdStartIMUResponse,
    CmdStopIMUResponse,
    CmdEraseSDResponse,
    CmdGetFreeSDSpaceResponse,
    CmdGetFileIndexInfoResponse,
    CmdGetFileCRC32Response,
    CmdDownloadFileChunkResponse,
]

SOT = b"#"
EOT = b"!"
