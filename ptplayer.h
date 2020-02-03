#ifndef PTPLAYER_H
#define PTPLAYER_H

/**************************************************
 *    ----- Protracker V2.3B Playroutine -----    *
 **************************************************/

/*
  Version 5.1
  Written by Frank Wille in 2013, 2016, 2017.
*/

#ifndef EXEC_TYPES_H
#include <exec/types.h>
#endif

/*
  _mt_install_cia(a6=CUSTOM, a0=AutoVecBase, d0=PALflag.b)
    Install a CIA-B interrupt for calling _mt_music or mt_sfxonly.
    The music module is replayed via _mt_music when _mt_Enable is non-zero.
    Otherwise the interrupt handler calls mt_sfxonly to play sound
    effects only.__
*/
void __asm mt_install_cia(register __a6 APTR custom,
	register __a0 APTR *AutoVecBase, register __d0 UBYTE PALflag);

/*
  _mt_remove_cia(a6=CUSTOM)
    Remove the CIA-B music interrupt and restore the old vector.m
*/

void __asm mt_remove_cia(register __a6 APTR custom);

/*
  _mt_init(a6=CUSTOM, a0=TrackerModule, a1=Samples|NULL, d0=InitialSongPos.b)
    Initialize a new module.
    Reset speed to 6, tempo to 125 and start at the given position.
    Master volume is at 64 (maximum).
    When a1 is NULL the samples are assumed to be stored after the patterns.
*/

void __asm mt_init(register __a6 APTR custom,
	register __a0 APTR TrackerModule, register __a1 APTR Samples,
	register __d0 UBYTE InitialSongPos);

/*
  _mt_end(a6=CUSTOM)
    Stop playing current module.
*/

void __asm mt_end(register __a6 APTR custom);

/*
  _mt_soundfx(a6=CUSTOM, a0=SamplePointer,
             d0=SampleLength.w, d1=SamplePeriod.w, d2=SampleVolume.w)
    Request playing of an external sound effect on the most unused channel.
    This function is for compatibility with the old API only!
    You should call _mt_playfx instead.
*/

void __asm mt_soundfx(register __a6 APTR custom,
	register __a0 APTR SamplePointer, register __d0 UWORD SampleLength,
	register __d1 UWORD SamplePeriod, register __d2 UWORD SampleVolume);

/*
  _mt_playfx(a6=CUSTOM, a0=SfxStructurePointer)
    Request playing of a prioritized external sound effect, either on a
    fixed channel or on the most unused one.
    Structure layout of SfxStructure:
      APTR sfx_ptr (pointer to sample start in Chip RAM, even address)
      WORD sfx_len (sample length in words)
      WORD sfx_per (hardware replay period for sample)
      WORD sfx_vol (volume 0..64, is unaffected by the song's master volume)
      BYTE sfx_cha (0..3 selected replay channel, -1 selects best channel)
      UBYTE sfx_pri (unsigned priority, must be non-zero)
    When multiple samples are assigned to the same channel the lower
    priority sample will be replaced. When priorities are the same, then
    the older sample is replaced.
    The chosen channel is blocked for music until the effect has
    completely been replayed.
*/

typedef struct SfxStructure
{
	APTR sfx_ptr;  /* pointer to sample start in Chip RAM, even address */
	UWORD sfx_len; /* sample length in words */
	UWORD sfx_per; /* hardware replay period for sample */
	UWORD sfx_vol; /* volume 0..64, is unaffected by the song's master volume */
	BYTE sfx_cha;  /* 0..3 selected replay channel, -1 selects best channel */
	UBYTE sfx_pri; /* unsigned priority, must be non-zero */
} SfxStructure;

void __asm mt_playfx(register __a6 APTR custom,
	register __a0 SfxStructure *SfxStructurePointer);

/*
  _mt_musicmask(a6=CUSTOM, d0=ChannelMask.b)
    Set bits in the mask define which specific channels are reserved
    for music only. Set bit 0 for channel 0, ..., bit 3 for channel 3.
    When calling _mt_soundfx or _mt_playfx with automatic channel selection
    (sfx_cha=-1) then these masked channels will never be picked.
    The mask defaults to 0.
*/

void __asm mt_musicmask(register __a6 APTR custom,
	register __d0 UBYTE ChannelMask);

/*
  _mt_mastervol(a6=CUSTOM, d0=MasterVolume.w)
    Set a master volume from 0 to 64 for all music channels.
    Note that the master volume does not affect the volume of external
    sound effects (which is desired).
*/

void __asm mt_mastervol(register __a6 APTR custom,
	register __d0 UWORD MasterVolume);

/*
  _mt_music(a6=CUSTOM)
    The replayer routine. Is called automatically after mt_install_cia().
*/

void __asm mt_music(register __a6 APTR custom);

/*
  _mt_Enable
    Set this byte to non-zero to play music, zero to pause playing.
    Note that you can still play sound effects, while music is stopped.
*/

extern UBYTE mt_Enable;

/*
  _mt_E8Trigger
    This byte reflects the value of the last E8 command.
    It is reset to 0 after mt_init().
*/

extern UBYTE mt_E8Trigger;

/*
  _mt_MusicChannels
    This byte defines the number of channels which should be dedicated
    for playing music. So sound effects will never use more
    than 4 - _mt_MusicChannels channels at once. Defaults to 0.
*/

extern UBYTE mt_MusicChannels;

#endif
