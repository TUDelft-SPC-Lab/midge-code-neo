from datetime import datetime
from pathlib import Path

from .files import RotationVectorEntry, ScanFileEntry, TimeSyncEntry, _ImuAxisEntry
from .protocol import mingle_midge_batt_mv_to_percent


def process_sync_entry(input_file: Path, output_file: Path):
    """
    Processes a binary time synchronization registry file and writes the extracted data to a CSV file.
    Args:
        input_file (Path): Path to the binary SYNC file.
        output_file (Path): Path to the output CSV file.
    Returns:
        None
    """
    with open(output_file, "w") as out_f:
        out_f.write(
            "reference_timestamp,interpolated_timestamp,internal_timestamp,"
            "reference_datetime,interpolated_datetime,internal_datetime\n"
        )
        with open(input_file, "rb") as file:
            while True:
                a = TimeSyncEntry()
                sz = file.readinto(a)
                if not sz:
                    break

                reference_ts = a.reference
                interpolated_ts = a.interpolated
                internal_ts = a.internal
                reference_dt = datetime.fromtimestamp(reference_ts / 1000.0)
                interpolated_dt = datetime.fromtimestamp(interpolated_ts / 1000.0)
                internal_dt = datetime.fromtimestamp(internal_ts / 1000.0)
                msg = f"{reference_ts},{interpolated_ts},{internal_ts},{reference_dt},{interpolated_dt},{internal_dt}\n"
                out_f.write(msg)


def process_scan_entry(input_file: Path, output_file: Path):
    """
    Processes a binary proximity scan data file and writes the extracted data to a CSV file.
    Args:
        input_file (Path): Path to the binary PROX file.
        output_file (Path): Path to the output CSV file.
    Returns:
        None
    """

    def mac_address_to_str(mac_address: bytes) -> str:
        return ":".join(f"{b:02x}" for b in mac_address)

    with open(output_file, "w") as out_f:
        out_f.write("addr,group,badgeID,rssi,battery%,active_sensors,timestamp\n")
        with open(input_file, "rb") as file:
            while True:
                a = ScanFileEntry()
                sz = file.readinto(a)
                if not sz:
                    break

                adv_data = a.advertised_data
                group = adv_data.badge_assignment.id.group
                badge_id = adv_data.badge_assignment.id.badge
                batt_p = mingle_midge_batt_mv_to_percent(adv_data.battery_mv)
                active_sensors = adv_data.active_sensor_bitflags
                timestamp = str(a.timestamp)
                out_f.write(
                    f"{mac_address_to_str(a.mac_address)},{group},{badge_id},{a.rssi},{batt_p},{active_sensors:05b},{timestamp}\n"
                )


def process_imu_entry(input_file: Path, output_file: Path, entry_class: _ImuAxisEntry):
    """
    Processes a binary IMU data file (accelerometer, gyroscope, magnetometer, or rotation vector) and writes the
    extracted data to a CSV file.
    Args:
        input_file (Path): Path to the binary IMU file.
        output_file (Path): Path to the output CSV file.
        entry_class (_ImuAxisEntry): The class representing the specific IMU data type (e.g., ImuAccelEntry,
            ImuGyroEntry, ImuMagnetoEntry, or RotationVectorEntry).
    Returns:
        None
    """
    with open(output_file, "w") as out_f:
        header = (
            f"{entry_class._fields_[0][0]},"
            f"{entry_class._fields_[1][0]},"
            f"{entry_class._fields_[2][0]},"
            f"{entry_class._fields_[3][0]},"
            f"{entry_class._fields_[4][0]}\n"
        )
        out_f.write(header)
        with open(input_file, "rb") as in_f:
            while True:
                a = entry_class()
                sz = in_f.readinto(a)
                if not sz:
                    break

                timestamp = str(a.timestamp)
                if isinstance(a, RotationVectorEntry):
                    line = f"{timestamp},{a.i},{a.j},{a.k},{a.real}\n"
                else:
                    line = f"{timestamp},{a.x},{a.y},{a.z},{a.reserved}\n"
                out_f.write(line)
