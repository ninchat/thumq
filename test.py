#!/usr/bin/env python

from __future__ import absolute_import, print_function

import argparse
import base64
import os
import subprocess
import sys
import tempfile
import time
import webbrowser
from contextlib import closing

import zmq

socket_path = "test.socket"
socket_addr = "ipc://" + socket_path

imagedir = "exif-orientation-examples"

protoc = os.environ.get("PROTOC", "protoc")

compiledir = tempfile.mkdtemp()
try:
    subprocess.check_call([protoc, "--python_out", compiledir, "thumq.proto"])
    sys.path.insert(0, compiledir)
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

    if args.top_square:
        request.crop = thumq_pb2.Request.TOP_SQUARE

    request_data = request.SerializeToString()

    service = subprocess.Popen(["./thumq", socket_addr])
    try:
        context = zmq.Context()
        try:
            with closing(context.socket(zmq.REQ)) as socket:
                socket.connect(socket_addr)

                for kind in ["Landscape", "Portrait"]:
                    for num in xrange(1, 8 + 1):
                        filename = "{}_{}.jpg".format(kind, num)
                        filepath = os.path.join(imagedir, filename)

                        with open(filepath) as f:
                            input_data = f.read()

                        socket.send_multipart([request_data, input_data])
                        response_data, output_data = socket.recv_multipart()

                        response = thumq_pb2.Response.FromString(response_data)
                        assert response.original_format == "JPEG"
                        assert output_data

                        if args.browser:
                            output_b64 = base64.standard_b64encode(output_data)
                            webbrowser.open_new_tab("data:image/jpeg;base64," + output_b64)

                            time.sleep(1)
        finally:
            context.term()
    finally:
        service.terminate()
        service.wait()

        os.remove(socket_path)


if __name__ == "__main__":
    main()
