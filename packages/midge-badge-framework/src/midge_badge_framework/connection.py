import asyncio
import io
import logging
import zlib
from collections.abc import Iterator
from ctypes import c_uint8, sizeof
from enum import Enum
from itertools import count, takewhile

from bleak import BleakClient, BleakScanner
from bleak.backends.characteristic import BleakGATTCharacteristic
from bleak.backends.device import BLEDevice
from bleak.backends.scanner import AdvertisementData

from .protocol import (
    CMD_RESPONSES,
    EOT,
    INTERFACE_MAX_FILE_NAME,
    SOT,
    CmdDownloadFileChunkRequest,
    CmdDownloadFileChunkResponse,
    CmdGetFileCRC32Request,
    CmdGetFileCRC32Response,
    CmdGetFileIndexInfoRequest,
    MidgeBadgeCommand,
)

UART_SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
UART_RX_CHAR_UUID = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
UART_TX_CHAR_UUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

logger = logging.getLogger(__name__)


# TIP: you can get this function and more from the ``more-itertools`` package.
def sliced(data: bytes, n: int) -> Iterator[bytes]:
    """
    Slices *data* into chunks of size *n*. The last slice may be smaller than
    *n*.
    """
    return takewhile(len, (data[i : i + n] for i in count(0, n)))


class MidgeBadgeQueue(asyncio.Queue):
    def __init__(self, async_loop):
        self.async_loop = async_loop
        super().__init__()

    def put_sync(self, item):
        async def __put_item():
            await self.put(item)

        asyncio.run_coroutine_threadsafe(__put_item(), self.async_loop)

    def get_sync(self, timeout=120):
        async def __get_item():
            return await self.get()

        fut = asyncio.run_coroutine_threadsafe(__get_item(), self.async_loop)
        return fut.result(timeout=timeout)


class NotifyState(Enum):
    READ_SOT = 0
    READ_CMD = 1
    READ_DATA = 2
    READ_EOT = 3


class BleakClientModified(BleakClient):
    async def __aexit__(self, *args):
        try:
            return await super().__aexit__(*args)
        except EOFError as _:
            # Handle EOFError that can occur on some platforms when the connection is closed
            return None


