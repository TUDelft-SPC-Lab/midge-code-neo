"""Tests for experiment loading schemas and YAML deserialization."""

from pathlib import Path

import pytest
from midge_badge_framework.schema import (
    AudioParamsSchema,
    BadgeSchema,
    ExperimentParamsSchema,
    ExperimentSchema,
    GroupSchema,
    ImuParamsSchema,
    ScanParamsSchema,
)
from pydantic import ValidationError


def test_badge_schema_accepts_mac_and_any_values():
    badge_mac = BadgeSchema(id=1, mac="AA:BB:CC:DD:EE:FF", name="alpha")
    badge_any = BadgeSchema(id=2, mac="any")

    assert badge_mac.mac == "AA:BB:CC:DD:EE:FF"
    assert badge_mac.name == "alpha"
    assert badge_any.mac == "any"
    assert badge_any.name is None


def test_badge_schema_rejects_invalid_mac_values_text():
    with pytest.raises(ValidationError):
        BadgeSchema(id=1, mac="not-a-mac")


def test_badge_schema_rejects_invalid_mac_values_missing_octet():
    with pytest.raises(ValidationError):
        BadgeSchema(id=1, mac="AA:BB:CC:DD:EE")  # Missing one octet


def test_badge_schema_rejects_invalid_mac_values_bad_mac():
    with pytest.raises(ValidationError):
        BadgeSchema(id=1, mac="AA:BB:CC:DD:EE:TT")  # Invalid characters


def test_badge_schema_fields_are_frozen():
    badge = BadgeSchema(id=1, mac="AA:BB:CC:DD:EE:FF", name="alpha")

    with pytest.raises(ValidationError, match="Field is frozen"):
        badge.id = 10

    # `mac` can be updated at runtime; ensure assignment succeeds
    badge.mac = "11:22:33:44:55:66"
    assert badge.mac == "11:22:33:44:55:66"

    with pytest.raises(ValidationError, match="Field is frozen"):
        badge.name = "beta"


def test_group_schema_accepts_unique_badge_ids():
    group = GroupSchema(
        id=1,
        name="group-a",
        description="group with unique badges",
        badges=[
            BadgeSchema(id=1, mac="AA:BB:CC:DD:EE:FF"),
            BadgeSchema(id=2, mac="11:22:33:44:55:66"),
        ],
    )

    assert group.id == 1
    assert [badge.id for badge in group.badges] == [1, 2]


def test_group_schema_rejects_duplicate_badge_ids():
    with pytest.raises(ValidationError, match="Badge IDs within a group must be unique"):
        GroupSchema(
            id=1,
            name="group-a",
            description="group with duplicate badges",
            badges=[
                BadgeSchema(id=1, mac="AA:BB:CC:DD:EE:FF"),
                BadgeSchema(id=1, mac="11:22:33:44:55:66"),
            ],
        )


def test_group_schema_fields_are_frozen_except_badges_container_assignment():
    group = GroupSchema(
        id=1,
        name="group-a",
        description="desc",
        badges=[BadgeSchema(id=1, mac="AA:BB:CC:DD:EE:FF")],
    )

    with pytest.raises(ValidationError, match="Field is frozen"):
        group.id = 2

    with pytest.raises(ValidationError, match="Field is frozen"):
        group.name = "group-b"

    with pytest.raises(ValidationError, match="Field is frozen"):
        group.description = "changed"


def test_experiment_schema_accepts_unique_group_ids():
    experiment = ExperimentSchema(
        id=0,
        name="experiment-a",
        description="valid experiment",
        params=ExperimentParamsSchema(audio=None, imu=None, scan=None),
        groups=[
            GroupSchema(
                id=1,
                name="g1",
                description="d1",
                badges=[BadgeSchema(id=1, mac="AA:BB:CC:DD:EE:FF")],
            ),
            GroupSchema(
                id=2,
                name="g2",
                description="d2",
                badges=[BadgeSchema(id=1, mac="11:22:33:44:55:66")],
            ),
        ],
    )

    assert experiment.name == "experiment-a"
    assert [group.id for group in experiment.groups] == [1, 2]


def test_experiment_schema_rejects_duplicate_group_ids():
    with pytest.raises(ValidationError, match="Group IDs must be unique"):
        ExperimentSchema(
            id=0,
            name="experiment-a",
            description="invalid experiment",
            params=ExperimentParamsSchema(audio=None, imu=None, scan=None),
            groups=[
                GroupSchema(
                    id=1,
                    name="g1",
                    description="d1",
                    badges=[BadgeSchema(id=1, mac="AA:BB:CC:DD:EE:FF")],
                ),
                GroupSchema(
                    id=1,
                    name="g2",
                    description="d2",
                    badges=[BadgeSchema(id=1, mac="11:22:33:44:55:66")],
                ),
            ],
        )


