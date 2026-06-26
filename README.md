# midge-code-neo

midge-code-neo is a complete software solution for the usage of sociometric
devices developed by the SPCL. This is being developed as a substitute of
[midge-code](https://github.com/TUDelft-SPC-Lab/midge-code), the previous
software solution under use for data collection experiments using the
Mingle Midge V1.

For now, this solution supports the following platforms:

- Mingle Midge V1
- Mingle Midge V2 (WIP)

## Hardware Design

Hardware Design for Mingle Midge devices is tracked in the [hardware repo](https://github.com/TUDelft-SPC-Lab/spcl_midge_hardware)

## Getting Started

Visit the [documentaton](https://tudelft-spc-lab.github.io/midge-code-neo/getting_started/)

## Repository structure

```
.
├── .vscode/     support for vscode workflow
├── docs/        project documentation
├── experiments/ example experiment configurations
├── firmware/    Embedded firmware sources
├── packages/    Internal python libraries
├── src/         Python applications
└── tests/       automated tests
```

## Help

Open an issue in this repository, we'll try to answer ASAP.

## Licensing

The code under this repository is licensed under the MIT license, with the sole
exception of the submodule for the ICM-20948