class MidgeBadgeClient:
    def __init__(self, address=None, reserved_macs=None):
        self.address = address
        self.device = None
        self.connected = False
        self.__request_queue = None
        self.__response_queue = None
        self.__reserved_macs = reserved_macs if reserved_macs is not None else []

    def get_address(self) -> str:
        return self.address

    def get_connected(self) -> bool:
        return self.connected

    def __find_filter(self, device: BLEDevice, adv: AdvertisementData):
        nus_present = UART_SERVICE_UUID.lower() in adv.service_uuids
        addr_match = device.address == self.address
        any_found = False
        logger.debug("%s %s", device, self.address)
        if nus_present and self.address is None and device.address not in self.__reserved_macs:
            self.address = device.address
            any_found = True
        return nus_present and (addr_match or any_found)

    def __handle_disconnect(self, client: BleakClient):
        logger.info("Disconnected %s", client.address)
        self.connected = False
        # for task in asyncio.all_tasks(self.__loop):
        #    task.cancel()

    def __handle_tx_notify(self, _: BleakGATTCharacteristic, data: bytearray):
        logger.debug("got %s", data)
        for byte in data:
            match self.__tx_notify_state:
                case NotifyState.READ_SOT:
                    if byte == SOT[0]:
                        self.__tx_notify_state = NotifyState.READ_CMD
                case NotifyState.READ_CMD:
                    self.__cmd = None

                    for response in CMD_RESPONSES:
                        logger.debug("id %s, byte %s", response.id(), byte)
                        if response.id() == byte:
                            self.__cmd = response
                            break

                    logger.debug("%s", self.__cmd)
                    if (self.__cmd) is None:
                        logger.error("Error: received trash data")
                        self.__tx_notify_state = NotifyState.READ_SOT
                    else:
                        self.__response_buffer = bytearray()
                        self.__response_buffer_len = sizeof(self.__cmd)
                        logger.debug("Expecting %s bytes of data", self.__response_buffer_len)

                        self.__response_buffer_idx = 0
                        self.__tx_notify_state = NotifyState.READ_DATA
                case NotifyState.READ_DATA:
                    self.__response_buffer.append(byte)
                    self.__response_buffer_idx += 1
                    if not (self.__response_buffer_idx < self.__response_buffer_len):
                        self.__tx_notify_state = NotifyState.READ_EOT
                case NotifyState.READ_EOT:
                    if byte == EOT[0]:
                        # make into command and return
                        response = self.__cmd()
                        buf = io.BytesIO(self.__response_buffer)
                        buf.readinto(response)
                        self.__response_queue.put_sync(response)
                    else:
                        logger.error("bad response termination")

                    self.__tx_notify_state = NotifyState.READ_SOT
                case _:
                    logger.error("Invalid state")

    async def start(self):
        def filter(device, adv):
            return self.__find_filter(device, adv)

        self.device = await BleakScanner.find_device_by_filter(filter)
        if self.device is None:
            return

        self.address = self.device.address

        def disconnect_cb(client):
            return self.__handle_disconnect(client)

        async with BleakClientModified(self.device, disconnected_callback=disconnect_cb) as client:
            logger.info("Connected to midge %s", self.device.address)
            self.__tx_notify_state = NotifyState.READ_SOT
            self.__tx_notify_buffer = bytearray()

            def tx_callback(x, y):
                return self.__handle_tx_notify(x, y)

            await client.start_notify(UART_TX_CHAR_UUID, tx_callback)
            nus = client.services.get_service(UART_SERVICE_UUID)
            assert nus is not None, "NUS not found"
            rx_characteristic = nus.get_characteristic(UART_RX_CHAR_UUID)
            assert rx_characteristic is not None, "NUS RX characteristic not found"

            self.__loop = asyncio.get_running_loop()
            self.__request_queue = MidgeBadgeQueue(self.__loop)
            self.__response_queue = MidgeBadgeQueue(self.__loop)
            self.connected = True

            while self.connected:
                try:
                    async with asyncio.Timeout(2):
                        request = await self.__request_queue.get()
                        logger.debug("%s %s", self.address, request)
                except Exception as _:
                    continue

                buffer = bytearray(SOT)
                buffer += bytes([request.__class__.id()])
                buffer += bytes(request)
                buffer += EOT

                for s in sliced(buffer, rx_characteristic.max_write_without_response_size):
                    await client.write_gatt_char(rx_characteristic, s, response=False)

                logger.debug("sent to %s: %s", self.address, buffer)

    def stop(self):
        self.connected = False

    def send_command(self, request: MidgeBadgeCommand):
        self.__request_queue.put_sync(request)

    def get_response(self, timeout=120):
        return self.__response_queue.get_sync(timeout=timeout)

    def execute_command_log_resp(self, request: MidgeBadgeCommand):
        self.send_command(request)
        logger.info("%s %s", self.address, self.get_response())

    # Utility functions for complex behavior

    def list_files(self, log_list=False) -> list[str]:
        index = 0
        paths = []
        while True:
            request = CmdGetFileIndexInfoRequest(index)
            self.send_command(request)
            response = self.get_response()
            if response.index < 0:
                # Got to the final entry
                # logger.error("File not found? %s", response.index)
                break
            path_str = bytes(response.path).rstrip(b"\x00").decode("utf-8")
            paths.append(path_str)
            if log_list:
                logger.info("%s %s bytes", path_str, response.size_bytes)
            index += 1
        return paths

    def download_file(self, path: str, outfile: str):
        path_bytes = path.encode("utf-8")
        path_type = c_uint8 * INTERFACE_MAX_FILE_NAME
        if len(path_bytes) >= INTERFACE_MAX_FILE_NAME:
            logger.error("Path must be at most %d bytes long", INTERFACE_MAX_FILE_NAME)
            return
        path_bytes = path_type(*path_bytes)

        file_data = bytearray()
        offset = 0
        while True:
            self.send_command(CmdDownloadFileChunkRequest(path_bytes, offset))
            resp = self.get_response()
            if not isinstance(resp, CmdDownloadFileChunkResponse):
                logger.error("Unexpected response type: %s", resp.__class__)
                break
            if resp.bytes < 0:
                logger.error("Error downloading file: %d", resp.bytes)
                break
            if resp.bytes == 0:
                if offset == 0:
                    logger.warning("File is empty")
                break

            logger.debug("Rx %d bytes off %d", resp.bytes, offset)
            file_data.extend(resp.data[0 : resp.bytes])
            offset += resp.bytes

        self.send_command(CmdGetFileCRC32Request(path_bytes))
        resp = self.get_response()
        if not isinstance(resp, CmdGetFileCRC32Response):
            logger.error("Unexpected response type: %s", resp.__class__)
            return

        if resp.status_code != 0:
            logger.error("Error getting file CRC32: %d", resp.status_code)
            return
        crc32 = resp.crc32
        logger.info("Expected CRC32: %08x", crc32)

        # verify CRC32
        crc32_calculated = zlib.crc32(file_data, 0)
        logger.debug("File data: %s", file_data)
        logger.info("Calculated CRC32: %08x", crc32_calculated)
        if crc32_calculated != crc32:
            logger.error("CRC32 mismatch: expected %08x, got %08x", crc32, crc32_calculated)
            return

        logger.info("CRC32 verified successfully")
        with open(outfile, "wb") as f:
            f.write(file_data)
