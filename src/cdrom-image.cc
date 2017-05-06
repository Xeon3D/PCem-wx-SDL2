#include <stdlib.h>
#include "cdrom-iso.h"
#include "dosbox/cdrom.h"

extern "C"
{
#include "ibm.h"
#include "ide.h"
}

char iso_path[1024];
static int iso_changed = 0;

extern ATAPI iso_atapi;

CDROM_Interface_Image* cdrom = NULL;

#define MSFtoLBA(m,s,f)  (((((m*60)+s)*75)+f)-150)

static uint32_t cdrom_capacity = 0;

enum
{
    CD_STOPPED = 0,
    CD_PLAYING,
    CD_PAUSED
};

static int iso_cd_state = CD_STOPPED;
static uint32_t iso_cd_pos = 0, iso_cd_end = 0;

#define BUF_SIZE 32768
static int16_t cd_buffer[BUF_SIZE];
static int cd_buflen = 0;

void iso_audio_callback(int16_t *output, int len)
{
        if (iso_cd_state != CD_PLAYING)
        {
                memset(output, 0, len * 2);
                return;
        }
        while (cd_buflen < len)
        {
                if (iso_cd_pos < iso_cd_end)
                {
//                      pclog("Read to %i\n", cd_buflen);
                        if (!cdrom->ReadSector((unsigned char*)&cd_buffer[cd_buflen], true, iso_cd_pos - 150))
                        {
//                                pclog("DeviceIoControl returned false\n");
                                memset(&cd_buffer[cd_buflen], 0, (BUF_SIZE - cd_buflen) * 2);
                                iso_cd_state = CD_STOPPED;
                                cd_buflen = len;
                        }
                        else
                        {
//                                pclog("DeviceIoControl returned true\n");
                                iso_cd_pos++;
                                cd_buflen += (RAW_SECTOR_SIZE / 2);
                        }
                }
                else
                {
                        memset(&cd_buffer[cd_buflen], 0, (BUF_SIZE - cd_buflen) * 2);
                        iso_cd_state = CD_STOPPED;
                        cd_buflen = len;
                }
        }
        memcpy(output, cd_buffer, len * 2);
        memmove(cd_buffer, &cd_buffer[len], (BUF_SIZE - len) * 2);
        cd_buflen -= len;
}

void iso_audio_stop()
{
        iso_cd_state = CD_STOPPED;
}

static int iso_is_track_audio(uint32_t pos, int ismsf)
{
        if (!cdrom) return 0;
        if (ismsf)
        {
                int m = (pos >> 16) & 0xff;
                int s = (pos >> 8) & 0xff;
                int f = pos & 0xff;
                pos = MSF_TO_FRAMES(m, s, f);
        }
        
        unsigned char attr;
        TMSF tmsf;
        int number;
        cdrom->GetAudioTrackInfo(cdrom->GetTrack(pos), number, tmsf, attr);

        return attr == AUDIO_TRACK;
}

static void iso_playaudio(uint32_t pos, uint32_t len, int ismsf)
{
        if (!cdrom) return;
        int number;
        unsigned char attr;
        TMSF tmsf;
        cdrom->GetAudioTrackInfo(cdrom->GetTrack(pos), number, tmsf, attr);
        if (attr == DATA_TRACK)
        {
                pclog("Can't play data track\n");
                iso_cd_pos = 0;
                iso_cd_state = CD_STOPPED;
                return;
        }
        pclog("Play audio - %08X %08X %i\n", pos, len, ismsf);
        if (ismsf)
        {
                pos = (pos & 0xff) + (((pos >> 8) & 0xff) * 75) + (((pos >> 16) & 0xff) * 75 * 60);
                len = (len & 0xff) + (((len >> 8) & 0xff) * 75) + (((len >> 16) & 0xff) * 75 * 60);
                pclog("MSF - pos = %08X len = %08X\n", pos, len);
        }
        else
                len += pos;
        iso_cd_pos   = pos;// + 150;
        iso_cd_end   = len;// + 150;
        iso_cd_state = CD_PLAYING;
        if (iso_cd_pos < 150)
                iso_cd_pos = 150;
        pclog("Audio start %08X %08X %i %i %i\n", iso_cd_pos, iso_cd_end, iso_cd_state, cd_buflen, len);
}

