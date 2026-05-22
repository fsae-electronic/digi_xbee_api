from __future__ import annotations

import argparse
import logging
import queue
import time
from dataclasses import dataclass
from typing import Callable, Optional

from digi.xbee.devices import RemoteXBeeDevice, XBeeDevice
from digi.xbee.exception import XBeeException
from digi.xbee.models.address import XBee64BitAddress, XBee16BitAddress
from digi.xbee.models.options import TransmitOptions, RemoteATCmdOptions

logger = logging.getLogger(__name__)

DEFAULT_REMOTE_64BIT_ADDRESS = "0013A20040B2B3D4"
DEFAULT_REMOTE_16BIT_ADDRESS = "FFFE"


@dataclass(frozen=True)
class Message:
    type: str
    timestamp_utc: float
    source_64bit: Optional[str]
    rssi: Optional[int]
    payload: bytes

    @property
    def payload_hex(self) -> str:
        return self.payload.hex()


class XBee:
    def __init__(
        self,
        port: str = "/dev/ttyUSB0",
        baud: int = 115200,
        tx_mode: str = "async",
        rx_mode: str = "async",
        remote_64bit_address: str = DEFAULT_REMOTE_64BIT_ADDRESS,
        remote_16bit_address: str = DEFAULT_REMOTE_16BIT_ADDRESS,
        on_message_rx: Callable[[Message], None] | None = None,
        on_message_tx: Callable[[bytes], None] | None = None,
    ) -> None:
        self.port = port
        self.baud = baud
        self.tx_mode = tx_mode
        self.rx_mode = rx_mode
        self.remote_64bit_address = remote_64bit_address
        self.remote_16bit_address = remote_16bit_address
        self.on_message_rx = on_message_rx
        self.on_message_tx = on_message_tx

        self.device: XBeeDevice | None = None
        self.remote_device: RemoteXBeeDevice | None = None
        self._tx: Callable[[RemoteXBeeDevice, str | bytearray, TransmitOptions], None] | None = None
        self.rx_messages: queue.Queue[Message] = queue.Queue()

    def start(self):
        if self.device is not None:
            return

        self.device = XBeeDevice(self.port, self.baud)
        self.remote_device = RemoteXBeeDevice(
            self.device,
            XBee64BitAddress.from_hex_string(self.remote_64bit_address),
            XBee16BitAddress.from_hex_string(self.remote_16bit_address),
        )

        try:
            self.device.open(force_settings=True)
        except XBeeException as e:
            logger.error("Error opening XBee device: %s", e)
            raise

        logger.info("XBee device opened on %s at %s baud", self.port, self.baud)
        logger.info(
            "Local XBee address: %s | Remote XBee address: %s",
            self.device.get_64bit_addr(),
            self.remote_64bit_address,
        )

        self._tx = (
            self.device.send_data_async if self.tx_mode == "async" else self.device.send_data
        )
        if self.rx_mode == "async":
            self.device.add_data_received_callback(self._rx_async)

    def stop(self) -> None:
        if self.device is not None and self.device.is_open():
            try:
                self.device.close()
            finally:
                self.device = None
                self.remote_device = None
                if self._tx is not None:
                    self._tx = None
                if self.rx_mode == "async":
                    self.device.remove_data_received_callback(self._rx_async)

    def tx(self, payload: bytes, timeout: int | None = None) -> None:
        if self.device is None or not self.device.is_open():
            raise RuntimeError("XBee device is not open")

        try:
            # TODO: Specify transmit options
            if self.tx_mode == "sync":
                self.device.set_sync_ops_timeout(timeout)
            self.device._tx(self.remote_device, payload, TransmitOptions.NONE)
        except XBeeException as e:
            logger.error("Error sending data: %s", e)
            return

        if self.on_message_tx is not None and self.tx_mode == "async":
            self.on_message_tx(payload)

    def _parse(self, xbee_message) -> Message:
        received = Message(
            type="rx",
            timestamp_utc=time.time(),
            rssi=getattr(xbee_message, "rssi", None),
            payload=bytes(xbee_message.data),
        )
        logger.info(
            "Received from %s RSSI=%s len=%d payload=%s",
            received.source_64bit or self.remote_64bit_address,
            received.rssi,
            len(received.payload),
            received.payload_hex,
        )
        return received

    def rx(self, timeout: int | None = None) -> Message | None:
        if self.rx_mode == "async":
            try:
                return self.rx_messages.get(timeout=timeout)
            except queue.Empty:
                return None
        else:
            if self.device is None or not self.device.is_open():
                raise RuntimeError("XBee device is not open")

            try:
                xbee_message = self.device.read_data_from(
                    self.remote_device, timeout=timeout
                )
                if xbee_message is None:
                    return None
                return self._parse(xbee_message)
            except XBeeException as e:
                logger.error("Error reading from XBee: %s", e)
                return None

    def _rx(self, xbee_message) -> None:
        received = self._parse(xbee_message)
        if received is None:
            return

        source = (
            str(xbee_message.remote_device.get_64bit_addr())
            if xbee_message.remote_device
            else None
        )
        if source != self.remote_64bit_address:
            logger.warning(
                "Received message from unknown source %s, expected %s",
                xbee_message.remote_device.get_64bit_addr(),
                self.remote_64bit_address,
            )
            return None

        self.rx_messages.put(received)

        if self.on_message_rx is not None:
            self.on_message_rx(received)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--port", required=True, help="Serial port (eg /dev/ttyUSB0 or COM3)"
    )
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate")
    args = parser.parse_args()

    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s [%(levelname)s] %(message)s",
        datefmt="%H:%M:%S",
    )

    xbee = XBee(port=args.port, baud=args.baud, rx_mode="sync", tx_mode="sync")
    xbee.start()
    print("Listening for incoming telemetry (loopback). Ctrl-C to stop.")

    try:
        while True:
            message = xbee.rx(timeout=1.0)
            if message is None:
                continue

            timestamp = time.strftime(
                "%Y-%m-%d %H:%M:%S", time.gmtime(message.timestamp_utc)
            )
            print(
                f"[{timestamp}] "
                # f"From {message.source_64bit} RSSI={message.rssi} "
                f"From {DEFAULT_REMOTE_64BIT_ADDRESS} RSSI={message.rssi} "
                f"len={len(message.payload)} payload={message.payload_hex}"
            )

            xbee.tx(message.payload, timeout=1.0)
    except KeyboardInterrupt:
        print("Exiting")
    finally:
        xbee.stop()
