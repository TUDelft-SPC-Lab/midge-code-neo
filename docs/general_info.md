# General Repo Info

## Index

- [Installation](#installation)
- [Available scripts](#available-scripts)
- [Adding dependencies](#adding-dependencies)
- [Running test](#running-tests)
- [Formatting and checking](#formatting-and-checking)
- [Documentation](#documentation)
- [Versions](#versions)

## Installation

1. Install [uv](https://docs.astral.sh/uv/):

2. Install the dependencies, including the dev dependencies

    ```bash
    uv sync
    ```
    or install only the runtime dependencies

    ```bash
    uv sync --no-dev
    ```

3. Install the prek hook.
This will set up prek to run the checks automatically on your files before you commit them.

    ```bash
    uv run prek install
    ```

  **Remember that if the prek checks fail, you can always commit by skipping the checks with `git commit --no-verify`**

## Available scripts

The repo provides multiple scripts to work with the Mingle Midge.

The following tables gives an overview of these.
For additional details see the the [Usage](usage/data_collection.md) section

| Script | Application |
| ------ | ----------- |
| `uv run tester` | Interactive CLI to test a single Mingle Midge over BLE |
| `uv run hub_cli` | Run a data collection experiment across several Midges |
| `uv run extract_data` | Extract data from a Midge SD card and run the parsers on it |
| `uv run scan_parse` | Parse raw scan data |
| `uv run imu_parse` | Parse raw IMU data |
| `uv run timesync_parse` | Parse raw time synchronization data |

## Adding dependencies


Add dependencies by running
```bash
uv add numpy
```
if you want to install PyTorch have a look at https://docs.astral.sh/uv/guides/integration/pytorch/

## Running tests

Run your tests with

```bash
uv run pytest --cov=src ./tests
```

## Formatting and checking

The tools for formatting and linting your code for errors are all bundled with [prek](https://prek.j178.dev). Included are:
- [ruff](https://astral.sh/ruff) - linting and formatting
- [yamlfix](https://github.com/lyz-code/yamlfix) - linting and formatting for .yaml files
- various other small fixes and checks (see the [`.pre-commit-config.yaml`](.pre-commit-config.yaml) file for more information)

It's possible that prek will make changes to your files when it runs the checks, so you should add those changes to your commit before you commit your code. A typical workflow would look like this:

```bash
git add -u
git commit -m "My commit message"
# prek will run the checks here; if it makes changes, you'll need to add them to your commit
git add -u
git commit -m "My commit message"
# changes should have all been made by now and the commit should pass if there are no other issues
# if your commit fails again here, you have to fix the issues manually (not everything can be fixed automatically).
```

One thing that is worth knowing is how to lint your files outside of the context of a commit. You can run the checks manually by running the following command:

```bash
uv run prek run --all-files
```

This will run the checks on all files in your git project, regardless of whether they're staged for commit or not.

## Documentation

Generate the documentation locally with

```bash
uv run zensical serve
```

## Versions

Versions are managed automatically via [hatch-vcs](https://github.com/ofek/hatch-vcs), which follows the versioning scheme from [setuptools-scm](https://setuptools-scm.readthedocs.io/en/latest/usage/#default-versioning-scheme).

To create a new version, tag the code with `git tag <version>`, e.g. `git tag v0.1.0`, and push the tag with `git push --tags`.

You can check the version by running

```bash
uv run hatch version
```

In python you can see the version with
```python
from midge_code_neo import __version__

print(f"midge_code_neo version is { __version__ }")
```
