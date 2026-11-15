package qr

import (
	"image"
	"image/color"
	"testing"

	"github.com/makiuchi-d/gozxing"
	"github.com/makiuchi-d/gozxing/qrcode"
)

func TestRoundTrip(t *testing.T) {
	const want = "otpauth://totp/ACME:alice?secret=GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ&algorithm=SHA512"
	m, err := qrcode.NewQRCodeWriter().Encode(want, gozxing.BarcodeFormat_QR_CODE, 300, 300, nil)
	if err != nil {
		t.Fatal(err)
	}
	b := m.Bounds()
	gray := image.NewGray(b)
	for y := b.Min.Y; y < b.Max.Y; y++ {
		for x := b.Min.X; x < b.Max.X; x++ {
			gray.Set(x, y, color.GrayModel.Convert(m.At(x, y)))
		}
	}
	got, err := DecodeGray(gray.Pix, b.Dx(), b.Dy())
	if err != nil || got != want {
		t.Fatalf("got %q, %v", got, err)
	}
	if _, err := DecodeGray(make([]byte, 100), 10, 10); err == nil {
		t.Fatal("decoded a blank image")
	}
}
