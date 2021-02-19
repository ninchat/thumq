// Copyright (c) 2020 Somia Reality Oy. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package main

import (
	"bytes"
	"encoding/binary"
	"errors"
	"flag"
	"image"
	_ "image/gif"
	"image/jpeg"
	_ "image/png"
	"io"
	"log"
	"net"
	"net/http"
	"os"
	"os/signal"
	"sync"
	"sync/atomic"
	"syscall"

	"github.com/disintegration/imaging"
	jpegstructure "github.com/dsoprea/go-jpeg-image-structure"
	"github.com/ninchat/thumq"
	_ "golang.org/x/image/bmp"
	"google.golang.org/protobuf/proto"
)

const exifTagIDOrientation = 0x112

type orientation uint16

const (
	_                  orientation = iota
	orientationTopLeft             // Upper left.
	orientationTopRight
	orientationBottomRight // Lower right.
	orientationBottomLeft
	orientationLeftTop
	orientationRightTop // Upper right.
	orientationRightBottom
	orientationLeftBottom // Lower left.
)

type subImager interface {
	SubImage(image.Rectangle) image.Image
}

func main() {
	log.SetFlags(0)

	returned := false

	defer func() {
		if !returned {
			x := recover()
			if err, ok := x.(error); ok {
				log.Fatal(err)
			} else {
				panic(x)
			}
		}
	}()

	panickyMain()
	returned = true
}

func panickyMain() {
	flag.Parse()
	if flag.NArg() != 1 {
		panic(errors.New("need exactly one argument: listen socket filename"))
	}
	address := flag.Arg(0)

	signals := make(chan os.Signal)
	signal.Notify(signals, syscall.SIGINT, syscall.SIGTERM)
	signal.Ignore(syscall.SIGPIPE)

	listener, err := net.Listen("unix", address)
	check(err)
	defer listener.Close()

	var shutdown uint32

	go func() {
		<-signals
		atomic.StoreUint32(&shutdown, 1)
		listener.Close()
	}()

	var group sync.WaitGroup
	defer group.Wait()

	for {
		conn, err := listener.Accept()
		if err != nil {
			if atomic.LoadUint32(&shutdown) == 1 {
				return
			}
			panic(err)
		}

		group.Add(1)
		go func() {
			defer group.Done()
			handle(conn)
		}()
	}
}

func handle(conn net.Conn) {
	defer func() {
		if x := recover(); x != nil {
			if err, ok := x.(error); ok {
				log.Print(err)
			} else {
				panic(x)
			}
		}
	}()

	defer conn.Close()

	req := new(thumq.Request)
	check(proto.Unmarshal(receive(conn), req))

	res, data := process(req, receive(conn))

	head, err := proto.Marshal(res)
	check(err)

	send(conn, head)
	send(conn, data)
}

func receive(conn net.Conn) []byte {
	size := make([]byte, 4)

	_, err := io.ReadFull(conn, size)
	check(err)

	buf := make([]byte, binary.LittleEndian.Uint32(size))

	_, err = io.ReadFull(conn, buf)
	check(err)

	return buf
}

func send(conn net.Conn, buf []byte) {
	size := make([]byte, 4)
	binary.LittleEndian.PutUint32(size, uint32(len(buf)))

	_, err := conn.Write(size)
	check(err)

	_, err = conn.Write(buf)
	check(err)
}

func process(req *thumq.Request, data []byte) (*thumq.Response, []byte) {
	res := new(thumq.Response)

	m, format, err := image.Decode(bytes.NewReader(data))
	if err != nil {
		res.SourceType = detectMIMEType(data)
		return res, nil
	}

	var ori orientation

	switch format {
	case "jpeg":
		res.SourceType = "image/jpeg"
		ori = parseJPEGOrientation(data)

	case "bmp", "gif", "png":
		res.SourceType = "image/" + format

	default: // Imports may have registerd unexpected handlers.
		log.Print("decoded image in unsupported format:", format)
		res.SourceType = detectMIMEType(data)
		return res, nil
	}

	m = convert(m, req, res, ori)

	buf := new(bytes.Buffer)
	check(jpeg.Encode(buf, m, nil))
	return res, buf.Bytes()
}

func parseJPEGOrientation(data []byte) (ori orientation) {
	media, _ := jpegstructure.NewJpegMediaParser().ParseBytes(data)
	if media == nil {
		return
	}

	_, _, tags, _ := media.(*jpegstructure.SegmentList).DumpExif()

	for _, tag := range tags {
		if tag.TagId == exifTagIDOrientation {
			if array, ok := tag.Value.([]uint16); ok && len(array) != 0 {
				ori = orientation(array[0])
			}
			break
		}
	}

	return
}

func convert(m image.Image, req *thumq.Request, res *thumq.Response, ori orientation) image.Image {
	m = fixOrientation(m, ori)

	if req.Crop == thumq.Request_TOP_SQUARE {
		m = cropTopSquare(m)
	}

	m = scale(m, int(req.Scale))

	size := m.Bounds()
	res.NailWidth = uint32(size.Dx())
	res.NailHeight = uint32(size.Dy())
	return m
}

func fixOrientation(m image.Image, ori orientation) image.Image {
	switch ori {
	case orientationTopLeft:

	case orientationTopRight:
		m = imaging.FlipH(m)

	case orientationBottomRight:
		m = imaging.Rotate180(m)

	case orientationBottomLeft:
		m = imaging.FlipV(m)

	case orientationLeftTop:
		m = imaging.FlipV(m)
		m = imaging.Rotate270(m)

	case orientationRightTop:
		m = imaging.Rotate270(m)

	case orientationRightBottom:
		m = imaging.FlipV(m)
		m = imaging.Rotate90(m)

	case orientationLeftBottom:
		m = imaging.Rotate90(m)
	}

	return m
}

func cropTopSquare(m image.Image) image.Image {
	var rect image.Rectangle

	size := m.Bounds()
	width := size.Dx()
	height := size.Dy()

	if width < height {
		rect = image.Rect(0, 0, width, width)
	} else {
		x := (width - height) / 2
		rect = image.Rect(x, 0, x+height, height)
	}

	return m.(subImager).SubImage(rect)
}

func scale(m image.Image, scale int) image.Image {
	size := m.Bounds()
	width := size.Dx()
	height := size.Dy()

	if width > scale || height > scale {
		if width < height {
			width = int(int64(scale) * int64(width) / int64(height))
			height = scale
		} else {
			height = int(int64(scale) * int64(height) / int64(width))
			width = scale
		}

		m = imaging.Resize(m, width, height, imaging.Lanczos)
	}

	return m
}

func detectMIMEType(data []byte) string {
	switch mime := http.DetectContentType(data); mime {
	case "application/octet-stream": // Unknown, really.
		return ""

	default:
		return mime
	}
}

func check(err error) {
	if err != nil {
		panic(err)
	}
}
