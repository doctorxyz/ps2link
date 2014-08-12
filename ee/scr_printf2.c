/*      
# _____     ___ ____     ___ ____
#  ____|   |    ____|   |        | |____|
# |     ___|   |____ ___|    ____| |    \    PS2DEV Open Source Project.
#-----------------------------------------------------------------------
# Copyright 2001-2004, ps2dev - http://www.ps2dev.org
# Licenced under Academic Free License version 2.0
# Review ps2sdk README & LICENSE files for further details.
#
# $Id: scr_printf.c 1408 2007-06-24 08:58:15Z lukasz $
# EE UGLY DEBUG ON SCREEN
*/

//#include <stdio.h>
//#include <tamtypes.h>
//#include <sifcmd.h>
//#include <kernel.h>
//#include <stdarg.h>
//#include <debug.h>

/* baseado nas libs do Duke... */

static int X = 0, Y = 0;
static int MX=80, MY=27;
static u32 bgcolor = 0;


struct t_setupscr
{
   u64	   dd0[6];
   u32	   dw0[2];
   u64	   dd1[1];
   u16	   dh[4];
   u64	   dd2[21];
};

static struct t_setupscr setupscr __attribute__ (( aligned (16) )) = {
  { 0x100000000000800E, 0xE, 0xA0000, 0x4C, 0x8C, 0x4E },
  { 27648, 30976 },
  { 0x18 },
  { 0, 639, 0, 223 },
  { 0x40, 1, 0x1a, 1, 0x46, 0, 0x45, 0x70000,
    0x47, 0x30000, 0x47, 6, 0, 0x3F80000000000000, 1, 0x79006C00, 5,
    0x87009400, 5, 0x70000, 0x47 }
  };

struct t_setupchar
{
   u64	   dd0[4];
   u32	   dw0[1];
   u16	   x,y;
   u64	   dd1[1];
   u32	   dw1[2];
   u64	   dd2[5];
};

static struct t_setupchar setupchar __attribute__ (( aligned (16) )) = {
  { 0x1000000000000004, 0xE, 0xA000000000000, 0x50 },
  { 0 },
  100, 100, 
  { 0x51 },
  { 8, 8 },
  { 0x52, 0, 0x53, 0x800000000000010, 0}
};

/* charmap must be 16 byte aligned.  */
static u32	charmap[64] __attribute__ (( aligned (16) ));

static void Init_GS( int a, int b, int c)
{
   u64	*mem = (u64 *)0x12001000;
   
   *mem = 0x200;
   GsPutIMR( 0xff00);
   SetGsCrt( a & 1, b & 0xff, c & 1);
}

static void SetVideoMode()
{
  unsigned dma_addr;
  unsigned val1;
  unsigned val2;
  unsigned val3;
  unsigned val4;
  unsigned val4_lo;

  asm volatile ("        .set push               \n"
                "        .set noreorder          \n"
                "        lui     %4, 0x001d      \n"
                "        lui     %5, 0x0183      \n"
                "        lui     %0, 0x1200      \n"
                "        li      %2, 1           \n"
                "        ori     %4, %4, 0xf9ff  \n"
                "        ori     %5, %5, 0x2278  \n"
                "        li      %1, 0xff62      \n"
                "        dsll32  %4, %4, 0       \n"
                "        li      %3, 0x1400      \n"
                "        sd      %1, 0(%0)       \n"
                "        or      %4, %4, %5      \n"
                "        sd      %2, 0x20(%0)    \n"
                "        sd      %3, 0x90(%0)    \n"
                "        sd      %4, 0xa0(%0)    \n"
                "        .set pop                \n"
                : "=&r" (dma_addr), "=&r" (val1), "=&r" (val2),
                "=&r" (val3), "=&r" (val4), "=&r" (val4_lo) );
}

static inline void Dma02Wait()
{
  unsigned dma_addr;
  unsigned status;

  asm volatile ("        .set push               \n"
                "        .set noreorder          \n"
                "        lui   %0, 0x1001        \n"
                "        lw    %1, -0x6000(%0)   \n"
                "1:      andi  %1, %1, 0x100     \n"
                "        bnel  %1, $0, 1b        \n"
                "        lw    %1, -0x6000(%0)   \n"
                "        .set pop                \n"
                : "=&r" (dma_addr), "=&r" (status) );
}

static void DmaReset()
{
  unsigned dma_addr;
  unsigned temp;

  asm volatile ("        .set push               \n"
                "        .set noreorder          \n"
                "        lui   %0, 0x1001        \n"
                "        sw    $0, -0x5f80(%0)   \n"
                "        sw    $0, -0x5000(%0)   \n"
                "        sw    $0, -0x5fd0(%0)   \n"
                "        sw    $0, -0x5ff0(%0)   \n"
                "        sw    $0, -0x5fb0(%0)   \n"
                "        sw    $0, -0x5fc0(%0)   \n"
                "        lui   %1, 0             \n"
                "        ori   %1, %1, 0xff1f    \n"
                "        sw    %1, -0x1ff0(%0)   \n"
                "        lw    %1, -0x1ff0(%0)   \n"
                "        andi  %1, %1, 0xff1f    \n"
                "        sw    %1, -0x1ff0(%0)   \n"
                "        sw    $0, -0x2000(%0)   \n"
                "        sw    $0, -0x1fe0(%0)   \n"
                "        sw    $0, -0x1fd0(%0)   \n"
                "        sw    $0, -0x1fb0(%0)   \n"
                "        sw    $0, -0x1fc0(%0)   \n"
                "        lw    %1, -0x2000(%0)   \n"
                "        ori   %1, %1, 1         \n"
                "        sw    %1, -0x2000(%0)   \n"
		"        .set pop                \n"
		: "=&r" (dma_addr), "=&r" (temp) );
}

