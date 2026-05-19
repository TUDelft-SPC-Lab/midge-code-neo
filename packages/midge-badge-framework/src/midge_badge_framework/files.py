import csv
from abc import abstractmethod
from ctypes import Structure, c_float, c_int8, c_uint8, c_uint64
from datetime import datetime

from .protocol import CustomAdvertisementData


class TimestampU64(Structure):
    _pack_ = 1
    _fields_ = [("timestamp", c_uint64)]

    def __str__(self):
        return f" (datetime: {datetime.fromtimestamp(self.timestamp / 1000.0)})"


class ScanFileEntry(Structure):
    _pack_ = 1
    _fields_ = [
        ("rssi", c_int8),
        ("mac_address", c_uint8 * 6),
        ("advertised_data", CustomAdvertisementData),
        ("timestamp", c_uint64),
    ]

    def __str__(self):
        ret = f"{self.__class__.__name__} : "
        for field in self._fields_:
            attr = field[0]
            if attr == "mac_address":
                ret += "[mac_addr = "
                arr = getattr(self, attr)
                for x in arr:
                    ret += f"{x:02x}:"
                ret = ret[0:-1]
                ret += "]"
            else:
                ret += f"[{attr} = {getattr(self, attr)}]"
        return ret


class AudioMetaDataCSVfmt:
    fields = ["timestamp(ms)", "status", "event", "freq", "channels"]

    AUDIO_EVENT_TYPE_TRIGGER_START = 0
    AUDIO_EVENT_TYPE_TRIGGER_STOP = 1

    def __init__(self, file_path):
        self.file_path = file_path

    def __enter__(self):
        self._file = open(self.file_path, newline="")
        self._reader = csv.DictReader(self._file)
        return self

    def __exit__(self, _exc_type, _exc_val, _exc_tb):
        self._file.close()

    def __str__(self):
        ret = f"{self.__class__.__name__} : "
        for field in self._fields_:
            attr = field[0]
            ret += f"[{attr} = {getattr(self, attr)}]"
        return ret

    def get_sample_info(self):
        """Returns (freq, channels) from the first file entry."""
        self._file.seek(0)
        reader = csv.DictReader(self._file)
        first = next(reader)
        return int(first["freq"]), int(first["channels"])

    def get_trigger_start_errors(self):
        """Returns copies of all entries where event=TRIGGER_START and status < 0."""
        self._file.seek(0)
        reader = csv.DictReader(self._file)
        return [
            dict(row)
            for row in reader
            if int(row["event"]) == self.AUDIO_EVENT_TYPE_TRIGGER_START and int(row["status"]) < 0
        ]


class _ImuAxisEntry(Structure):
    _pack_ = 1
    _fields_ = [("timestamp", c_uint64), ("x", c_float), ("y", c_float), ("z", c_float), ("reserved", c_float)]

    def __str__(self):
        ret = f"{self.__class__.__name__} : "
        for field in self._fields_:
            attr = field[0]
            ret += f"[{attr} = {getattr(self, attr)}]"
        return ret

    @abstractmethod
    def units(self) -> str:
        pass


# Need to verify units


class ImuAccelEntry(_ImuAxisEntry):
    def units(self):
        return "m/s^2"


class ImuGyroEntry(_ImuAxisEntry):
    def units(self):
        return "rad/s"


class ImuMagnetoEntry(_ImuAxisEntry):
    def units(self):
        return "uT"


class RotationVectorEntry(Structure):
    _pack_ = 1
    _fields_ = [("timestamp", c_uint64), ("i", c_float), ("j", c_float), ("k", c_float), ("real", c_float)]

    def units(self):
        return "unitless"


class TimeSyncEntry(Structure):
    _pack_ = 1
    _fields_ = [
        ("reference", c_uint64),
        ("interpolated", c_uint64),
        ("internal", c_uint64),
    ]

    def __str__(self):
        ret = f"{self.__class__.__name__} : "
        for field in self._fields_:
            attr = field[0]
            ret += f"[{attr} = {datetime.fromtimestamp(getattr(self, attr) / 1000.0)}]"
        return ret
