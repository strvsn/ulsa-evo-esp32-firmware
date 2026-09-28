#!/usr/bin/env python3
"""Verify the public ULSA EVO custom-firmware compatibility contract."""

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path
import re
import struct


ROOT = Path(__file__).resolve().parents[1]
CONTRACT_PATH = ROOT / "config" / "ulsa_evo_compatibility_contract.json"
PACKAGE_HEADER_SIZE = 92


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def source(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def numeric_constant(path: str, name: str) -> int:
    text = source(path)
    patterns = (
        rf"^\s*#define\s+{re.escape(name)}\s+([^/\r\n]+)",
        rf"^\s*(?:static\s+)?const(?:expr)?\s+[^=;]+\s+{re.escape(name)}\s*=\s*([^;]+);",
    )
    expression = None
    for pattern in patterns:
        match = re.search(pattern, text, re.MULTILINE)
        if match:
            expression = match.group(1).strip()
            break
    require(expression is not None, f"missing numeric constant {name} in {path}")
    normalized = re.sub(r"(?<=[0-9A-Fa-f])[uUlL]+", "", expression)
    require(
        re.fullmatch(r"[0-9A-Fa-fxX()\s+*\-/]+", normalized) is not None,
        f"unsupported numeric expression for {name}: {expression}",
    )
    return int(eval(normalized, {"__builtins__": {}}, {}))


def string_constant(path: str, name: str) -> str:
    text = source(path)
    patterns = (
        rf'^\s*#define\s+{re.escape(name)}\s+"([^"]*)"',
        rf'^\s*(?:static\s+)?const\s+char\s+{re.escape(name)}\[\]\s*=\s*"([^"]*)";',
    )
    for pattern in patterns:
        match = re.search(pattern, text, re.MULTILINE)
        if match:
            return match.group(1)
    raise RuntimeError(f"missing string constant {name} in {path}")


def parse_partition_table() -> dict[str, dict[str, int | str]]:
    partitions: dict[str, dict[str, int | str]] = {}
    with (ROOT / "partitions_ulsa_stm32pkg.csv").open(
        encoding="utf-8", newline=""
    ) as handle:
        for row in csv.reader(handle):
            if not row or row[0].lstrip().startswith("#"):
                continue
            values = [value.strip() for value in row]
            require(len(values) >= 5, f"invalid partition row: {row}")
            partitions[values[0]] = {
                "type": values[1],
                "subtype": values[2],
                "offset": int(values[3], 0),
                "size": int(values[4], 0),
            }
    return partitions


def verify_partition(
    partitions: dict[str, dict[str, int | str]], expected: dict[str, object]
) -> None:
    label = str(expected["label"])
    require(label in partitions, f"missing partition {label}")
    actual = partitions[label]
    require(actual["type"] == expected["type"], f"{label} partition type differs")
    require(actual["subtype"].lower() == str(expected["subtype"]).lower(),
            f"{label} partition subtype differs")
    require(int(actual["size"]) >= int(expected["minimumSizeBytes"]),
            f"{label} partition is too small")


def verify_exact_partition(
    partitions: dict[str, dict[str, int | str]], expected: dict[str, object]
) -> None:
    label = str(expected["label"])
    require(label in partitions, f"missing partition {label}")
    actual = partitions[label]
    require(actual["type"] == expected["type"], f"{label} partition type differs")
    require(actual["subtype"].lower() == str(expected["subtype"]).lower(),
            f"{label} partition subtype differs")
    require(int(actual["offset"]) == int(expected["offsetBytes"]),
            f"{label} partition offset differs")
    require(int(actual["size"]) == int(expected["sizeBytes"]),
            f"{label} partition size differs")


def parse_version(value: str) -> tuple[int, int, int]:
    match = re.fullmatch(r"(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)", value)
    require(match is not None, f"invalid canonical firmware version: {value}")
    version = tuple(int(component) for component in match.groups())
    require(all(component <= 255 for component in version),
            f"firmware version component exceeds 255: {value}")
    return version


def verify_sources(contract: dict[str, object]) -> None:
    require(contract.get("schemaVersion") == 1, "unsupported compatibility schema")
    hardware = contract["hardware"]
    pins = hardware["pins"]
    pin_macros = {
        "i2cSda": "I2C_SDA_PIN",
        "i2cScl": "I2C_SCL_PIN",
        "stm32UartTx": "UART1_TX_PIN",
        "stm32UartRx": "UART1_RX_PIN",
        "stm32Boot0": "STM32_BOOT0_PIN",
        "stm32Reset": "STM32_RESET_PIN",
    }
    for field, macro in pin_macros.items():
        require(numeric_constant("include/config/pin_config.h", macro) == pins[field],
                f"hardware pin contract differs for {field}")

    measurement = contract["normalMeasurement"]
    i2c = measurement["i2c"]
    i2c_constants = {
        "frequencyHz": ("include/config/pin_config.h", "I2C_FREQUENCY"),
        "defaultAddress": ("include/config/pin_config.h", "ULSA_EVO_I2C_ADDR_DEFAULT"),
        "whoAmIRegister": ("include/sensor/ulsa_evo_i2c_client.h", "REG_WHOAMI"),
        "deviceId": ("include/sensor/ulsa_evo_i2c_client.h", "DEVICE_ID"),
        "minimumRegisterVersion": (
            "include/sensor/ulsa_evo_i2c_client.h", "SUPPORTED_REG_VERSION_MIN"
        ),
        "snapshotRegister": (
            "include/sensor/ulsa_evo_i2c_client.h", "REG_SNAPSHOT_START"
        ),
        "snapshotLength": ("include/sensor/ulsa_evo_i2c_client.h", "SNAPSHOT_LENGTH"),
    }
    for field, (path, name) in i2c_constants.items():
        require(numeric_constant(path, name) == i2c[field],
                f"I2C contract differs for {field}")
    require(numeric_constant("include/config/pin_config.h", "UART1_BAUD_RATE") ==
            measurement["uartFallback"]["baud"], "UART baud contract differs")
    require(numeric_constant("include/sensor/uart_wind_frame_parser.h", "TOKEN_COUNT") ==
            measurement["uartFallback"]["tokenCount"], "UART frame contract differs")

    app = contract["companionApp"]
    ble_constants = {
        "advertisedService": "UUID_SERVICE_ENVIRONMENTAL",
        "optionalTemperatureCharacteristic": "UUID_CHAR_TEMPERATURE",
        "capabilitiesProtocolVersion": "BLE_CAPABILITIES_PROTOCOL_VERSION",
        "bleInterfaceRevision": "BLE_INTERFACE_REVISION",
    }
    for field, name in ble_constants.items():
        require(numeric_constant("include/ble/ble_config.h", name) == app[field],
                f"BLE contract differs for {field}")
    wind_characteristics = [
        numeric_constant("include/ble/ble_config.h", "UUID_CHAR_WIND_DIRECTION"),
        numeric_constant("include/ble/ble_config.h", "UUID_CHAR_WIND_SPEED"),
    ]
    require(wind_characteristics == app["requiredWindCharacteristics"],
            "BLE wind characteristic contract differs")
    require(string_constant("include/ble/ble_config.h", "UUID_SERVICE_ULSA_WIND") ==
            app["ulsaServiceUuid"], "ULSA BLE service UUID differs")

    update = contract["stm32Update"]
    require(numeric_constant("include/stm32_update/stm32_version_identity.h",
                             "STM32_UPDATE_CONTRACT_VERSION") ==
            update["esp32UpdateContractVersion"], "STM32 update contract differs")
    require(numeric_constant("include/stm32_update/stm32_package.h",
                             "STM32_PACKAGE_MAX_PACKAGE_SIZE") ==
            update["maximumPackageBytes"], "STM32 package size contract differs")
    require(numeric_constant("include/stm32_update/stm32_package.h",
                             "STM32_PACKAGE_MAX_FIRMWARE_SIZE") ==
            update["maximumFirmwareBytes"], "STM32 firmware size contract differs")
    signing_key = string_constant(
        "include/stm32_update/stm32_package_keys.h",
        "STM32_PACKAGE_EXPECTED_SIGNATURE_KEY_ID",
    )
    require(signing_key in update["acceptedSignatureKeyIds"],
            "STM32 signing key is absent from the compatibility contract")
    require(update["minimumEsp32FirmwareVersionPolicy"] == "informational",
            "custom forks must use the capability contract as the machine gate")
    package_policy = source("src/stm32_update/stm32_package_internal.cpp")
    require("manifest.minEsp32UpdateContract > STM32_UPDATE_CONTRACT_VERSION" in
            package_policy, "STM32 package capability contract is not enforced")
    manager = source("src/stm32_update/stm32_update_manager.cpp")
    require(f'"{update["scratchPartition"]["label"]}"' in manager,
            "STM32 scratch partition label differs")
    require("(esp_partition_subtype_t)0x40" in manager,
            "STM32 scratch partition subtype differs")

    partitions = parse_partition_table()
    for partition in contract["esp32Ota"]["requiredPartitions"]:
        verify_partition(partitions, partition)
    verify_partition(partitions, update["scratchPartition"])
    exact_layout = contract["factoryInitial"]["officialPartitionLayout"]
    require(set(partitions) == {str(item["label"]) for item in exact_layout},
            "official partition labels differ")
    for partition in exact_layout:
        verify_exact_partition(partitions, partition)
    ota = contract["esp32Ota"]
    app_slot_size = min(int(partitions[label]["size"]) for label in ("app0", "app1"))
    largest_budget = max(int(value) for value in ota["firmwareBudgetsBytes"].values())
    require(largest_budget + int(ota["minimumSlotHeadroomBytes"]) <= app_slot_size,
            "ESP32 firmware budget leaves too little OTA slot headroom")
    require(int(partitions[update["scratchPartition"]["label"]]["size"]) >=
            update["maximumPackageBytes"], "scratch partition cannot hold max package")
    sizing = update["packageSizing"]
    chunks = ((int(update["maximumFirmwareBytes"]) + int(sizing["chunkSizeBytes"]) - 1) //
              int(sizing["chunkSizeBytes"]))
    maximum_sized_package = (
        int(update["maximumFirmwareBytes"])
        + chunks * int(sizing["encryptedFrameOverheadBytes"])
        + chunks * int(sizing["chunkTableEntryBytes"])
        + int(sizing["packageHeaderBytes"])
        + int(sizing["manifestBudgetBytes"])
    )
    require(maximum_sized_package <= int(update["maximumPackageBytes"]),
            "scratch partition cannot hold the maximum firmware package budget")


def read_package(path: Path) -> tuple[dict[str, object], int]:
    payload = path.read_bytes()
    require(len(payload) >= PACKAGE_HEADER_SIZE, "STM32 package is shorter than its header")
    require(payload[:8] == b"ULSASTM1", "STM32 package magic differs")
    manifest_length = struct.unpack_from("<I", payload, 12)[0]
    package_length = struct.unpack_from("<I", payload, 24)[0]
    require(package_length == len(payload), "STM32 package length header differs")
    manifest_end = PACKAGE_HEADER_SIZE + manifest_length
    require(manifest_end <= len(payload), "STM32 package manifest is truncated")
    return json.loads(payload[PACKAGE_HEADER_SIZE:manifest_end]), len(payload)


def verify_package(contract: dict[str, object], path: Path) -> dict[str, object]:
    manifest, package_size = read_package(path)
    update = contract["stm32Update"]
    require(package_size <= update["maximumPackageBytes"],
            "STM32 package exceeds the ESP32 scratch/package limit")
    require(int(manifest["size"]) <= update["maximumFirmwareBytes"],
            "STM32 firmware exceeds the ESP32 plaintext limit")
    require(manifest["target"] == update["target"], "STM32 package target differs")
    require(manifest["baseAddress"] == update["baseAddress"],
            "STM32 package base address differs")
    require(manifest["payloadEncoding"] == update["payloadEncoding"],
            "STM32 package encoding differs")
    require(int(manifest["customBootloaderProtocol"]) ==
            update["customBootloaderProtocol"], "STM32 loader protocol differs")
    require(int(manifest["minEsp32UpdateContract"]) <=
            update["esp32UpdateContractVersion"],
            "STM32 package needs a newer ESP32 update contract")
    parse_version(manifest["minEsp32Fw"])
    require(manifest["signatureKeyId"] in update["acceptedSignatureKeyIds"],
            "STM32 package signing key is not accepted")
    return {
        "path": str(path),
        "packageBytes": package_size,
        "firmwareBytes": int(manifest["size"]),
        "minEsp32Fw": manifest["minEsp32Fw"],
        "minEsp32UpdateContract": int(manifest["minEsp32UpdateContract"]),
        "signatureKeyId": manifest["signatureKeyId"],
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stm32-package", type=Path)
    args = parser.parse_args()
    contract = json.loads(CONTRACT_PATH.read_text(encoding="utf-8"))
    verify_sources(contract)
    print(f"Compatibility source contract passed: {contract['contractId']}")
    if args.stm32_package:
        result = verify_package(contract, args.stm32_package.resolve())
        print(
            "STM32 package is compatible: "
            f"package={result['packageBytes']} bytes, "
            f"firmware={result['firmwareBytes']} bytes, "
            f"minEsp32Fw={result['minEsp32Fw']}, "
            f"updateContract={result['minEsp32UpdateContract']}, "
            f"signatureKeyId={result['signatureKeyId']}"
        )


if __name__ == "__main__":
    try:
        main()
    except (KeyError, OSError, RuntimeError, ValueError, json.JSONDecodeError) as error:
        raise SystemExit(f"Compatibility verification failed: {error}")
