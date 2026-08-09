import matplotlib.pyplot as plt
import pandas as pd
import seaborn as sns
from scipy import stats

from midge_badge_framework.files import (
    AudioMetaDataCSVfmt,
)


def imu_file_graph(file_path, img_path, ylabel, axis=None, xlabel="Elapsed Time (ms)"):
    if axis is None:
        axis = ["x", "y", "z"]
    print(file_path)
    df = pd.read_csv(file_path)
    for ax in axis:
        sns.lineplot(data=df, x="timestamp", y=ax, label=ax)
    plt.ylabel(ylabel)
    plt.xlabel(xlabel)
    plt.savefig(img_path, dpi=300, bbox_inches="tight")
    plt.close()


def scan_file_graph(file_path, img_path):
    print(file_path)
    df = pd.read_csv(file_path)
    g = sns.FacetGrid(data=df, col="addr")
    g.map_dataframe(sns.lineplot, x="timestamp", y="rssi")
    g.add_legend()
    g.set_axis_labels("Elapsed Time (ms)", "RSSI (dBm)")
    plt.savefig(img_path)
    plt.close()


def audio_buffer_drops_per_time_unit_graph(file_path, img_path, time_unit="ms"):
    """
    Plots the number of audio buffer drops over time, with the time unit specified.
    Args:
        file_path (str): Path to the CSV file containing audio metadata.
        img_path (str): Path to save the generated plot image.
        time_unit (str): Time unit for the x-axis. Options are "hr", "minute", "ms".

    Returns:
        lr (LinregressResult): The result of the linear regression performed on the dropped buffer data.
    """
    print(file_path)
    divisor = 0
    match time_unit:
        case "hr":
            divisor = 60000
        case "minute":
            divisor = 3600000
        case "ms":
            divisor = 1
        case _:
            msg = "Unsupported time unit"
            raise ValueError(msg)

    df = pd.read_csv(file_path)
    df_dropped = df[df["status"] == AudioMetaDataCSVfmt.AUDIO_STATUS_BUFFER_DROPPED].copy()
    df_dropped.reset_index(drop=True, inplace=True)
    df_dropped["timestamp(ms)"] = df_dropped["timestamp(ms)"] - df_dropped["timestamp(ms)"].min()
    df_dropped[f"timestamp({time_unit})"] = df_dropped["timestamp(ms)"] / divisor
    lr = stats.linregress(df_dropped[f"timestamp({time_unit})"], df_dropped.index)
    fit_line = lr.slope * df_dropped[f"timestamp({time_unit})"] + lr.intercept
    timestamps = df_dropped[f"timestamp({time_unit})"].values
    fit_data = pd.DataFrame({f"timestamp({time_unit})": timestamps, "fit_line": fit_line})
    sns.scatterplot(data=df_dropped, x=f"timestamp({time_unit})", y=df_dropped.index)
    sns.lineplot(
        data=fit_data,
        x=f"timestamp({time_unit})",
        y="fit_line",
        color="red",
        label=f"f(t) = ({lr.slope:.2e}/{time_unit} * t + {lr.intercept:.2e}) dropped buffers, R² = {lr.rvalue**2:.2e}",
    )
    plt.ylabel("Dropped Buffers")
    plt.xlabel(f"Elapsed Time ({time_unit})")
    time_per_buffer = AudioMetaDataCSVfmt.AUDIO_BUFFER_BYTES / (df.iloc[0]["freq"] * df.iloc[0]["channels"] * 2)
    instant_samples_per_buffer = AudioMetaDataCSVfmt.AUDIO_BUFFER_BYTES // (2 * df.iloc[0]["channels"])
    text = f"""
    Buffer size: {AudioMetaDataCSVfmt.AUDIO_BUFFER_BYTES} bytes
    Time per buffer: {time_per_buffer:e} seconds
    Instant samples per buffer: {instant_samples_per_buffer} samples"""
    plt.text(0.05, 0.95, text, transform=plt.gca().transAxes, fontsize=10, verticalalignment="top")
    plt.savefig(img_path, dpi=300, bbox_inches="tight")
    plt.close()
    return lr


def sync_analysis_graph(file_path, img_path):
    """
    Performs time synchronization analysis on the provided CSV file and generates a plot.
    Args:
        file_path (str): Path to the CSV file containing time synchronization data.
        img_path (str): Path to save the generated plot image.

    Returns:
        lr (LinregressResult): The result of the linear regression performed on the time sync data.
    """
    df = pd.read_csv(file_path)
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
    plt.savefig(img_path, dpi=300, bbox_inches="tight")
    plt.close()
    return lr
