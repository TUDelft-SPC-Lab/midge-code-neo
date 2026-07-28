import argparse
import pathlib
import re
from dataclasses import dataclass

import matplotlib.pyplot as plt
import pandas as pd
import seaborn as sns
from midge_badge_framework.files import (
    AudioMetaDataCSVfmt,
    ImuAccelEntry,
    ImuGyroEntry,
    ImuMagnetoEntry,
    RotationVectorEntry,
    process_imu_entry,
    process_scan_entry,
    process_sync_entry,
)
from scipy import stats


@dataclass
class FilePattern:
    regex: re.Pattern
    parse_func: callable


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
        print(file)
        df = pd.read_csv(file)
        sns.lineplot(data=df, x="timestamp", y="x", label="x")
        sns.lineplot(data=df, x="timestamp", y="y", label="y")
        sns.lineplot(data=df, x="timestamp", y="z", label="z")
        plt.ylabel("Acceleration (g)")
        plt.xlabel("Elapsed Time (ms)")
        plt.savefig(f"{file.parent.joinpath(file.stem)}_plt.{args.img_fmt}", dpi=300, bbox_inches="tight")
        plt.close()

    gyro_files = list(input_dir.glob("GYR*.csv"))
    for file in gyro_files:
        print(file)
        df = pd.read_csv(file)
        sns.lineplot(data=df, x="timestamp", y="x", label="x")
        sns.lineplot(data=df, x="timestamp", y="y", label="y")
        sns.lineplot(data=df, x="timestamp", y="z", label="z")
        plt.ylabel("Angular Velocity (deg/s)")
        plt.xlabel("Elapsed Time (ms)")
        plt.savefig(f"{file.parent.joinpath(file.stem)}_plt.{args.img_fmt}", dpi=300, bbox_inches="tight")
        plt.close()

    mag_files = list(input_dir.glob("MAG*.csv"))
    for file in mag_files:
        print(file)
        df = pd.read_csv(file)
        sns.lineplot(data=df, x="timestamp", y="x", label="x")
        sns.lineplot(data=df, x="timestamp", y="y", label="y")
        sns.lineplot(data=df, x="timestamp", y="z", label="z")
        plt.ylabel("Magnetic Field (uT)")
        plt.xlabel("Elapsed Time (ms)")
        plt.savefig(f"{file.parent.joinpath(file.stem)}_plt.{args.img_fmt}", dpi=300, bbox_inches="tight")
        plt.close()

    rotation_files = list(input_dir.glob("ROT*.csv"))
    for file in rotation_files:
        print(file)
        df = pd.read_csv(file)
        sns.lineplot(data=df, x="timestamp", y="i", label="i")
        sns.lineplot(data=df, x="timestamp", y="j", label="j")
        sns.lineplot(data=df, x="timestamp", y="k", label="k")
        sns.lineplot(data=df, x="timestamp", y="real", label="real")
        plt.ylabel("Rotation Vector (unitless)")
        plt.xlabel("Elapsed Time (ms)")
        plt.savefig(f"{file.parent.joinpath(file.stem)}_plt.{args.img_fmt}", dpi=300, bbox_inches="tight")
        plt.close()

    # visualize scan data
    scan_files = list(input_dir.glob("PROX*.csv"))
    for file in scan_files:
        print(file)
        df = pd.read_csv(file)
        g = sns.FacetGrid(data=df, col="addr")
        g.map_dataframe(sns.lineplot, x="timestamp", y="rssi")
        g.add_legend()
        g.set_axis_labels("Elapsed Time (ms)", "RSSI (dBm)")
        plt.savefig(f"{file.parent.joinpath(file.stem)}_plt.{args.img_fmt}", dpi=300, bbox_inches="tight")
        plt.close()

    # track post-processing analysis data
    post_data = ""
    # Audio metadata ===
    audio_metadata_files = list(input_dir.glob("MIC*.M"))
    for file in audio_metadata_files:
        print(file)
        df = pd.read_csv(file)
        df_dropped = df[df["status"] == AudioMetaDataCSVfmt.AUDIO_STATUS_BUFFER_DROPPED].copy()
        df_dropped.reset_index(drop=True, inplace=True)
        df_dropped["timestamp(ms)"] = df_dropped["timestamp(ms)"] - df_dropped["timestamp(ms)"].min()
        df_dropped["timestamp(hr)"] = df_dropped["timestamp(ms)"] / (1000 * 60 * 60)
        lr = stats.linregress(df_dropped["timestamp(hr)"], df_dropped.index)
        fit_line = lr.slope * df_dropped["timestamp(hr)"] + lr.intercept
        timestamps = df_dropped["timestamp(hr)"].values
        fit_data = pd.DataFrame({"timestamp(hr)": timestamps, "fit_line": fit_line})
        sns.scatterplot(data=df_dropped, x="timestamp(hr)", y=df_dropped.index)
        sns.lineplot(
            data=fit_data,
            x="timestamp(hr)",
            y="fit_line",
            color="red",
            label=f"f(t) = ({lr.slope:.2e}/hr * t + {lr.intercept:.2e}) dropped buffers, R² = {lr.rvalue**2:.2e}",
        )
        plt.ylabel("Dropped Buffers")
        plt.xlabel("Elapsed Time (hr)")
        time_per_buffer = AudioMetaDataCSVfmt.AUDIO_BUFFER_BYTES / (df.iloc[0]["freq"] * df.iloc[0]["channels"] * 2)
        instant_samples_per_buffer = AudioMetaDataCSVfmt.AUDIO_BUFFER_BYTES // (2 * df.iloc[0]["channels"])
        text = f"""
        Buffer size: {AudioMetaDataCSVfmt.AUDIO_BUFFER_BYTES} bytes
        Time per buffer: {time_per_buffer:e} seconds
        Instant samples per buffer: {instant_samples_per_buffer} samples"""
        plt.text(0.05, 0.95, text, transform=plt.gca().transAxes, fontsize=10, verticalalignment="top")
        plt.savefig(f"{file.parent.joinpath(file.stem)}_analysis_plt.{args.img_fmt}", dpi=300, bbox_inches="tight")
        plt.close()
        post_data += f"Audio buffer dropped linear regression: {lr}\n"
        post_data += text + "\n"

    ## Time Sync ===
    file = input_dir.joinpath("SYNC.csv")
    df = pd.read_csv(file)
    df.drop(columns=["reference_datetime", "interpolated_datetime", "internal_datetime"], inplace=True)

    ## normalize first
    df = df[1:]  # drop first sample as it is the calibration sample
    col = df.iloc[0, :]
    df = df.apply(lambda x: x - col, axis=1)

    df["delta(ms)"] = df["internal_timestamp"] - df["reference_timestamp"]
    lr = stats.linregress(df["reference_timestamp"], df["delta(ms)"])
    df["fit_line"] = lr.slope * df["reference_timestamp"] + lr.intercept
    sns.scatterplot(data=df, x="reference_timestamp", y="delta(ms)")
    sns.lineplot(
        data=df,
        x="reference_timestamp",
        y="fit_line",
        color="red",
        label=f"f(t) = ({lr.slope:.2e} * t + {lr.intercept:.2e}) ms, R² = {lr.rvalue**2:.2e}",
    )
    plt.ylabel("internal - reference (ms)")
    plt.xlabel("Reference elapsed Time (ms)")
    text = f"""
    Approximate internal clock drift: {lr.slope * 1e6:.2f} ppm
    Average delta: {df["delta(ms)"].mean() / 1e3:.2e} s
    """
    plt.text(0.05, 0.95, text, transform=plt.gca().transAxes, fontsize=10, verticalalignment="top")
    plt.savefig(f"{file.parent.joinpath(file.stem)}_analysis.{args.img_fmt}", dpi=300, bbox_inches="tight")
    plt.close()

    post_data += f"Time sync linear regression: {lr}\n"
    post_data += "Slope is an experimental measurement of internal clk drift.\n"
    post_data += text + "\n"

    post_data_file = output_dir.joinpath("post_processing_analysis.txt")
    with open(post_data_file, "w") as f:
        f.write(post_data)
