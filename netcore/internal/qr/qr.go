// Package qr decodes QR codes (authenticator exports) from grayscale pixels.
package qr

import (
	"errors"
	"image"

	"github.com/makiuchi-d/gozxing"
	"github.com/makiuchi-d/gozxing/qrcode"
)

// DecodeGray decodes the first QR code in a w×h 8-bit grayscale buffer.
func DecodeGray(pix []byte, w, h int) (string, error) {
	if w <= 0 || h <= 0 || len(pix) < w*h || w > 8192 || h > 8192 {
		return "", errors.New("qr: bad image")
	}
	img := &image.Gray{Pix: pix[:w*h], Stride: w, Rect: image.Rect(0, 0, w, h)}
	bmp, err := gozxing.NewBinaryBitmapFromImage(img)
	if err != nil {
		return "", err
	}
	hints := map[gozxing.DecodeHintType]interface{}{gozxing.DecodeHintType_TRY_HARDER: true}
	res, err := qrcode.NewQRCodeReader().Decode(bmp, hints)
	if err != nil {
		return "", errors.New("qr: no QR code found")
	}
	return res.GetText(), nil
}
