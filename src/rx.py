# boxes_receiver.py
from digi.xbee.devices import XBeeDevice, RemoteXBeeDevice, XBee64BitAddress
import csv
import time
import argparse


def xbee_rx(port, baud, csv_out):
    device = XBeeDevice(port, baud)

    try:
        device.open()
        print("XBee device opened")

        local_addr = device.get_64bit_addr()
        print("Local XBee address:", local_addr)

        # Tx data will be logged to CSV
        csv_file = open(csv_out, "a", newline="")
        csv_writer = csv.writer(csv_file)
        csv_writer.writerow(["ts_utc", "src64", "rssi", "payload_hex"])

        def data_receive_callback(xbee_message):
            src = (
                xbee_message.remote_device.get_64bit_addr()
                if xbee_message.remote_device
                else "unknown"
            )
            rssi = xbee_message.rssi
            ts = time.time()
            payload = xbee_message.data
            hex_payload = payload.hex()
            print(
                f"[{time.strftime('%Y-%m-%d %H:%M:%S', time.gmtime(ts))}] From {src} RSSI={rssi} len={len(payload)} payload={hex_payload}"
            )
            csv_writer.writerow([ts, str(src), rssi, hex_payload])
            csv_file.flush()

        device.add_data_received_callback(data_receive_callback)

        print("Listening for incoming telemetry. Ctrl-C to stop.")
        while True:
            time.sleep(1)

    except KeyboardInterrupt:
        print("Exiting")
    finally:
        try:
            device.close()
        except:
            pass
        csv_file.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--port", required=True, help="Serial port (eg /dev/ttyUSB0 or COM3)"
    )
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate")
    parser.add_argument("--csv", default="telemetry_log.csv", help="CSV output file")
    args = parser.parse_args()
    xbee_rx(args.port, args.baud, args.csv)
