//https://stackoverflow.com/a/23303847
#include "bitmap.h"
#include <string.h>
#include <stdlib.h>

/* 24 bit values */
//#define _bitsperpixel 24
//#define _compression BI_RGB //0

//const uint32_t bits555[] = {0x7C00,0x03E0,0x001F};
//const uint32_t bits565[] = {0xF800,0x07E0,0x001F};


// These are bit field masks for true colour devices

const uint32_t bits555[] = {0x007C00,0x0003E0,0x00001F};
const uint32_t bits565[] = {0x00F800,0x0007E0,0x00001F};
const uint32_t bits888[] = {0xFF0000,0x00FF00,0x0000FF};


#define _planes 1
#define _xpixelpermeter 0x130B //2835 , 72 DPI
#define _ypixelpermeter 0x130B //2835 , 72 DPI
#define pixel 0xFF

char *bmp_create_header(int w, int h)
{
	const int _bitsperpixel = 24;
	const int _compression = BI_RGB; //0

	bitmap *pbitmap  = (bitmap*)calloc(1, sizeof(bitmap));
	int _pixelbytesize = w * h * _bitsperpixel/8;
	int _filesize = _pixelbytesize+sizeof(bitmap);
	strcpy((char*)pbitmap->fileheaderb.signature, "BM");
	pbitmap->fileheaderb.filesize = _filesize;
	pbitmap->fileheaderb.fileoffset_to_pixelarray = sizeof(bitmap);
	pbitmap->bitmapinfoheaderb.dibheadersize = sizeof(bitmapinfoheader);
	pbitmap->bitmapinfoheaderb.width = w;
//	pbitmap->bitmapinfoheader.height = (-1)*h; // should be -ve height if written from top to bottom
  pbitmap->bitmapinfoheaderb.height = h;
	pbitmap->bitmapinfoheaderb.planes = _planes;
	pbitmap->bitmapinfoheaderb.bitsperpixel = _bitsperpixel;
	pbitmap->bitmapinfoheaderb.compression = _compression;
	pbitmap->bitmapinfoheaderb.imagesize = _pixelbytesize;
	pbitmap->bitmapinfoheaderb.ypixelpermeter = _ypixelpermeter ;
	pbitmap->bitmapinfoheaderb.xpixelpermeter = _xpixelpermeter ;
	pbitmap->bitmapinfoheaderb.numcolorspallette = 0;
	return (char *)pbitmap;
}

char *bmp_create_header565(int w, int h)
{
	const int _bitsperpixel = 16;

	bitmap565 *pbitmap  = (bitmap565*)calloc(1, sizeof(bitmap565));
	int _pixelbytesize = w * h * _bitsperpixel/8;
	int _filesize = _pixelbytesize+sizeof(bitmap565);
	// bitmap565 is 12 bytes larger than std bitmap
	strcpy((char*)pbitmap->fileheaderb.signature, "BM");
	pbitmap->fileheaderb.filesize = _filesize;
	pbitmap->fileheaderb.fileoffset_to_pixelarray = sizeof(bitmap565);
	pbitmap->bitmapinfoheaderb.dibheadersize = sizeof(bitmapinfoheader);
	pbitmap->bitmapinfoheaderb.width = w;
//	pbitmap->bitmapinfoheader.height = (-1)*h; // should be -ve height if written from top to bottom
	pbitmap->bitmapinfoheaderb.height = h;
	pbitmap->bitmapinfoheaderb.planes = _planes;
	pbitmap->bitmapinfoheaderb.bitsperpixel = _bitsperpixel;
	pbitmap->bitmapinfoheaderb.compression = BI_BITFIELDS;
	pbitmap->bitmapinfoheaderb.imagesize = 0; //_pixelbytesize;
	pbitmap->bitmapinfoheaderb.ypixelpermeter = _ypixelpermeter ;
	pbitmap->bitmapinfoheaderb.xpixelpermeter = _xpixelpermeter ;
	pbitmap->bitmapinfoheaderb.numcolorspallette = 0;
	pbitmap->bitmapinfoheaderb.BF1 = bits565[0];
	pbitmap->bitmapinfoheaderb.BF2 = bits565[1];
	pbitmap->bitmapinfoheaderb.BF3 = bits565[2];
	return (char *)pbitmap;
}
