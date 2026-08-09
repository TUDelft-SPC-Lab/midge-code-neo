import argparse
import pathlib
import re

from midge_badge_framework.files import (
    ImuAccelEntry,
    ImuGyroEntry,
    ImuMagnetoEntry,
    RotationVectorEntry,
)
from midge_badge_framework.raw_file_processing import (
    process_imu_entry,
    process_scan_entry,
    process_sync_entry,
)
from midge_badge_framework.visualizations import (
    audio_buffer_drops_per_time_unit_graph,
    imu_file_graph,
    scan_file_graph,
    sync_analysis_graph,
)


def main():
    parser = argparse.ArgumentParser(description="Parse scan data from input file")
    parser.add_argument("-i", "--input", type=str, default=None, help="Mingle Midge SD card path")
    parser.add_argument("-e", "--experiment", type=str, default=None, help="Experiment directory")
    parser.add_argument("-o", "--output", type=str, default=None, help="Experiment output directory")
    parser.add_argument("-f", "--img_fmt", type=str, default="png", help="image format for plots (default: png)")
    args = parser.parse_args()

    group_id = 0
    badge_id = 0

    input_dir = pathlib.Path(f"{args.input}/{args.experiment}")
    id_file = input_dir.joinpath("ID.TXT")
    with open(id_file) as f:
        text = f.read()
        # get group and badge id from text
        numbers = re.findall(r"[0-9]+", text)
        if len(numbers) == 2:
            group_id = int(numbers[0])
            badge_id = int(numbers[1])
        else:
            print(f"Could not parse group and badge id from {id_file}")
            exit(1)
        print(f"Group ID: {group_id}, Badge ID: {badge_id}")

    output_dir = pathlib.Path(f"{args.output}/group_{group_id}/badge_{badge_id}")
    output_dir.mkdir(parents=True, exist_ok=True)
    print(f"Output directory: {output_dir}")

    # Copy all files
    for _root, _dirs, files in input_dir.walk():
        for file in files:
            print(f"Copying {file} to {output_dir}")
            input_dir.joinpath(file).copy_into(output_dir)

    print(f"Copied all files from {input_dir} to {output_dir}")
    input_dir = output_dir

    # Post process
    mag_files = list(input_dir.glob("MAG[0-9]*"))
    mag_files = [f for f in mag_files if not f.suffix]
    print(f"Found {len(mag_files)} MAG files in {input_dir}")
    for mag_file in mag_files:
        output_file = output_dir.joinpath(f"{mag_file.name}.csv")
        process_imu_entry(mag_file, output_file, ImuMagnetoEntry)

    accel_files = list(input_dir.glob("ACC*"))
    accel_files = [f for f in accel_files if not f.suffix]
    print(f"Found {len(accel_files)} ACC files in {input_dir}")
    for accel_file in accel_files:
        output_file = output_dir.joinpath(f"{accel_file.name}.csv")
        process_imu_entry(accel_file, output_file, ImuAccelEntry)

    gyro_files = list(input_dir.glob("GYR*"))
    gyro_files = [f for f in gyro_files if not f.suffix]
    print(f"Found {len(gyro_files)} GYR files in {input_dir}")
    for gyro_file in gyro_files:
        output_file = output_dir.joinpath(f"{gyro_file.name}.csv")
        process_imu_entry(gyro_file, output_file, ImuGyroEntry)

    rotation_files = list(input_dir.glob("ROT*"))
    rotation_files = [f for f in rotation_files if not f.suffix]
    print(f"Found {len(rotation_files)} ROT files in {input_dir}")
    for rotation_file in rotation_files:
        output_file = output_dir.joinpath(f"{rotation_file.name}.csv")
        process_imu_entry(rotation_file, output_file, RotationVectorEntry)

    scan_files = list(input_dir.glob("PROX*"))
    scan_files = [f for f in scan_files if not f.suffix]
    print(f"Found {len(scan_files)} PROX files in {input_dir}")
    for scan_file in scan_files:
        output_file = output_dir.joinpath(f"{scan_file.name}.csv")
        process_scan_entry(scan_file, output_file)

    sync_files = list(input_dir.glob("SYNC"))
    sync_files = [f for f in sync_files if not f.suffix]
    print(f"Found {len(sync_files)} SYNC files in {input_dir}")
    for sync_file in sync_files:
        output_file = output_dir.joinpath(f"{sync_file.name}.csv")
        process_sync_entry(sync_file, output_file)

    # IMU Graphs
    accel_files = list(input_dir.glob("ACC*.csv"))
    for file in accel_files:
        img_path = file.parent.joinpath(f"{file.stem}_plt.{args.img_fmt}")
        imu_file_graph(
            file,
            img_path,
            ylabel="Acceleration (g)",
        )

    gyro_files = list(input_dir.glob("GYR*.csv"))
    for file in gyro_files:
        img_path = file.parent.joinpath(f"{file.stem}_plt.{args.img_fmt}")
        imu_file_graph(
            file,
            img_path,
            ylabel="Angular Velocity (deg/s)",
        )

    mag_files = list(input_dir.glob("MAG*.csv"))
    for file in mag_files:
        img_path = file.parent.joinpath(f"{file.stem}_plt.{args.img_fmt}")
        imu_file_graph(
            file,
            img_path,
            ylabel="Magnetic Field (uT)",
        )

    rotation_files = list(input_dir.glob("ROT*.csv"))
    for file in rotation_files:
        img_path = file.parent.joinpath(f"{file.stem}_plt.{args.img_fmt}")
        imu_file_graph(
            file,
            img_path,
            ylabel="Rotation Vector (unitless)",
            axis=["i", "j", "k", "real"],
        )

    # visualize scan data
    scan_files = list(input_dir.glob("PROX*.csv"))
    for file in scan_files:
        img_path = f"{file.parent.joinpath(file.stem)}_plt.{args.img_fmt}"
        scan_file_graph(file, img_path)

    # track post-processing analysis data
    post_data = ""

    # Audio metadata ===
    audio_metadata_files = list(input_dir.glob("MIC*.M"))
    for file in audio_metadata_files:
        img_path = f"{file.parent.joinpath(file.stem)}_analysis_plt.{args.img_fmt}"
        lr = audio_buffer_drops_per_time_unit_graph(file, img_path, time_unit="hr")
        post_data += f"Audio buffer dropped linear regression: {lr}\n"
        post_data += text + "\n"

    # Time Sync ===
    file = input_dir.joinpath("SYNC.csv")
    img_path = f"{file.parent.joinpath(file.stem)}_analysis_plt.{args.img_fmt}"
    lr = sync_analysis_graph(file, img_path)
    post_data += f"Time sync linear regression: {lr}\n"
    post_data += "Slope is an experimental measurement of internal clk drift.\n"
    post_data += text + "\n"

    post_data_file = output_dir.joinpath("post_processing_analysis.txt")
    with open(post_data_file, "w") as f:
        f.write(post_data)
