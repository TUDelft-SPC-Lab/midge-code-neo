import argparse
import pathlib
from datetime import datetime

from midge_badge_framework.files import TimeSyncEntry


def main():
    parser = argparse.ArgumentParser(description="Parse time synchronization data from input file")
    parser.add_argument("input_file", type=str, help="Input file to parse")
    parser.add_argument("-o", "--output", type=str, default=None, help="Output file (default: stdout)")

    args = parser.parse_args()

    # Open input file
    input_path = pathlib.Path(args.input_file)

    # Determine output destination
    output_path = args.output if (args.output) else "/dev/stdout"
    with open(output_path, "w") as output_file:
        output_file.write(
            "reference_timestamp,interpolated_timestamp,internal_timestamp,"
            "reference_datetime,interpolated_datetime,internal_datetime\n"
        )
        with open(input_path, "rb") as file:
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
                output_file.write(msg)
