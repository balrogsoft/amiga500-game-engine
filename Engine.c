/**********************************************
 *					      *
 *		A500GE v 0.1		      *
 *	    Amiga 500 Game Engine             *
 *	     Copyright 2011-2018 	      *
 *	by Pedro Gil Guirado - Balrog Soft    *
 *	      www.amigaskool.net	      *
 *					      *
 **********************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <exec/execbase.h>
#include <exec/memory.h>  

#include <devices/input.h>
#include <devices/timer.h>

#include <graphics/gfxbase.h>

#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/timer.h>   

#include <hardware/cia.h>
#include <hardware/custom.h>
#include <hardware/dmabits.h>
#include <hardware/intbits.h>

#include "ptplayer.h"

#define MOD_SIZE    8340
#define MAP_SIZE    320
#define TILES_SIZE  11648 
#define PLAYER_SIZE 1536 


#define WIDTH   320
#define HEIGHT  256
#define DEPTH   4


#define TILE    16
#define TILE_BITS 4


#define UP     1 
#define DOWN   2
#define LEFT   4
#define RIGHT  8
#define FIRE1 16
#define FIRE2 32

#define LINEBLOCKS 13
#define WBLOCK 16
#define BITMAPLINEBYTES 40

#define POTGOR *(UWORD *)0xDFF016     

struct MsgPort *InputMP;
struct IOStdReq *InputReq;
struct Interrupt InputHandler;

struct CIA *cia = (struct CIA *) 0xBFE001;
struct Custom *custom = (struct Custom *)0xdff000;

extern struct ExecBase *SysBase;
struct GfxBase *GfxBase;
struct Library *CyberGfxBase= NULL;
struct Library *TimerBase;
struct IntuitionBase *IntuitionBase;

struct timeval tt;
struct timeval a, b;

struct timerequest *TimerIO;
struct MsgPort *TimerMP;
struct Message *TimerMsg;


struct Task *myTask = NULL;
BYTE oldPri;

UBYTE NameString[]="InputGameHandler";

static struct Window *old_processwinptr;
static struct Process *thisprocess;

typedef struct {
	UBYTE *mask;
	WORD width;
	WORD height;
	UBYTE depth;
	UBYTE colors;
	BOOL rasterOwn;
	USHORT *colortable;
        UBYTE wbytes;
	struct BitMap *bitmap;
	struct RastPort *rastPort;
} Bitmap;

typedef struct {
	Bitmap* bm;
	Bitmap* rest_bm;
		
	WORD px[2];
	WORD py[2];
	
	WORD x;
	WORD y;
        WORD width;
        WORD height;
    
} Sprite;


static LONG __interrupt __saveds NullInputHandler(void)
{
    return 0;
}

void HardWaitBlitter(void)
{
    while (custom->dmaconr & DMAF_BLTDONE)
    {
    }
}

Bitmap* bm_create(WORD w, WORD h, WORD d, UBYTE* data)
{       
	UBYTE i;
	Bitmap* bm = (Bitmap*) AllocMem(sizeof(Bitmap), MEMF_CLEAR);

	bm->width  = w;
	bm->height = h;
	bm->depth  = d;

        bm->wbytes = w >> 3;
    
	bm->mask = NULL;
	bm->colortable = (UWORD*) AllocMem(sizeof(UWORD)*(2<<d), MEMF_CLEAR);
	bm->rastPort = NULL;
	
	bm->rasterOwn = TRUE;
	bm->bitmap = AllocMem(sizeof(struct BitMap),MEMF_CLEAR);
			
	if (bm->bitmap) {		
            InitBitMap(bm->bitmap, d, w, h);
            
            for (i = 0; i < d; i++) 
                bm->bitmap->Planes[i] = (PLANEPTR) AllocRaster(w,h);
	    
            bm->rastPort = (struct RastPort*) AllocMem(sizeof(struct RastPort),MEMF_CLEAR);
            InitRastPort(bm->rastPort);
            bm->rastPort->BitMap = bm->bitmap;
            SetRast(bm->rastPort, 0);
            if (data != NULL) {
                WaitBlit();
                
                for (i = 0; i < d; i++) 
                    CopyMem(data+((w*h*i)>>3), bm->bitmap->Planes[i], (w*h)>>3);
            }
            return bm;
	}
	else
		return NULL;
}

void bm_dealloc(Bitmap* bm) {
    WaitBlit();
		
    if (bm->bitmap) {
        if (bm->rasterOwn)
        {
            BYTE d;
            for (d = 0; d < bm->depth; d++)
                FreeRaster(bm->bitmap->Planes[d], bm->width, bm->height);
            
            FreeMem(bm->bitmap,sizeof(struct BitMap));
        }
        else
            FreeBitMap(bm->bitmap);
    }
            
    if (bm->mask)
        FreeMem(bm->mask, bm->width*bm->height);
		
    if (bm->colortable)
        FreeMem(bm->colortable, sizeof(UWORD)*(2<<bm->depth));
    
    if (bm->rastPort)
        FreeMem(bm->rastPort, sizeof(struct RastPort));
            
    FreeMem(bm, sizeof(Bitmap));
}

void bm_drawBlock(Bitmap* bm, struct RastPort* rp, WORD x, WORD y, WORD tile)
{
    LONG scr_offset = ( (x >> 3) & 0xFFFE ) + ( y  * BITMAPLINEBYTES );
    LONG map_offset = ( ((tile-1) % LINEBLOCKS) << 1 ) + ( (((tile-1) / LINEBLOCKS) << 4) * 26 );

    HardWaitBlitter();

    custom->bltcon0 = 0xFCA;
    custom->bltcon1 = 0;
    custom->bltafwm = 0xFFFF;
    custom->bltalwm = 0xFFFF;
    custom->bltamod = 24;
    custom->bltbmod = 24;
    custom->bltcmod = BITMAPLINEBYTES - 2;
    custom->bltdmod = BITMAPLINEBYTES - 2;


    custom->bltapt  = bm->mask + map_offset;
    custom->bltbpt  = bm->bitmap->Planes[0] + map_offset;
    custom->bltcpt	= rp->BitMap->Planes[0] + scr_offset;
    custom->bltdpt	= rp->BitMap->Planes[0] + scr_offset;

    custom->bltsize = 1025;//16*64+1;

    HardWaitBlitter();

    custom->bltapt  = bm->mask + map_offset;
    custom->bltbpt  = bm->bitmap->Planes[1] + map_offset;
    custom->bltcpt	= rp->BitMap->Planes[1] + scr_offset;
    custom->bltdpt	= rp->BitMap->Planes[1] + scr_offset;

    custom->bltsize = 1025;//16*64+1;

    HardWaitBlitter();

    custom->bltapt  = bm->mask + map_offset;
    custom->bltbpt  = bm->bitmap->Planes[2] + map_offset;
    custom->bltcpt	= rp->BitMap->Planes[2] + scr_offset;
    custom->bltdpt	= rp->BitMap->Planes[2] + scr_offset;

    custom->bltsize = 1025;//16*64+1;

    HardWaitBlitter();

    custom->bltapt  = bm->mask + map_offset;
    custom->bltbpt  = bm->bitmap->Planes[3] + map_offset;
    custom->bltcpt	= rp->BitMap->Planes[3] + scr_offset;
    custom->bltdpt	= rp->BitMap->Planes[3] + scr_offset;

    custom->bltsize = 1025;//16*64+1;
}

void bm_createMask(Bitmap* bm, UBYTE color) 
{
    WORD x, y, pen, bit = 0, byte, bit_pos;
    
    if (!bm->mask)
        bm->mask = (UBYTE*)AllocMem(bm->width*bm->height, MEMF_CLEAR|MEMF_CHIP);

    for (y = 0; y < bm->height; y++) 
    {
        for (x = 0; x < bm->width; x++)
        {
            pen = ReadPixel(bm->rastPort, x, y);
            if (pen != color)
            {
                byte = bit / 8;
                bit_pos = bit & 7;
                
                bm->mask[byte] ^= 1<<(7-bit_pos);
            }
                            
            bit++;
        }
        bit += 15-((bit-1)&15);
    }
}

Sprite* sp_create(Bitmap* bm, WORD width, WORD height, BOOL mask) 
{
    Sprite* spr = (Sprite*)AllocMem(sizeof(Sprite), 0L);
	
    spr->bm = bm;
    spr->rest_bm = NULL;
	
    if (mask)
        bm_createMask(spr->bm, 0);
    
    spr->px[0] = spr->px[1] = 0;
    spr->py[0] = spr->py[1] = 0;
    
    spr->width = width;
    spr->height = height;
    
    spr->rest_bm = bm_create(spr->width+16, spr->height<<1, bm->depth, NULL);

    return spr;
}

void sp_backupSpriteBack(Sprite* spr, struct RastPort* rp, WORD x, WORD y, UBYTE frame) {
    ULONG scr_offset, map_offset;
    
    spr->px[frame] = x; 
    spr->py[frame] = y;
     
    scr_offset = ( (spr->px[frame] >> 3) & 0xFFFE ) + ( spr->py[frame] * BITMAPLINEBYTES );
    map_offset = ( (frame * spr->height) << 2);
    
    HardWaitBlitter();

    custom->bltcon0 = 0x9F0;
    custom->bltcon1 = 0;
    custom->bltafwm = 0xFFFF;
    custom->bltalwm = 0xFFFF;
    custom->bltamod = BITMAPLINEBYTES - 4;
    custom->bltdmod = 0;;


    custom->bltapt  = rp->BitMap->Planes[0] + scr_offset;
    custom->bltdpt	= spr->rest_bm->bitmap->Planes[0] + map_offset;

    custom->bltsize = 1026;//16*64+2;

    HardWaitBlitter();

    custom->bltapt  = rp->BitMap->Planes[1] + scr_offset;
    custom->bltdpt	= spr->rest_bm->bitmap->Planes[1] + map_offset;

    custom->bltsize = 1026;//16*64+2;

    HardWaitBlitter();

    custom->bltapt  = rp->BitMap->Planes[2] + scr_offset;
    custom->bltdpt	= spr->rest_bm->bitmap->Planes[2] + map_offset;

    custom->bltsize = 1026;//16*64+2;

    HardWaitBlitter();

    custom->bltapt  = rp->BitMap->Planes[3] + scr_offset;
    custom->bltdpt	= spr->rest_bm->bitmap->Planes[3] + map_offset;

    custom->bltsize = 1026;//16*64+2;
    
}
void sp_restoreSpriteBack(Sprite* spr, struct RastPort* rp,  WORD x, WORD y, UBYTE frame)  {
    ULONG scr_offset, map_offset;
    
    scr_offset = ( (spr->px[frame] >> 3) & 0xFFFE ) + ( spr->py[frame] * BITMAPLINEBYTES );
    map_offset = ( (frame * spr->height) << 2);
    
    HardWaitBlitter();

    custom->bltcon0 = 0x9F0;
    custom->bltcon1 = 0;
    custom->bltafwm = 0xFFFF;
    custom->bltalwm = 0xFFFF;
    custom->bltamod = 0;

    custom->bltdmod = BITMAPLINEBYTES - 4;


    custom->bltapt	= spr->rest_bm->bitmap->Planes[0] + map_offset;
    custom->bltdpt	=  rp->BitMap->Planes[0] + scr_offset;

    custom->bltsize = 1026;//16*64+2;

    HardWaitBlitter();

    custom->bltapt	= spr->rest_bm->bitmap->Planes[1] + map_offset;
    custom->bltdpt	=  rp->BitMap->Planes[1] + scr_offset;

    
    custom->bltsize = 1026;//16*64+2;

    HardWaitBlitter();

    custom->bltapt	= spr->rest_bm->bitmap->Planes[2] + map_offset;
    custom->bltdpt	=  rp->BitMap->Planes[2] + scr_offset;

    custom->bltsize = 1026;//16*64+2;

    HardWaitBlitter();

    custom->bltapt	= spr->rest_bm->bitmap->Planes[3] + map_offset;
    custom->bltdpt	=  rp->BitMap->Planes[3] + scr_offset;


    custom->bltsize = 1026;//16*64+2;
}

void sp_drawSprite(Sprite* spr, struct RastPort* rp, WORD sx, WORD sy, UBYTE frame) 
{
    ULONG scr_offset = ( (spr->px[frame] >> 3) & 0xFFFE ) + ( spr->py[frame] * BITMAPLINEBYTES );
    ULONG map_offset = ( (sx >> 3) & 0xFFFE ) + ( sy * spr->bm->wbytes );
    
    HardWaitBlitter();

    custom->bltcon0 = 0xFCA + ((spr->px[frame]&0xf)<<12);
    custom->bltcon1 = ((spr->px[frame]&0xf)<<12);
    custom->bltafwm = 0xFFFF;
    custom->bltalwm = 0x0000;
    custom->bltamod = spr->bm->wbytes - 4;
    custom->bltbmod = spr->bm->wbytes - 4;
    custom->bltcmod = BITMAPLINEBYTES - 4;
    custom->bltdmod = BITMAPLINEBYTES - 4;


    custom->bltapt  = spr->bm->mask + map_offset;
    custom->bltbpt  = spr->bm->bitmap->Planes[0] + map_offset;
    custom->bltcpt	= rp->BitMap->Planes[0] + scr_offset;
    custom->bltdpt	= rp->BitMap->Planes[0] + scr_offset;

    custom->bltsize = 1026;//16*64+2;

    HardWaitBlitter();

    custom->bltapt  = spr->bm->mask + map_offset;
    custom->bltbpt  = spr->bm->bitmap->Planes[1] + map_offset;
    custom->bltcpt	= rp->BitMap->Planes[1] + scr_offset;
    custom->bltdpt	= rp->BitMap->Planes[1] + scr_offset;

    custom->bltsize = 1026;//16*64+2;

    HardWaitBlitter();

    custom->bltapt  = spr->bm->mask + map_offset;
    custom->bltbpt  = spr->bm->bitmap->Planes[2] + map_offset;
    custom->bltcpt	= rp->BitMap->Planes[2] + scr_offset;
    custom->bltdpt	= rp->BitMap->Planes[2] + scr_offset;

    custom->bltsize = 1026;//16*64+2;

    HardWaitBlitter();

    custom->bltapt  = spr->bm->mask + map_offset;
    custom->bltbpt  = spr->bm->bitmap->Planes[3] + map_offset;
    custom->bltcpt	= rp->BitMap->Planes[3] + scr_offset;
    custom->bltdpt	= rp->BitMap->Planes[3] + scr_offset;

    custom->bltsize = 1026;//16*64+2;
}

 
void sp_dealloc(Sprite* spr) 
{
    if (spr)
    {
        if (spr->rest_bm)
            bm_dealloc(spr->rest_bm);

        FreeMem(spr,sizeof(Sprite));
    }
}


UBYTE joy_read(UWORD joynum)
{
    UBYTE ret = 0;
    UWORD joy;

    if(joynum == 0) 
        joy = custom->joy0dat;
    else
        joy = custom->joy1dat;

    ret += (joy >> 1 ^ joy) & 0x0100 ? UP : 0;  
    ret += (joy >> 1 ^ joy) & 0x0001 ? DOWN : 0;

    ret += joy & 0x0200 ? LEFT : 0;
    ret += joy & 0x0002 ? RIGHT : 0;

    if(joynum == 0) {
        ret += !(cia->ciapra & 0x0040) ? FIRE1 : 0; 
        ret += !(POTGOR & 0x0400) ? FIRE2 : 0;
    }
    else {
        ret += !(cia->ciapra & 0x0080) ? FIRE1 : 0;
        ret += !(POTGOR & 0x4000) ? FIRE2 : 0;
    }

    return(ret);
}

void startTimer(void) {
    TimerMP = CreatePort(0,0);
    TimerIO = (struct timerequest *) AllocMem(sizeof(struct timerequest),MEMF_PUBLIC|MEMF_CLEAR); 
    TimerIO->tr_node.io_Message.mn_Node.ln_Type = NT_MESSAGE;
    TimerIO->tr_node.io_Message.mn_Length = sizeof(struct Message);
    TimerIO->tr_node.io_Message.mn_ReplyPort = TimerMP;
    OpenDevice(TIMERNAME,UNIT_VBLANK,(struct IORequest *)TimerIO, MEMF_CLEAR);
    TimerBase = TimerIO->tr_node.io_Device;
}

    
ULONG timer(void) {
    TimerIO->tr_node.io_Command = TR_GETSYSTIME;
    DoIO((struct IORequest *) TimerIO);
    a=TimerIO->tr_time;
    b=a;
    SubTime(&b, &tt); 
    tt = a;
    return b.tv_micro/1000;
}

void closeTimer(void) {
    CloseDevice((struct IORequest *) TimerIO);
    FreeMem(TimerIO, sizeof(struct timerequest));
    DeletePort(TimerMP);
}

int main(void)
{
    struct View view1, view2, *oldview; 
    struct ViewPort viewPort;

    struct BitMap bitMap1;
    struct BitMap bitMap2;
    struct RastPort rastPort1;
    struct RastPort rastPort2;
    struct RastPort *rastPort;

    struct RasInfo rasInfo;

    
    Bitmap* tiles_bm;
    Bitmap* player_bm;
    Sprite* player_spr;
    Sprite* obj_spr;
    
    BPTR file_ptr;
    WORD fps=0;
    WORD fps_val=0;
    ULONG mtimer = 0, d;
    LONG frame;
    UBYTE numstr[16];
    WORD x, y, i, j, l;
    WORD px = 32, py = 32, ox = 32, oy = 64;
    UBYTE tx1 = 0, ty1 = 0, tx2, ty2, tile1, tile2, otx1 = 0, oty1 = 0, otx2, oty2,otile1, otile2;
    UBYTE walk_tiles[15] = {9,10,37,38,50,51,63,64,76,77,90,42,46,88,0};
    UBYTE paint_tiles[5] = {9, 10, 42, 46};
    UWORD repaint_tilesx[10], repaint_tilesy[10], repaint_tiles[10];
    WORD offsetx_tiles[92], offsety_tiles[92];
    WORD mapy[16] = {0, 20, 40, 60, 80, 100, 120, 140, 160, 180, 200, 220, 240, 260, 280, 300};
    
    UBYTE spr_frames[4] = {0, 1, 0, 2};            
    UBYTE spr_anim[4] = {0, 16, 32, 48};
    UBYTE spr_dir = 2;
    
    BYTE dx = 0, dy = 0;
    UBYTE* tiles = (UBYTE*)AllocMem(TILES_SIZE, MEMF_CHIP);
    UBYTE* player = (UBYTE*)AllocMem(PLAYER_SIZE, MEMF_CHIP);
    UBYTE* mod = (UBYTE*)AllocMem(MOD_SIZE, MEMF_CHIP);
    UBYTE* map = (UBYTE*)AllocMem(MAP_SIZE, 0L);
    UWORD* pal = (UWORD*)AllocMem(32, 0L);
    UBYTE joyData;
    BOOL move = FALSE;
    
    UWORD oldDMA;
    
    // Open libraries need for the game engine
    
    IntuitionBase = (struct IntuitionBase *) OpenLibrary("intuition.library", 0L);
    GfxBase = (struct GfxBase *) OpenLibrary( "graphics.library", 0L);
          
    // Save pointers for view, task and windowptr
    oldview = GfxBase->ActiView;
    myTask = FindTask(NULL);
    
    old_processwinptr = thisprocess->pr_WindowPtr;
    thisprocess->pr_WindowPtr = (APTR)-1;
    thisprocess = (struct Process *) myTask;
   
    
    // Create a NULL input handler to prevent other process capturing the keyboard events 
    if ((InputMP = CreatePort("inputgamehandler",127))) {
        if ((InputReq = AllocMem(sizeof(struct IORequest),MEMF_PUBLIC|MEMF_CLEAR))) {
            InputReq->io_Message.mn_Node.ln_Type = NT_MESSAGE;
            InputReq->io_Message.mn_Length = sizeof(struct Message);
            InputReq->io_Message.mn_ReplyPort = InputMP;
            if (OpenDevice("input.device",0,(struct IORequest *)InputReq,0) == 0)
            {
                InputHandler.is_Node.ln_Type = NT_INTERRUPT;
                InputHandler.is_Node.ln_Pri = 127;
                InputHandler.is_Data = 0;
                InputHandler.is_Code = (APTR)NullInputHandler;
                InputHandler.is_Node.ln_Name=NameString;
        
                InputReq->io_Command = IND_ADDHANDLER;
                InputReq->io_Data = &InputHandler;
                
                DoIO((struct IORequest *)InputReq);
            }
        }
    }
    
    // Load game assets
    if (file_ptr = Open("data/intro1.mod", MODE_OLDFILE)) 
    {
            Read(file_ptr, mod, MOD_SIZE);
            Close(file_ptr);
    }
        
    
    if (file_ptr = Open("data/map.dat", MODE_OLDFILE)) 
    {
            Read(file_ptr, map, MAP_SIZE);
            Close(file_ptr);
    }
    
    if (file_ptr = Open("data/pal.dat", MODE_OLDFILE)) 
    {
            Read(file_ptr, pal, 32);
            Close(file_ptr);
    }
    
    if (file_ptr = Open("data/tiles2.dat", MODE_OLDFILE)) 
    {
            Read(file_ptr, tiles, TILES_SIZE);
            Close(file_ptr);
    }
    
    if (file_ptr = Open("data/player.dat", MODE_OLDFILE)) 
    {
            Read(file_ptr, player, PLAYER_SIZE);
            Close(file_ptr);
    }
    
    // Remove current view
    LoadView(NULL);
    WaitTOF();
    WaitTOF();
    
    // Initialize two view that will act as double buffer
    InitView(&view1); 
    InitView(&view2); 

    // Initialize bitmap views 
    InitBitMap(&bitMap1, DEPTH, WIDTH, HEIGHT);
    for (d=0; d<DEPTH; d++)
        bitMap1.Planes[d] = (PLANEPTR)AllocRaster(WIDTH, HEIGHT);

    InitBitMap(&bitMap2, DEPTH, WIDTH, HEIGHT);
    for (d=0; d<DEPTH; d++)
        bitMap2.Planes[d] = (PLANEPTR)AllocRaster(WIDTH, HEIGHT);
    
    // And the rastport associated to each bitmap
    InitRastPort(&rastPort1);
    rastPort1.BitMap = &bitMap1;
    SetRast(&rastPort1, 0);
    
    InitRastPort(&rastPort2);
    rastPort2.BitMap = &bitMap2;
    SetRast(&rastPort2, 0);
    
    rasInfo.BitMap = &bitMap1;
    rasInfo.RxOffset = 0;
    rasInfo.RyOffset = 0;
    rasInfo.Next = NULL;

    // Initialize the viewport for our two views
    // and create the copper list for each view
    InitVPort(&viewPort);
    view1.ViewPort = &viewPort;
    viewPort.RasInfo = &rasInfo;
    viewPort.DWidth = WIDTH;
    viewPort.DHeight = HEIGHT;
    
    MakeVPort(&view1, &viewPort);
    MrgCop(&view1);
    LoadRGB4(&viewPort, pal, 16);
    
    
    rasInfo.BitMap = &bitMap2;
    view2.ViewPort = &viewPort;
    
    MakeVPort(&view2, &viewPort);
    MrgCop(&view2);
    LoadRGB4(&viewPort, pal, 16);

    // Create bitmap and sprite objects
    tiles_bm = bm_create(208, 112, 4, tiles);
    bm_createMask(tiles_bm, 0);
    
    player_bm = bm_create(48, 64, 4, player);
    player_spr = sp_create(player_bm, TILE, TILE, TRUE);
    
    obj_spr = sp_create(tiles_bm, TILE, TILE, FALSE);
        
    // Set task priority higher, forbid and disable multitasking and interrupts
    oldPri = SetTaskPri(myTask, 127);	
    oldDMA = custom->dmacon;

    Forbid();
    Disable();
    
    // Setup dma bits
    custom->dmacon = DMAF_SETCLR | DMAF_MASTER | DMAF_RASTER | DMAF_BLITTER | DMAF_BLITHOG;

    // First drawing of the tilemap on each buffer
    rastPort = &rastPort1;
    LoadView(&view2);
    
    frame = 0;
    
    for (j = 0; j < 2; j++) {
        WORD i = 0;         
        for (y = 0; y < HEIGHT-1; y += TILE) 
        {
                for (x = 0; x < WIDTH-1; x+= TILE) 
                {
                    UBYTE tile = map[i];
                    bm_drawBlock(tiles_bm, rastPort, x, y, 33);
                    if (tile>0)
                        bm_drawBlock(tiles_bm, rastPort, x, y, tile);

                    i++;  
                }
        }
        sp_backupSpriteBack(obj_spr, rastPort, ox, oy, j);
        sp_backupSpriteBack(player_spr, rastPort, px, py, j);
        rastPort = &rastPort2;
    }
        
    for (j = 0; j < 91; j++) {
        offsetx_tiles[j+1] = (j<<TILE_BITS)%208;
        offsety_tiles[j+1] = ((j<<TILE_BITS)/208)<<TILE_BITS;
    }
    
    // Initialize ptplayer code and play the mod
    mt_install_cia(custom, 0, 1);
    mt_init(custom, mod, NULL, 0);
    mt_musicmask(custom, 0xf);
    mt_Enable = 1;
    i = 0; 
    
    // Initialize the timer
    startTimer();
    while((*(UBYTE *)0xBFE001) & 0x40)
    {  
        // Handle game logic, moving player, objects, collision, repaint tiles...
        if (move == FALSE) {
            if (dx!=0 || dy!=0) {

                otx1 = (ox>>4);
                oty1 = (oy>>4);
                
                otile1 = map[(otx1%20)+(mapy[oty1])];
                
                otx2 = otx1 + dx;
                oty2 = oty1 + dy;
                otile2 = map[(otx2%20)+(mapy[oty2])];
                
                tx2 = (px>>4);
                ty2 = (py>>4);
                tile2 = map[(tx2%20)+(mapy[ty2])];

                tx1 = tx2 + dx;
                ty1 = ty2 + dy;
                tile1 = map[(tx1%20)+(mapy[ty1])];
                i = 0;

                for (j = 0; j < 4; j++) {
                    if (tile1 == paint_tiles[j]) {
                        repaint_tiles[i] = tile1;
                        repaint_tilesx[i] = tx1<<TILE_BITS;
                        repaint_tilesy[i] = ty1<<TILE_BITS;
                        i++;
                        
                        for (l = 0; l < i-1; l++) {
                            if (repaint_tilesx[i-1] == repaint_tilesx[l] &&
                                repaint_tilesy[i-1] == repaint_tilesy[l]) {
                                i --;
                                break;
                            }
                        }
                    }
                    if (tile2 == paint_tiles[j]) {
                        repaint_tiles[i] = tile2;
                        repaint_tilesx[i] = tx2<<TILE_BITS;
                        repaint_tilesy[i] = ty2<<TILE_BITS;
                        i++;
                        
                        for (l = 0; l < i-1; l++) {
                            if (repaint_tilesx[i-1] == repaint_tilesx[l] &&
                                repaint_tilesy[i-1] == repaint_tilesy[l]) {
                                i --;
                                break;
                            }
                        }
                    }


                    if (otile1 == paint_tiles[j] && (tx1!=otx1 || ty1!=oty1)) {
                        repaint_tiles[i] = otile1;
                        repaint_tilesx[i] = otx1<<TILE_BITS;
                        repaint_tilesy[i] = oty1<<TILE_BITS;
                        i++;
                        
                        for (l = 0; l < i-1; l++) {
                            if (repaint_tilesx[i-1] == repaint_tilesx[l] &&
                                repaint_tilesy[i-1] == repaint_tilesy[l]) {
                                i --;
                                break;
                            }
                        }
                    }
                    if (otile2 == paint_tiles[j] && (otx1!=otx2 || oty1!=oty2)) {
                        repaint_tiles[i] = otile2;
                        repaint_tilesx[i] = otx2<<TILE_BITS;
                        repaint_tilesy[i] = oty2<<TILE_BITS;
                        i++;
                        
                        for (l = 0; l < i-1; l++) {
                            if (repaint_tilesx[i-1] == repaint_tilesx[l] &&
                                repaint_tilesy[i-1] == repaint_tilesy[l]) {
                                i --;
                                break;
                            }
                        }
                    }

                }
                for (j = 0; j < 15; j++) {
                    if (walk_tiles[j] == tile1) {
                        move = TRUE;
                    }
                }
                if (move == TRUE && tx1 == otx1 && ty1 == oty1) {
                    move = FALSE;
                    for (j = 0; j < 15; j++) {
                        if (walk_tiles[j] == otile2) {
                            move = TRUE;
                        }
                    }
                }
                if (move == FALSE) {
                    dx = 0;
                    dy = 0;
                }
            }
        }
            
        if (move == TRUE) {
            px+=dx;
            py+=dy;
            if (tx1 == otx1 && ty1 == oty1) {
                ox+=dx;
                oy+=dy;
                
            }
            if ((px&15) == 0 && (py&15) == 0) {
                move = FALSE;
                dx = 0;
                dy = 0;

            }
        }
        
        // Setup rastPort to point buffer rastport
        if (frame==0) {
            rastPort = &rastPort1;
        }
        else {
            rastPort = &rastPort2;
        }
    
        // Read joystick
        joyData = joy_read(1);

        if (move == FALSE) {
            if(joyData & DOWN) {
                dy=1;
                dx=0;
                spr_dir = 2;
            } 
            else if(joyData & LEFT) {
                dx=-1;
                dy=0;
                spr_dir = 3;
            }
            else if(joyData & RIGHT) {
                dx=1;
                dy=0;
                spr_dir = 1;
            } 
            else if(joyData & UP) {
                dy=-1;
                dx=0;
                spr_dir = 0;
            }
        }
        
        // Wait to end of frame to draw game content
        WaitTOF();
        
        // Restore sprite and object backgrounds
        sp_restoreSpriteBack(obj_spr, rastPort, ox, oy, frame);
        sp_restoreSpriteBack(player_spr, rastPort,  px, py, frame);

        // Save a copy of the background where the sprites will be drawed
        sp_backupSpriteBack(obj_spr, rastPort, ox, oy, frame);
        sp_backupSpriteBack(player_spr, rastPort, px, py, frame);

        // Draw the sprites
        sp_drawSprite(obj_spr, rastPort,  144, 96, frame);
        sp_drawSprite(player_spr, rastPort, (dx!=0||dy!=0?spr_frames[(fps>>2)%4]:0)<<TILE_BITS, spr_anim[spr_dir], frame);

        // Draw tiles which need to be repainted
        for (j = 0; j < i; j++) {
            bm_drawBlock(tiles_bm, rastPort, repaint_tilesx[j], repaint_tilesy[j], repaint_tiles[j]);
        }
        
        // Calculate FPS
        mtimer+=timer();
        if (mtimer>=1000)
        {
            fps_val = fps;
            fps = 0;
            mtimer = mtimer - 1000;
        }
        
        sprintf(numstr, "%d fps" , fps_val);
                                
        SetAPen(rastPort, 31);
        Move(rastPort, 10, 10);
        Text(rastPort, numstr, strlen(numstr));
        
        // Show the current view of double buffer
        LoadView(frame==0?&view1:&view2);
        
        frame ^= 1;
        fps++;
    }; 
    
    WaitTOF();
    
    // Close the timer
    closeTimer();
    
    // Close ptplayer code
    mt_end(custom);
    mt_remove_cia(custom);
    
    // Set original dma bits
    custom->dmacon = oldDMA | DMAF_SETCLR | DMAF_MASTER;

    // Enable and permit multitasking and interupts
    Enable();
    Permit();
   
    thisprocess->pr_WindowPtr = old_processwinptr;

    // Load original view
    LoadView(oldview);
    WaitTOF();
    
    // Free copper lists created by the viewport
    FreeCprList(view1.LOFCprList);
    if(view1.SHFCprList)
        FreeCprList(view1.SHFCprList);
    FreeCprList(view2.LOFCprList);
    if(view2.SHFCprList)
        FreeCprList(view2.SHFCprList);
    FreeVPortCopLists(&viewPort); 

    // Free the screen bitmap planes
    for(d=0; d<DEPTH; d++)
    {
        if (bitMap1.Planes[d])
            FreeRaster(bitMap1.Planes[d], WIDTH, HEIGHT);
        if (bitMap2.Planes[d])
            FreeRaster(bitMap2.Planes[d], WIDTH, HEIGHT);
    }
    
    // Free sprites and bitmaps objects
    sp_dealloc(player_spr);
    sp_dealloc(obj_spr);
    
    bm_dealloc(player_bm);
    bm_dealloc(tiles_bm);
        
    // Free memory for the game assets
    FreeMem(mod, MOD_SIZE);
    FreeMem(tiles, TILES_SIZE);
    FreeMem(player, PLAYER_SIZE);
    
    FreeMem(map, MAP_SIZE);
    FreeMem(pal, 32);

    // Remove null input handler
    if (InputMP && InputReq)
    {
        InputReq->io_Data = &InputHandler;
        InputReq->io_Command = IND_REMHANDLER;
        DoIO((struct IORequest *)InputReq);
        CloseDevice((struct IORequest *)InputReq);
    }

    if (InputReq) FreeMem(InputReq,sizeof(struct IORequest));
    if (InputMP) DeletePort(InputMP);

    // Close libraries
    if(GfxBase)
        CloseLibrary((struct Library *)GfxBase);
    
    if(IntuitionBase)
        CloseLibrary((struct Library *)IntuitionBase);
        
    return 0;
}