static void iso_pause(void)
{
        if (!cdrom) return;
        if (iso_cd_state == CD_PLAYING)
                iso_cd_state = CD_PAUSED;
}

static void iso_resume(void)
{
        if (!cdrom) return;
        if (iso_cd_state == CD_PAUSED)
                iso_cd_state = CD_PLAYING;
}

static void iso_stop(void)
{
        if (!cdrom) return;
        iso_cd_state = CD_STOPPED;
}

static void iso_seek(uint32_t pos)
{
        if (!cdrom) return;
        iso_cd_pos   = pos;
        iso_cd_state = CD_STOPPED;
}

static int iso_ready(void)
{
        if (!cdrom)
                return 0;

        if (strlen(iso_path) == 0)
                return 0;

        if (old_cdrom_drive != cdrom_drive)
                return 1;

        return 1;
}

static int iso_get_last_block(unsigned char starttrack, int msf, int maxlen, int single)
{
        long size;
        int c;
        int lb=0;

        if (!cdrom) return 0;

        int first_track;
        int last_track;
        int number;
        unsigned char attr;
        TMSF tmsf;
        cdrom->GetAudioTracks(first_track, last_track, tmsf);

        for (c = 0; c <= last_track; c++)
        {
                uint32_t address;
                cdrom->GetAudioTrackInfo(c+1, number, tmsf, attr);
                address = MSFtoLBA(tmsf.min, tmsf.sec, tmsf.fr);
                if (address > lb)
                        lb = address;
        }
        return lb;
}

static int iso_medium_changed(void)
{
        int changed = iso_changed;
        iso_changed = 0;
        return changed;
}

static uint8_t iso_getcurrentsubchannel(uint8_t *b, int msf)
{
        if (!cdrom) return 0;
        uint8_t ret;
        int pos=0;

        uint32_t cdpos = iso_cd_pos;
        if (cdpos >= 150) cdpos -= 150;
        TMSF relPos, absPos;
        unsigned char attr, track, index;
        cdrom->GetAudioSub(cdpos, attr, track, index, relPos, absPos);

        if (iso_cd_state == CD_PLAYING)
                ret = 0x11;
        else if (iso_cd_state == CD_PAUSED)
                ret = 0x12;
        else
                ret = 0x13;

        b[pos++] = attr;
        b[pos++] = track;
        b[pos++] = index;

        if (msf)
        {
                uint32_t dat = MSFtoLBA(absPos.min, absPos.sec, absPos.fr);
                b[pos + 3] = (uint8_t)(dat % 75); dat /= 75;
                b[pos + 2] = (uint8_t)(dat % 60); dat /= 60;
                b[pos + 1] = (uint8_t)dat;
                b[pos]     = 0;
                pos += 4;
                dat = MSFtoLBA(relPos.min, relPos.sec, relPos.fr);
                b[pos + 3] = (uint8_t)(dat % 75); dat /= 75;
                b[pos + 2] = (uint8_t)(dat % 60); dat /= 60;
                b[pos + 1] = (uint8_t)dat;
                b[pos]     = 0;
                pos += 4;
        }
        else
        {
                uint32_t dat = MSFtoLBA(absPos.min, absPos.sec, absPos.fr);
                b[pos++] = (dat >> 24) & 0xff;
                b[pos++] = (dat >> 16) & 0xff;
                b[pos++] = (dat >> 8) & 0xff;
                b[pos++] = dat & 0xff;
                dat = MSFtoLBA(relPos.min, relPos.sec, relPos.fr);
                b[pos++] = (dat >> 24) & 0xff;
                b[pos++] = (dat >> 16) & 0xff;
                b[pos++] = (dat >> 8) & 0xff;
                b[pos++] = dat & 0xff;
        }

        return ret;
}

static void iso_eject(void)
{
        /*
        iso_cd_state = CD_STOPPED;
        if (cdrom)
        {
                delete cdrom;
                cdrom = NULL;
        }
        */
}

