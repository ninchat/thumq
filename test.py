#!/usr/bin/env python3

import argparse
import base64
import os
import signal
import socket
import subprocess
import sys
import tempfile
import time
import webbrowser
from contextlib import closing
from struct import pack, unpack

socket_path = "./test.socket"

imagedir = "exif-orientation-examples"

protoc = os.environ.get("PROTOC", "protoc")

compiledir = tempfile.mkdtemp()
try:
    subprocess.check_call([protoc, "--python_out", compiledir, "thumq.proto"])
    sys.path.insert(0, compiledir)
    sys.dont_write_bytecode = True
    import thumq_pb2
    sys.path.pop(0)
finally:
    for dirpath, dirnames, filenames in os.walk(compiledir, topdown=False):
        for filename in filenames:
            os.remove(os.path.join(dirpath, filename))
        for dirname in dirnames:
            os.rmdir(os.path.join(dirpath, dirname))
        os.rmdir(compiledir)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--browser", action="store_true", help="open result images in web browser tabs")
    parser.add_argument("--top-square", action="store_true", help="enable cropping")
    parser.add_argument("scale", type=int, help="maximum width/height of result image")
    args = parser.parse_args()

    request = thumq_pb2.Request()
    request.scale = args.scale

    crop = "no-crop"
    if args.top_square:
        request.crop = thumq_pb2.Request.TOP_SQUARE
        crop = "top-square"

    request_data = request.SerializeToString()

    service = subprocess.Popen(["./thumq", socket_path])
    try:
        for _ in range(10):
            if os.path.exists(socket_path):
                break
            time.sleep(0.2)

        files = []

        for kind in ["Landscape", "Portrait"]:
            for num in range(1, 8 + 1):
                filename = "{}_{}.jpg".format(kind, num)
                filepath = os.path.join(imagedir, filename)
                files.append((filepath, "image/jpeg", True))

        files.append(("test.pdf", "application/pdf", False))

        for filepath, expect_type, expect_thumbnail in files:
            print(filepath)

            sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            with closing(sock):
                sock.connect(socket_path)

                send(sock, request_data)
                with open(filepath, "rb") as f:
                    send(sock, f.read())

                response = thumq_pb2.Response.FromString(receive(sock))
                output_data = receive(sock)

                if expect_thumbnail:
                    assert response.source_type == expect_type, response
                    assert response.nail_width in range(1, args.scale + 1), response
                    assert response.nail_height in range(1, args.scale + 1), response
                    assert output_data

                    if args.browser:
                        output_b64 = base64.standard_b64encode(output_data).decode()
                        webbrowser.open_new_tab("data:image/jpeg;base64," + output_b64)
                    else:
                        with open(filepath.replace(imagedir + "/", "test-output/" + crop + "/"), "wb") as f:
                            f.write(output_data)
                else:
                    assert response.source_type == expect_type, response
                    assert not response.nail_width, response
                    assert not response.nail_height, response
                    assert not output_data
    finally:
        os.kill(service.pid, signal.SIGINT)
        service.wait()


def send(sock, data):
    sock.send(pack("<I", len(data)))
    sock.send(data)


def receive(sock):
    size, = unpack("<I", sock.recv(4))
    return sock.recv(size)


if __name__ == "__main__":
    main()