def test_experiment_schema_fields_are_frozen():
    experiment = ExperimentSchema(
        id=0,
        name="exp",
        description="desc",
        params=ExperimentParamsSchema(audio=None, imu=None, scan=None),
        groups=[],
    )

    with pytest.raises(ValidationError, match="Field is frozen"):
        experiment.name = "changed"

    with pytest.raises(ValidationError, match="Field is frozen"):
        experiment.description = "changed"

    with pytest.raises(ValidationError, match="Field is frozen"):
        experiment.params = ExperimentParamsSchema(audio=None, imu=None, scan=None)

    with pytest.raises(ValidationError, match="Field is frozen"):
        experiment.groups = []


def test_audio_params_schema_accepts_valid_values():
    audio = AudioParamsSchema(high_freq_hz=16000, low_freq_decimation=8, channels=2)
    assert audio.high_freq_hz == 16000
    assert audio.low_freq_decimation == 8
    assert audio.channels == 2


def test_audio_params_schema_rejects_invalid_channel_count():
    with pytest.raises(ValidationError):
        AudioParamsSchema(high_freq_hz=16000, low_freq_decimation=8, channels=0)

    with pytest.raises(ValidationError):
        AudioParamsSchema(high_freq_hz=16000, low_freq_decimation=8, channels=3)


def test_imu_params_schema_accepts_valid_values():
    imu = ImuParamsSchema(sample_rate_hz=100, accel_range_g=2, gyro_range_dps=250)
    assert imu.sample_rate_hz == 100
    assert imu.accel_range_g == 2
    assert imu.gyro_range_dps == 250


def test_scan_params_schema_accepts_valid_values():
    scan = ScanParamsSchema(interval=100, window=50)
    assert scan.interval == 100
    assert scan.window == 50


def test_experiment_params_schema_accepts_all_params():
    audio = AudioParamsSchema(high_freq_hz=16000, low_freq_decimation=8, channels=1)
    imu = ImuParamsSchema(sample_rate_hz=100, accel_range_g=2, gyro_range_dps=250)
    scan = ScanParamsSchema(interval=100, window=50)

    params = ExperimentParamsSchema(audio=audio, imu=imu, scan=scan)
    assert params.audio == audio
    assert params.imu == imu
    assert params.scan == scan


def test_experiment_params_schema_accepts_all_none():
    params = ExperimentParamsSchema(audio=None, imu=None, scan=None)
    assert params.audio is None
    assert params.imu is None
    assert params.scan is None


def test_experiment_params_schema_accepts_mixed_params():
    audio = AudioParamsSchema(high_freq_hz=16000, low_freq_decimation=8, channels=1)
    params = ExperimentParamsSchema(audio=audio, imu=None, scan=None)
    assert params.audio == audio
    assert params.imu is None
    assert params.scan is None


def test_experiment_schema_load_from_yaml_success(tmp_path):
    yaml_file = tmp_path / "experiment.yaml"
    yaml_file.write_text(
        """
id: 1
name: testlab
description: Test loading from yaml
params:
  audio:
    high_freq_hz: 16000
    low_freq_decimation: 8
    channels: 2
  imu: null
  scan: null
groups:
  - id: 1
    name: group-a
    description: first group
    badges:
      - id: 1
        mac: AA:BB:CC:DD:EE:FF
        name: badge-1
      - id: 2
        mac: any
""".strip(),
        encoding="utf-8",
    )

    result = ExperimentSchema.load_from_yaml(str(yaml_file))

    assert isinstance(result, ExperimentSchema)
    assert result.name == "testlab"
    assert result.params.audio is not None
    assert result.params.audio.high_freq_hz == 16000
    assert result.params.imu is None
    assert result.params.scan is None
    assert len(result.groups) == 1
    assert [badge.id for badge in result.groups[0].badges] == [1, 2]
    assert result.groups[0].badges[1].name is None


def test_experiment_schema_load_from_yaml_missing_required_field_raises_validation_error(tmp_path):
    yaml_file = tmp_path / "missing-fields.yaml"
    yaml_file.write_text(
        """
description: Missing name and groups
""".strip(),
        encoding="utf-8",
    )

    with pytest.raises(ValidationError):
        ExperimentSchema.load_from_yaml(str(yaml_file))


def test_experiment_schema_load_from_yaml_rejects_invalid_yaml(tmp_path):
    yaml_file = tmp_path / "invalid.yaml"
    yaml_file.write_text(
        """
name: broken
description: invalid
groups:
  - id: 1
    name group-a
""".strip(),
        encoding="utf-8",
    )

    with pytest.raises(Exception):  # noqa: B017, PT011
        ExperimentSchema.load_from_yaml(str(yaml_file))


def test_experiment_schema_load_from_yaml_propagates_file_not_found():
    missing_path = Path("/tmp/does-not-exist-experiment.yaml")

    with pytest.raises(FileNotFoundError):
        ExperimentSchema.load_from_yaml(str(missing_path))