/* 
 * addr is the address of the data to be transfered.  addr must be 16
 * byte aligned.
 * 
 * size is the size (in 16 byte quads) of the data to be transfered.
 */

static inline void progdma( void *addr, int size)
{
  unsigned dma_addr;
  unsigned temp;

  asm volatile ("        .set push               \n"
                "        .set noreorder          \n"
                "        lui   %0, 0x1001        \n"
                "        sw    %3, -0x5fe0(%0)   \n"
                "        sw    %2, -0x5ff0(%0)   \n"
                "        li    %1, 0x101         \n"
                "        sw    %1, -0x6000(%0)   \n"
                "        .set pop                \n"
                : "=&r" (dma_addr), "=&r" (temp)
                : "r" (addr), "r" (size) );
}                      

void scr_setbgcolor(u32 color)
{
	bgcolor = color;
}

void init_scr2()
{
   X = Y = 0;
   EI();
   DmaReset(); 
   Init_GS( 0, ((*((char*)0x1FC7FF52))=='E')+2, 1);
   SetVideoMode();
   Dma02Wait();
   progdma( &setupscr, 15);
   FlushCache(2);
}

extern u8 msx[];


void
_putchar( int x, int y, u32 color, u8 ch)
{
   int 	i,j, l;
   u8	*font;
   u32  pixel;
   
   font = &msx[ (int)ch * 8];
   for (i=l=0; i < 8; i++, l+= 8, font++)
      for (j=0; j < 8; j++)
          {
          if ((*font & (128 >> j)))
              pixel = color;
          else
              pixel = bgcolor;
          charmap[ l + j] = pixel; 
          }
   setupchar.x = x;
   setupchar.y = y;

   FlushCache(0);
   progdma( &setupchar, 6);
   Dma02Wait();
   
   progdma( charmap, (8*8*4) / 16);
   Dma02Wait();

}


static void  clear_line( int Y)
{
   int i;
   for (i=0; i < MX; i++)
	//_putchar( i*8 , Y * 8, bgcolor, " ");
	  _putchar( i*7 , Y * 8, bgcolor, 219);
}

void delay(int count)
{
	int i;
	int ret;
	for (i = 0; i < count; i++) {
		ret = 0x01000000;
			while(ret--) asm("nop\nnop\nnop\nnop");
	}
}

void scr_printf2(const char *format, ...)
{
   va_list	opt;
   u8		buff[2048], c;
   int		i, bufsz, j;
   
   
   va_start(opt, format);
   bufsz = vsnprintf( buff, sizeof(buff), format, opt);

   for (i = 0; i < bufsz; i++)
       {
       c = buff[i];
       switch (c)
          {
          case		'\n':
                             X = 0;
                             Y ++;
                             if (Y > MY)
                             {
                                 Y = 0;
								delay(10);
								scr_clear();
                             }
                             clear_line(Y);
                             break;
          case      '\t':
                             for (j = 0; j < 5; j++) {
                             	_putchar( X*7 , Y * 8, 0xffffff, ' ');
                             	X++;
                             }
                             break;
          default:
             		     _putchar( X*7 , Y * 8, 0xffffff, c);
                             X++;
                             if (X == MX)
                                {
                                X = 0;
                                Y++;
                                if (Y > MY)
                                   Y = 0;
                                clear_line(Y);
                                }
          }
       }
    _putchar( X*7 , Y * 8, 0xffffff, 219);
}


void scr_setXY(int x, int y)
{
	if( x<MX && x>=0 ) X=x;
	if( y<MY && y>=0 ) Y=y;
}

int scr_getX()
{
	return X;
}

int scr_getY()
{
	return Y;
}

void scr_clear()
{
	int y;
	for(y=0;y<(MY+1);y++)
		clear_line(y);
	scr_setXY(0,0);
}

/// Interlaced Mode
#define GS_INTERLACED 0x01

/// Field Mode - Reads Every Other Line
#define GS_FIELD 0x00

/// NTSC Full Buffer
#define GS_MODE_NTSC 0x02

/// R8 G8 B8 (RGB24) Texture
#define GS_PSM_CT24 0x01

/// GS Reset Macro
#define GS_RESET() *GS_CSR = ((u64)(1)      << 9)


/// GS CSR (GS System Status) Register
#define GS_CSR (volatile u64 *)0x12001000

/// GS Framebuffer Register (Output Circuit 1)
#define GS_DISPFB1	((volatile u64 *)(0x12000070))