static void iso_load(void)
{
        /*
        if (!cdrom && iso_path && strlen(iso_path) > 0)
                iso_open(iso_path);
        */
}

static int iso_readsector(uint8_t *b, int sector)
{
        if (!cdrom) return -1;
        return !cdrom->ReadSector(b, false, sector);
}

static void iso_readsector_raw(uint8_t *b, int sector)
{
        if (!cdrom) return;
        cdrom->ReadSector(b, true, sector);
}

static int iso_readtoc(unsigned char *b, unsigned char starttrack, int msf, int maxlen, int single)
{
        if (!cdrom) return 0;
        int len=4;
        int c,d;
        uint32_t temp;

        int first_track;
        int last_track;
        int number;
        unsigned char attr;
        TMSF tmsf;
        cdrom->GetAudioTracks(first_track, last_track, tmsf);

        b[2] = first_track;
        b[3] = last_track;

        d = 0;
        for (c = 0; c <= last_track; c++)
        {
                cdrom->GetAudioTrackInfo(c+1, number, tmsf, attr);
                if (number >= starttrack)
                {
                        d=c;
                        break;
                }
        }
        cdrom->GetAudioTrackInfo(c+1, number, tmsf, attr);
        b[2] = number;

        for (c = d; c <= last_track; c++)
        {
                if ((len + 8) > maxlen)
                        break;
                cdrom->GetAudioTrackInfo(c+1, number, tmsf, attr);

//                pclog("Len %i max %i Track %02X - %02X %02X %02i:%02i:%02i %08X\n",len,maxlen,toc[c].cdte_track,toc[c].cdte_adr,toc[c].cdte_ctrl,toc[c].cdte_addr.msf.minute, toc[c].cdte_addr.msf.second, toc[c].cdte_addr.msf.frame,MSFtoLBA(toc[c].cdte_addr.msf.minute, toc[c].cdte_addr.msf.second, toc[c].cdte_addr.msf.frame));
                b[len++] = 0; /* reserved */
                b[len++] = attr;
                b[len++] = number; /* track number */
                b[len++] = 0; /* reserved */

                if (msf)
                {
                        b[len++] = 0;
                        b[len++] = tmsf.min;
                        b[len++] = tmsf.sec;
                        b[len++] = tmsf.fr;
                }
                else
                {
                        temp = MSFtoLBA(tmsf.min, tmsf.sec, tmsf.fr);
                        b[len++] = temp >> 24;
                        b[len++] = temp >> 16;
                        b[len++] = temp >> 8;
                        b[len++] = temp;
                }
                if (single)
                        break;
        }
        b[0] = (uint8_t)(((len-2) >> 8) & 0xff);
        b[1] = (uint8_t)((len-2) & 0xff);
        /*
        pclog("Table of Contents:\n");
        pclog("First track - %02X\n", first_track);
        pclog("Last  track - %02X\n", last_track);
        for (c = 0; c <= last_track; c++)
        {
                cdrom->GetAudioTrackInfo(c+1, number, tmsf, attr);
                pclog("Track %02X - number %02X control %02X adr %02X address %02X %02X %02X %02X\n", c, number, attr, 0, 0, tmsf.min, tmsf.sec, tmsf.fr);
        }
        for (c = 0;c <= last_track; c++) {
                cdrom->GetAudioTrackInfo(c+1, number, tmsf, attr);
            pclog("Track %02X - number %02X control %02X adr %02X address %06X\n", c, number, attr, 0, MSF_TO_FRAMES(tmsf.min, tmsf.sec, tmsf.fr));
        }
        */
        return len;
}

