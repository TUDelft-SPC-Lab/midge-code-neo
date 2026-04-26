from typing import Annotated

from pydantic import BaseModel, Field, field_validator
from ruamel.yaml import YAML

VALID_MAC_OR_ANY = r"^([0-9A-Fa-f]{2}[:]){5}([0-9A-Fa-f]{2})$|^any$"


class BadgeSchema(BaseModel):
    id: Annotated[int, Field(frozen=True)]
    mac: Annotated[str, Field(pattern=VALID_MAC_OR_ANY, frozen=True)]
    name: Annotated[str | None, Field(frozen=True)] = None


class GroupSchema(BaseModel):
    id: Annotated[int, Field(frozen=True)]
    name: Annotated[str, Field(frozen=True)]
    description: Annotated[str, Field(frozen=True)]
    badges: list[BadgeSchema]

    @field_validator("badges")
    def validate_unique_badge_ids(cls, badges):
        badge_ids = [badge.id for badge in badges]
        if len(badge_ids) != len(set(badge_ids)):
            msg = "Badge IDs within a group must be unique"
            raise ValueError(msg)
        return badges


class AudioParamsSchema(BaseModel):
    high_freq_hz: int
    low_freq_decimation: int
    channels: Annotated[int, Field(ge=1, le=2)]


class ImuParamsSchema(BaseModel):
    accel_range_g: int
    gyro_range_dps: int
    sample_rate_hz: int


class ScanParamsSchema(BaseModel):
    interval: int
    window: int


class ExperimentParamsSchema(BaseModel):
    audio: AudioParamsSchema | None
    imu: ImuParamsSchema | None
    scan: ScanParamsSchema | None


class ExperimentSchema(BaseModel):
    id: Annotated[int, Field(frozen=True)]
    name: Annotated[str, Field(frozen=True)]
    description: Annotated[str, Field(frozen=True)]
    params: Annotated[ExperimentParamsSchema, Field(frozen=True)]
    groups: Annotated[list[GroupSchema], Field(frozen=True)]

    @field_validator("groups")
    def validate_unique_group_ids(cls, groups):
        group_ids = [group.id for group in groups]
        if len(group_ids) != len(set(group_ids)):
            msg = "Group IDs must be unique"
            raise ValueError(msg)
        return groups

    @staticmethod
    def load_from_yaml(file_path: str) -> ExperimentSchema:
        yaml = YAML(typ="safe")
        with open(file_path) as f:
            data = yaml.load(f)
            return ExperimentSchema(**data)