/// GS Framebuffer Register (Output Circuit 2)
#define GS_DISPFB2	((volatile u64 *)(0x12000090))

/// GS PCRTC (Merge Circuit) Registers
#define GS_PMODE	((volatile u64 *)(0x12000000))

/// GS PCRTC (Merge Circuit) Register Access Macro
#define GS_SET_PMODE(EN1,EN2,MMOD,AMOD,SLBG,ALP) \
        *GS_PMODE = \
        ((u64)(EN1)     << 0)   | \
        ((u64)(EN2)     << 1)   | \
        ((u64)(001)     << 2)   | \
        ((u64)(MMOD)    << 5)   | \
        ((u64)(AMOD)	<< 6)	| \
        ((u64)(SLBG)	<< 7)	| \
        ((u64)(ALP)     << 8)

/// GS Framebuffer Register Access Macro (Output Circuit 1)
#define GS_SET_DISPFB1(FBP,FBW,PSM,DBX,DBY) \
        *GS_DISPFB1 = \
        ((u64)(FBP)     << 0)   | \
        ((u64)(FBW)     << 9)   | \
        ((u64)(PSM)     << 15)  | \
        ((u64)(DBX)     << 32)  | \
        ((u64)(DBY)     << 43)

/// GS Framebuffer Register Access Macro (Output Circuit 2)
#define GS_SET_DISPFB2(FBP,FBW,PSM,DBX,DBY) \
        *GS_DISPFB2 = \
        ((u64)(FBP)     << 0)   | \
        ((u64)(FBW)     << 9)   | \
        ((u64)(PSM)     << 15)  | \
        ((u64)(DBX)     << 32)  | \
        ((u64)(DBY)     << 43)

/// GS Display Settings Register (Output Circuit 1)
#define GS_DISPLAY1        ((volatile u64 *)(0x12000080))

/// GS Display Settings Register Access Macro (Output Circuit 1)
#define GS_SET_DISPLAY1(DX,DY,MAGH,MAGV,DW,DH) \
        *GS_DISPLAY1 = \
        ((u64)(DX)      << 0)   | \
        ((u64)(DY)      << 12)  | \
        ((u64)(MAGH)    << 23)  | \
        ((u64)(MAGV)    << 27)  | \
        ((u64)(DW)      << 32)  | \
        ((u64)(DH)      << 44)

/// GS Display Settings Register (Output Circuit 2)
#define GS_DISPLAY2        ((volatile u64 *)(0x120000a0))

/// GS Display Settings Register Access Macro (Output Circuit 2)
#define GS_SET_DISPLAY2(DX,DY,MAGH,MAGV,DW,DH) \
        *GS_DISPLAY2 = \
        ((u64)(DX)      << 0)   | \
        ((u64)(DY)      << 12)  | \
        ((u64)(MAGH)    << 23)  | \
        ((u64)(MAGV)    << 27)  | \
        ((u64)(DW)      << 32)  | \
        ((u64)(DH)      << 44)

/// GS Background Color Register
#define GS_BGCOLOR         ((volatile u64 *)(0x120000e0))

/// GS Background Color Register Access Macro
#define GS_SET_BGCOLOR(R,G,B) \
        *GS_BGCOLOR = \
        ((u64)(R)       << 0)           | \
        ((u64)(G)       << 8)           | \
        ((u64)(B)       << 16)
void init_scr3()
{
	GS_RESET();

	__asm__("sync.p; nop;");
	*GS_CSR = 0x00000000; // Clean CSR registers

	GsPutIMR(0x0000F700); // Unmasks all of the GS interrupts

	SetGsCrt(GS_INTERLACED, GS_MODE_NTSC, GS_FIELD);

	DIntr(); // disable interrupts

	GS_SET_PMODE(	0,		// Read Circuit 1
			1,		// Read Circuit 2
			0,		// Use ALP Register for Alpha Blending
			1,		// Alpha Value of Read Circuit 2 for Output Selection
			0,		// Blend Alpha with output of Read Circuit 2
			0x80);		// Alpha Value = 1.0

	GS_SET_DISPFB1(	0,			// Frame Buffer Base Pointer (Address/2048)
			10,	// Buffer Width (Address/64)
			GS_PSM_CT24,		// Pixel Storage Format
			0,			// Upper Left X in Buffer
			0);

	GS_SET_DISPFB2(	0,			// Frame Buffer Base Pointer (Address/2048)
			10,	// Buffer Width (Address/64)
			GS_PSM_CT24,		// Pixel Storage Format
			0,			// Upper Left X in Buffer
			0);			// Upper Left Y in Buffer

	GS_SET_DISPLAY1(620,		// X position in the display area (in VCK unit
			50,		// Y position in the display area (in Raster u
			4,			// Horizontal Magnification
			1,			// Vertical Magnification
			2559,	// Display area width
			447);		// Display area height

	GS_SET_DISPLAY2(620,		// X position in the display area (in VCK units)
			50,		// Y position in the display area (in Raster units)
			4,			// Horizontal Magnification
			1,			// Vertical Magnification
			2559,	// Display area width
			447);		// Display area height

    EIntr(); //enable interrupts
	delay(10);
	scr_clear();
}