static int iso_readtoc_session(unsigned char *b, int msf, int maxlen)
{
        if (!cdrom) return 0;
        int len = 4;

        int number;
        TMSF tmsf;
        unsigned char attr;
        cdrom->GetAudioTrackInfo(1, number, tmsf, attr);

//        pclog("Read TOC session - %i %02X %02X %i %i %02X %02X %02X\n",0, 0, 0,1,1,attr,0,number);

        b[2] = 1;
        b[3] = 1;
        b[len++] = 0; /* reserved */
        b[len++] = attr;
        b[len++] = number; /* track number */
        b[len++] = 0; /* reserved */
        if (msf)
        {
                b[len++] = 0;
                b[len++] = tmsf.min;
                b[len++] = tmsf.sec;
                b[len++] = tmsf.fr;
        }
        else
        {
                uint32_t temp = MSFtoLBA(tmsf.min, tmsf.sec, tmsf.fr);
                b[len++] = temp >> 24;
                b[len++] = temp >> 16;
                b[len++] = temp >> 8;
                b[len++] = temp;
        }

        return len;
}

static int iso_readtoc_raw(unsigned char *b, int maxlen)
{
        if (!cdrom) return 0;

        int track;
        int len = 4;

        int first_track;
        int last_track;
        int number;
        unsigned char attr;
        TMSF tmsf;
        cdrom->GetAudioTracks(first_track, last_track, tmsf);

        b[2] = first_track;
        b[3] = last_track;

        for (track = first_track; track <= last_track; track++)
        {
                if ((len + 11) > maxlen)
                {
                        pclog("iso_readtocraw: This iteration would fill the buffer beyond the bounds, aborting...\n");
                        return len;
                }

                cdrom->GetAudioTrackInfo(track, number, tmsf, attr);

//              pclog("read_toc: Track %02X - number %02X control %02X adr %02X address %02X %02X %02X %02X\n", track, toc[track].cdte_track, toc[track].cdte_ctrl, toc[track].cdte_adr, 0, toc[track].cdte_addr.msf.minute, toc[track].cdte_addr.msf.second, toc[track].cdte_addr.msf.frame);

                b[len++] = track;
                b[len++]= attr;
                b[len++]=0;
                b[len++]=0;
                b[len++]=0;
                b[len++]=0;
                b[len++]=0;
                b[len++]=0;
                b[len++] = tmsf.min;
                b[len++] = tmsf.sec;
                b[len++] = tmsf.fr;
        }
        return len;
}

static uint32_t iso_size()
{
        return cdrom_capacity;
}

static int iso_status()
{
        if (!cdrom) return CD_STATUS_EMPTY;
        if (cdrom->HasAudioTracks())
        {
                switch(iso_cd_state)
                {
                        case CD_PLAYING:
                        return CD_STATUS_PLAYING;
                        case CD_PAUSED:
                        return CD_STATUS_PAUSED;
                        case CD_STOPPED:
                        default:
                        return CD_STATUS_STOPPED;
                }
        }
        return CD_STATUS_DATA_ONLY;
}
void iso_reset()
{
}

void iso_close(void)
{
        iso_cd_state = CD_STOPPED;
        if (cdrom)
        {
                delete cdrom;
                cdrom = NULL;
        }
        memset(iso_path, 0, 1024);
}

int iso_open(char* fn)
{
        if (strcmp(fn, iso_path) != 0)
                iso_changed = 1;

        /* Make sure iso_changed stays when changing from ISO to another ISO. */
        if (cdrom_drive != CDROM_ISO)
                iso_changed = 1;
        /* strcpy fails on OSX if both parameters are pointing to the same address */
        if (iso_path != fn)
                strcpy(iso_path, fn);

        cdrom = new CDROM_Interface_Image();
        if (!cdrom->SetDevice(fn, false))
        {
                iso_close();
                return 1;
        }
        iso_cd_state = CD_STOPPED;
        iso_cd_pos = 0;
        cd_buflen = 0;
        cdrom_capacity = iso_get_last_block(0, 0, 4096, 0);
        atapi = &iso_atapi;
        return 0;
}

static void iso_exit(void)
{
}

ATAPI iso_atapi=
{
        iso_ready,
        iso_medium_changed,
        iso_readtoc,
        iso_readtoc_session,
        iso_readtoc_raw,
        iso_getcurrentsubchannel,
        iso_readsector,
        iso_readsector_raw,
        iso_playaudio,
        iso_seek,
        iso_load,
        iso_eject,
        iso_pause,
        iso_resume,
        iso_size,
	iso_status,
	iso_is_track_audio,
        iso_stop,
        iso_exit
};