
#include "config.h"

#include <stdio.h>
#include <stdlib.h>

#include "alMain.h"
#include "alMidi.h"
#include "alError.h"
#include "alu.h"

#include "midi/base.h"


static ALuint read_le32(Reader *stream)
{
    ALubyte buf[4];
    if(READ(stream, buf, 4) != 4)
    {
        READERR(stream) = 1;
        return 0;
    }
    return (buf[3]<<24) | (buf[2]<<16) | (buf[1]<<8) | buf[0];
}
static ALushort read_le16(Reader *stream)
{
    ALubyte buf[2];
    if(READ(stream, buf, 2) != 2)
    {
        READERR(stream) = 1;
        return 0;
    }
    return (buf[1]<<8) | buf[0];
}
static ALubyte read_8(Reader *stream)
{
    ALubyte buf[1];
    if(READ(stream, buf, 1) != 1)
    {
        READERR(stream) = 1;
        return 0;
    }
    return buf[0];
}
static void skip(Reader *stream, ALuint amt)
{
    while(amt > 0 && !READERR(stream))
    {
        char buf[4096];
        size_t got;

        got = READ(stream, buf, minu(sizeof(buf), amt));
        if(got == 0) READERR(stream) = 1;

        amt -= got;
    }
}

typedef struct Generator {
    ALushort mGenerator;
    ALushort mAmount;
} Generator;
static void Generator_read(Generator *self, Reader *stream)
{
    self->mGenerator = read_le16(stream);
    self->mAmount = read_le16(stream);
}

static const ALint DefaultGenValue[60] = {
    0, /* 0 - startAddrOffset */
    0, /* 1 - endAddrOffset */
    0, /* 2 - startloopAddrOffset */
    0, /* 3 - endloopAddrOffset */
    0, /* 4 - startAddrCoarseOffset */
    0, /* 5 - modLfoToPitch */
    0, /* 6 - vibLfoToPitch */
    0, /* 7 - modEnvToPitch */
    13500, /* 8 - initialFilterFc */
    0, /* 9 - initialFilterQ */
    0, /* 10 - modLfoToFilterFc */
    0, /* 11 - modEnvToFilterFc */
    0, /* 12 - endAddrCoarseOffset */
    0, /* 13 - modLfoToVolume */
    0, /* 14 -  */
    0, /* 15 - chorusEffectsSend */
    0, /* 16 - reverbEffectsSend */
    0, /* 17 - pan */
    0, /* 18 -  */
    0, /* 19 -  */
    0, /* 20 -  */
    -12000, /* 21 - delayModLFO */
    0, /* 22 - freqModLFO */
    -12000, /* 23 - delayVibLFO */
    0, /* 24 - freqVibLFO */
    -12000, /* 25 - delayModEnv */
    -12000, /* 26 - attackModEnv */
    -12000, /* 27 - holdModEnv */
    -12000, /* 28 - decayModEnv */
    0, /* 29 - sustainModEnv */
    -12000, /* 30 - releaseModEnv */
    0, /* 31 - keynumToModEnvHold */
    0, /* 32 - keynumToModEnvDecay */
    -12000, /* 33 - delayVolEnv */
    -12000, /* 34 - attackVolEnv */
    -12000, /* 35 - holdVolEnv */
    -12000, /* 36 - decayVolEnv */
    0, /* 37 - sustainVolEnv */
    -12000, /* 38 - releaseVolEnv */
    0, /* 39 - keynumToVolEnvHold */
    0, /* 40 - keynumToVolEnvDecay */
    0, /* 41 -  */
    0, /* 42 -  */
    0, /* 43 - keyRange */
    0, /* 44 - velRange */
    0, /* 45 - startloopAddrCoarseOffset */
    0, /* 46 - keynum */
    0, /* 47 - velocity */
    0, /* 48 - initialAttenuation */
    0, /* 49 -  */
    0, /* 50 - endloopAddrCoarseOffset */
    0, /* 51 - corseTune */
    0, /* 52 - fineTune */
    0, /* 53 -  */
    0, /* 54 - sampleModes */
    0, /* 55 -  */
    100, /* 56 - scaleTuning */
    0, /* 57 - exclusiveClass */
    0, /* 58 - overridingRootKey */
    0, /* 59 -  */
};

typedef struct Modulator {
    ALushort mSrcOp;
    ALushort mDstOp;
    ALshort mAmount;
    ALushort mAmtSrcOp;
    ALushort mTransOp;
} Modulator;
static void Modulator_read(Modulator *self, Reader *stream)
{
    self->mSrcOp = read_le16(stream);
    self->mDstOp = read_le16(stream);
    self->mAmount = read_le16(stream);
    self->mAmtSrcOp = read_le16(stream);
    self->mTransOp = read_le16(stream);
}

typedef struct Zone {
    ALushort mGenIdx;
    ALushort mModIdx;
} Zone;
static void Zone_read(Zone *self, Reader *stream)
{
    self->mGenIdx = read_le16(stream);
    self->mModIdx = read_le16(stream);
}

typedef struct PresetHeader {
    ALchar mName[20];
    ALushort mPreset; /* MIDI program number */
    ALushort mBank;
    ALushort mZoneIdx;
    ALuint mLibrary;
    ALuint mGenre;
    ALuint mMorphology;
} PresetHeader;
static void PresetHeader_read(PresetHeader *self, Reader *stream)
{
    if(READ(stream, self->mName, sizeof(self->mName)) != sizeof(self->mName))
        READERR(stream) = 1;
    self->mPreset = read_le16(stream);
    self->mBank = read_le16(stream);
    self->mZoneIdx = read_le16(stream);
    self->mLibrary = read_le32(stream);
    self->mGenre = read_le32(stream);
    self->mMorphology = read_le32(stream);
}

typedef struct InstrumentHeader {
    ALchar mName[20];
    ALushort mZoneIdx;
} InstrumentHeader;
static void InstrumentHeader_read(InstrumentHeader *self, Reader *stream)
{
    if(READ(stream, self->mName, sizeof(self->mName)) != sizeof(self->mName))
        READERR(stream) = 1;
    self->mZoneIdx = read_le16(stream);
}

typedef struct SampleHeader {
    ALchar mName[20]; // 20 bytes
    ALuint mStart;
    ALuint mEnd;
    ALuint mStartloop;
    ALuint mEndloop;
    ALuint mSampleRate;
    ALubyte mOriginalKey;
    ALbyte mCorrection;
    ALushort mSampleLink;
    ALushort mSampleType;
} SampleHeader;
static void SampleHeader_read(SampleHeader *self, Reader *stream)
{
    if(READ(stream, self->mName, sizeof(self->mName)) != sizeof(self->mName))
        READERR(stream) = 1;
    self->mStart = read_le32(stream);
    self->mEnd = read_le32(stream);
    self->mStartloop = read_le32(stream);
    self->mEndloop = read_le32(stream);
    self->mSampleRate = read_le32(stream);
    self->mOriginalKey = read_8(stream);
    self->mCorrection = read_8(stream);
    self->mSampleLink = read_le16(stream);
    self->mSampleType = read_le16(stream);
}


typedef struct Soundfont {
    PresetHeader *phdr;
    ALsizei phdr_size;

    Zone *pbag;
    ALsizei pbag_size;
    Modulator *pmod;
    ALsizei pmod_size;
    Generator *pgen;
    ALsizei pgen_size;

    InstrumentHeader *inst;
    ALsizei inst_size;

    Zone *ibag;
    ALsizei ibag_size;
    Modulator *imod;
    ALsizei imod_size;
    Generator *igen;
    ALsizei igen_size;

    SampleHeader *shdr;
    ALsizei shdr_size;
} Soundfont;

static void Soundfont_Construct(Soundfont *self)
{
    self->phdr = NULL;
    self->phdr_size = 0;

    self->pbag = NULL;
    self->pbag_size = 0;
    self->pmod = NULL;
    self->pmod_size = 0;
    self->pgen = NULL;
    self->pgen_size = 0;

    self->inst = NULL;
    self->inst_size = 0;

    self->ibag = NULL;
    self->ibag_size = 0;
    self->imod = NULL;
    self->imod_size = 0;
    self->igen = NULL;
    self->igen_size = 0;

    self->shdr = NULL;
    self->shdr_size = 0;
}

static void Soundfont_Destruct(Soundfont *self)
{
    free(self->phdr);
    self->phdr = NULL;
    self->phdr_size = 0;

    free(self->pbag);
    self->pbag = NULL;
    self->pbag_size = 0;
    free(self->pmod);
    self->pmod = NULL;
    self->pmod_size = 0;
    free(self->pgen);
    self->pgen = NULL;
    self->pgen_size = 0;

    free(self->inst);
    self->inst = NULL;
    self->inst_size = 0;

    free(self->ibag);
    self->ibag = NULL;
    self->ibag_size = 0;
    free(self->imod);
    self->imod = NULL;
    self->imod_size = 0;
    free(self->igen);
    self->igen = NULL;
    self->igen_size = 0;

    free(self->shdr);
    self->shdr = NULL;
    self->shdr_size = 0;
}


#define FOURCC(a,b,c,d) (((d)<<24) | ((c)<<16) | ((b)<<8) | (a))
#define FOURCCARGS(x)  (char)((x)&0xff), (char)(((x)>>8)&0xff), (char)(((x)>>16)&0xff), (char)(((x)>>24)&0xff)
typedef struct RiffHdr {
    ALuint mCode;
    ALuint mSize;
} RiffHdr;
static void RiffHdr_read(RiffHdr *self, Reader *stream)
{
    self->mCode = read_le32(stream);
    self->mSize = read_le32(stream);
}


typedef struct GenModList {
    Generator *gens;
    ALsizei gens_size;
    ALsizei gens_max;

    Modulator *mods;
    ALsizei mods_size;
    ALsizei mods_max;
} GenModList;

static void GenModList_Construct(GenModList *self)
{
    self->gens = NULL;
    self->gens_size = 0;
    self->gens_max = 0;

    self->mods = NULL;
    self->mods_size = 0;
    self->mods_max = 0;
}

static void GenModList_Destruct(GenModList *self)
{
    free(self->gens);
    self->gens = NULL;
    self->gens_size = 0;
    self->gens_max = 0;

    free(self->mods);
    self->mods = NULL;
    self->mods_size = 0;
    self->mods_max = 0;
}

static GenModList GenModList_clone(const GenModList *self)
{
    GenModList ret;

    ret.gens = malloc(self->gens_max * sizeof(ret.gens[0]));
    memcpy(ret.gens, self->gens, self->gens_size * sizeof(ret.gens[0]));
    ret.gens_size = self->gens_size;
    ret.gens_max = self->gens_max;

    ret.mods = malloc(self->mods_max * sizeof(ret.mods[0]));
    memcpy(ret.mods, self->mods, self->mods_size * sizeof(ret.mods[0]));
    ret.mods_size = self->mods_size;
    ret.mods_max = self->mods_max;

    return ret;
}

static void GenModList_insertGen(GenModList *self, const Generator *gen, ALboolean ispreset)
{
    Generator *i = self->gens;
    Generator *end = i + self->gens_size;
    for(;i != end;i++)
    {
        if(i->mGenerator == gen->mGenerator)
        {
            i->mAmount = gen->mAmount;
            return;
        }
    }

    if(ispreset &&
       (gen->mGenerator == 0 || gen->mGenerator == 1 || gen->mGenerator == 2 ||
        gen->mGenerator == 3 || gen->mGenerator == 4 || gen->mGenerator == 12 ||
        gen->mGenerator == 45 || gen->mGenerator == 46 || gen->mGenerator == 47 ||
        gen->mGenerator == 50 || gen->mGenerator == 54 || gen->mGenerator == 57 ||
        gen->mGenerator == 58))
        return;

    if(self->gens_size == self->gens_max)
    {
        void *temp = NULL;
        ALsizei newsize;

        newsize = (self->gens_max ? self->gens_max<<1 : 1);
        if(newsize > self->gens_max)
            temp = realloc(self->gens, newsize * sizeof(self->gens[0]));
        if(!temp)
        {
            ERR("Failed to increase generator storage to %d elements (from %d)\n",
                newsize, self->gens_max);
            return;
        }

        self->gens = temp;
        self->gens_max = newsize;
    }

    self->gens[self->gens_size] = *gen;
    self->gens_size++;
}
static void GenModList_accumGen(GenModList *self, const Generator *gen)
{
    Generator *i = self->gens;
    Generator *end = i + self->gens_size;
    for(;i != end;i++)
    {
        if(i->mGenerator == gen->mGenerator)
        {
            if(gen->mGenerator == 43 || gen->mGenerator == 44)
            {
                /* Range generators accumulate by taking the intersection of
                 * the two ranges.
                 */
                ALushort low = maxu(i->mAmount&0x00ff, gen->mAmount&0x00ff);
                ALushort high = minu(i->mAmount&0xff00, gen->mAmount&0xff00);
                i->mAmount = low | high;
            }
            else
                i->mAmount += gen->mAmount;
            return;
        }
    }

    if(self->gens_size == self->gens_max)
    {
        void *temp = NULL;
        ALsizei newsize;

        newsize = (self->gens_max ? self->gens_max<<1 : 1);
        if(newsize > self->gens_max)
            temp = realloc(self->gens, newsize * sizeof(self->gens[0]));
        if(!temp)
        {
            ERR("Failed to increase generator storage to %d elements (from %d)\n",
                newsize, self->gens_max);
            return;
        }

        self->gens = temp;
        self->gens_max = newsize;
    }

    self->gens[self->gens_size] = *gen;
    if(gen->mGenerator < 60)
        self->gens[self->gens_size].mAmount += DefaultGenValue[gen->mGenerator];
    self->gens_size++;
}

static void GenModList_insertMod(GenModList *self, const Modulator *mod)
{
    Modulator *i = self->mods;
    Modulator *end = i + self->mods_size;
    for(;i != end;i++)
    {
        if(i->mDstOp == mod->mDstOp && i->mSrcOp == mod->mSrcOp &&
           i->mAmtSrcOp == mod->mAmtSrcOp && i->mTransOp == mod->mTransOp)
        {
            i->mAmount = mod->mAmount;
            return;
        }
    }

    if(self->mods_size == self->mods_max)
    {
        void *temp = NULL;
        ALsizei newsize;

        newsize = (self->mods_max ? self->mods_max<<1 : 1);
        if(newsize > self->mods_max)
            temp = realloc(self->mods, newsize * sizeof(self->mods[0]));
        if(!temp)
        {
            ERR("Failed to increase modulator storage to %d elements (from %d)\n",
                newsize, self->mods_max);
            return;
        }

        self->mods = temp;
        self->mods_max = newsize;
    }

    self->mods[self->mods_size] = *mod;
    self->mods_size++;
}
static void GenModList_accumMod(GenModList *self, const Modulator *mod)
{
    Modulator *i = self->mods;
    Modulator *end = i + self->mods_size;
    for(;i != end;i++)
    {
        if(i->mDstOp == mod->mDstOp && i->mSrcOp == mod->mSrcOp &&
           i->mAmtSrcOp == mod->mAmtSrcOp && i->mTransOp == mod->mTransOp)
        {
            i->mAmount += mod->mAmount;
            return;
        }
    }

    if(self->mods_size == self->mods_max)
    {
        void *temp = NULL;
        ALsizei newsize;

        newsize = (self->mods_max ? self->mods_max<<1 : 1);
        if(newsize > self->mods_max)
            temp = realloc(self->mods, newsize * sizeof(self->mods[0]));
        if(!temp)
        {
            ERR("Failed to increase modulator storage to %d elements (from %d)\n",
                newsize, self->mods_max);
            return;
        }

        self->mods = temp;
        self->mods_max = newsize;
    }

    self->mods[self->mods_size] = *mod;
    if(mod->mSrcOp == 0x0502 && mod->mDstOp == 48 && mod->mAmtSrcOp == 0 && mod->mTransOp == 0)
        self->mods[self->mods_size].mAmount += 960;
    else if(mod->mSrcOp == 0x0102 && mod->mDstOp == 8 && mod->mAmtSrcOp == 0 && mod->mTransOp == 0)
        self->mods[self->mods_size].mAmount += -2400;
    else if(mod->mSrcOp == 0x000D && mod->mDstOp == 6 && mod->mAmtSrcOp == 0 && mod->mTransOp == 0)
        self->mods[self->mods_size].mAmount += 50;
    else if(mod->mSrcOp == 0x0081 && mod->mDstOp == 6 && mod->mAmtSrcOp == 0 && mod->mTransOp == 0)
        self->mods[self->mods_size].mAmount += 50;
    else if(mod->mSrcOp == 0x0582 && mod->mDstOp == 48 && mod->mAmtSrcOp == 0 && mod->mTransOp == 0)
        self->mods[self->mods_size].mAmount += 960;
    else if(mod->mSrcOp == 0x028A && mod->mDstOp == 17 && mod->mAmtSrcOp == 0 && mod->mTransOp == 0)
        self->mods[self->mods_size].mAmount += 1000;
    else if(mod->mSrcOp == 0x058B && mod->mDstOp == 48 && mod->mAmtSrcOp == 0 && mod->mTransOp == 0)
        self->mods[self->mods_size].mAmount += 960;
    else if(mod->mSrcOp == 0x00DB && mod->mDstOp == 16 && mod->mAmtSrcOp == 0 && mod->mTransOp == 0)
        self->mods[self->mods_size].mAmount += 200;
    else if(mod->mSrcOp == 0x00DD && mod->mDstOp == 15 && mod->mAmtSrcOp == 0 && mod->mTransOp == 0)
        self->mods[self->mods_size].mAmount += 200;
    /*else if(mod->mSrcOp == 0x020E && mod->mDstOp == ?initialpitch? && mod->mAmtSrcOp == 0x0010 && mod->mTransOp == 0)
        self->mods[self->mods_size].mAmount += 12700;*/
    self->mods_size++;
}


#define ERROR_GOTO(lbl_, ...)  do {                                           \
    ERR(__VA_ARGS__);                                                         \
    goto lbl_;                                                                \
} while(0)

static ALboolean ensureFontSanity(const Soundfont *sfont)
{
    ALsizei i;

    for(i = 0;i < sfont->phdr_size-1;i++)
    {
        if(sfont->phdr[i].mZoneIdx >= sfont->pbag_size)
        {
            WARN("Preset %d has invalid zone index %d (max: %d)\n", i,
                 sfont->phdr[i].mZoneIdx, sfont->pbag_size);
            return AL_FALSE;
        }
        if(sfont->phdr[i].mZoneIdx > sfont->phdr[i+1].mZoneIdx)
        {
            WARN("Preset %d has invalid zone index (%d does not follow %d)\n", i+1,
                 sfont->phdr[i+1].mZoneIdx, sfont->phdr[i].mZoneIdx);
            return AL_FALSE;
        }
    }
    if(sfont->phdr[i].mZoneIdx >= sfont->pbag_size)
    {
        WARN("Preset %d has invalid zone index %d (max: %d)\n", i,
             sfont->phdr[i].mZoneIdx, sfont->pbag_size);
        return AL_FALSE;
    }

    for(i = 0;i < sfont->pbag_size-1;i++)
    {
        if(sfont->pbag[i].mGenIdx >= sfont->pgen_size)
        {
            WARN("Preset zone %d has invalid generator index %d (max: %d)\n", i,
                 sfont->pbag[i].mGenIdx, sfont->pgen_size);
            return AL_FALSE;
        }
        if(sfont->pbag[i].mGenIdx > sfont->pbag[i+1].mGenIdx)
        {
            WARN("Preset zone %d has invalid generator index (%d does not follow %d)\n", i+1,
                 sfont->pbag[i+1].mGenIdx, sfont->pbag[i].mGenIdx);
            return AL_FALSE;
        }
        if(sfont->pbag[i].mModIdx >= sfont->pmod_size)
        {
            WARN("Preset zone %d has invalid modulator index %d (max: %d)\n", i,
                 sfont->pbag[i].mModIdx, sfont->pmod_size);
            return AL_FALSE;
        }
        if(sfont->pbag[i].mModIdx > sfont->pbag[i+1].mModIdx)
        {
            WARN("Preset zone %d has invalid modulator index (%d does not follow %d)\n", i+1,
                 sfont->pbag[i+1].mModIdx, sfont->pbag[i].mModIdx);
            return AL_FALSE;
        }
    }
    if(sfont->pbag[i].mGenIdx >= sfont->pgen_size)
    {
        WARN("Preset zone %d has invalid generator index %d (max: %d)\n", i,
             sfont->pbag[i].mGenIdx, sfont->pgen_size);
        return AL_FALSE;
    }
    if(sfont->pbag[i].mModIdx >= sfont->pmod_size)
    {
        WARN("Preset zone %d has invalid modulator index %d (max: %d)\n", i,
             sfont->pbag[i].mModIdx, sfont->pmod_size);
        return AL_FALSE;
    }


    for(i = 0;i < sfont->inst_size-1;i++)
    {
        if(sfont->inst[i].mZoneIdx >= sfont->ibag_size)
        {
            WARN("Instrument %d has invalid zone index %d (max: %d)\n", i+1,
                 sfont->inst[i].mZoneIdx, sfont->ibag_size);
            return AL_FALSE;
        }
        if(sfont->inst[i].mZoneIdx > sfont->inst[i+1].mZoneIdx)
        {
            WARN("Instrument %d has invalid zone index (%d does not follow %d)\n", i+1,
                 sfont->inst[i+1].mZoneIdx, sfont->inst[i].mZoneIdx);
            return AL_FALSE;
        }
    }
    if(sfont->inst[i].mZoneIdx >= sfont->ibag_size)
    {
        WARN("Instrument %d has invalid zone index %d (max: %d)\n", i+1,
             sfont->inst[i].mZoneIdx, sfont->ibag_size);
        return AL_FALSE;
    }

    for(i = 0;i < sfont->ibag_size-1;i++)
    {
        if(sfont->ibag[i].mGenIdx >= sfont->igen_size)
        {
            WARN("Instrument zone %d has invalid generator index %d (max: %d)\n", i,
                 sfont->ibag[i].mGenIdx, sfont->igen_size);
            return AL_FALSE;
        }
        if(sfont->ibag[i].mGenIdx > sfont->ibag[i+1].mGenIdx)
        {
            WARN("Instrument zone %d has invalid generator index (%d does not follow %d)\n", i+1,
                 sfont->ibag[i+1].mGenIdx, sfont->ibag[i].mGenIdx);
            return AL_FALSE;
        }
        if(sfont->ibag[i].mModIdx >= sfont->imod_size)
        {
            WARN("Instrument zone %d has invalid modulator index %d (max: %d)\n", i,
                 sfont->ibag[i].mModIdx, sfont->imod_size);
            return AL_FALSE;
        }
        if(sfont->ibag[i].mModIdx > sfont->ibag[i+1].mModIdx)
        {
            WARN("Instrument zone %d has invalid modulator index (%d does not follow %d)\n", i+1,
                 sfont->ibag[i+1].mModIdx, sfont->ibag[i].mModIdx);
            return AL_FALSE;
        }
    }
    if(sfont->ibag[i].mGenIdx >= sfont->igen_size)
    {
        WARN("Instrument zone %d has invalid generator index %d (max: %d)\n", i,
             sfont->ibag[i].mGenIdx, sfont->igen_size);
        return AL_FALSE;
    }
    if(sfont->ibag[i].mModIdx >= sfont->imod_size)
    {
        WARN("Instrument zone %d has invalid modulator index %d (max: %d)\n", i,
             sfont->ibag[i].mModIdx, sfont->imod_size);
        return AL_FALSE;
    }

    return AL_TRUE;
}

static ALboolean ensureZoneSanity(const GenModList *zone, int instidx, int zoneidx, int splidx)
{
    ALsizei i;

    for(i = 0;i < zone->gens_size;i++)
    {
        if(zone->gens[i].mGenerator == 43 || zone->gens[i].mGenerator == 44)
        {
            int high = zone->gens[i].mAmount>>8;
            int low = zone->gens[i].mAmount&0xff;

            if(!(low >= 0 && low <= 127 && low >= 0 && low <= 127 && high >= low))
            {
                TRACE("Instrument %d zone %d sample %d, invalid %s range: %d...%d\n",
                      instidx, zoneidx, splidx,
                      (zone->gens[i].mGenerator == 43) ? "key" :
                      (zone->gens[i].mGenerator == 44) ? "vel" : "(unk)",
                      low, high);
                return AL_FALSE;
            }
        }
    }

    return AL_TRUE;
}

static void fillZone(ALfontsound *sound, ALCcontext *context, const GenModList *zone)
{
    static const ALenum Gen2Param[60] = {
        0, /* 0 - startAddrOffset */
        0, /* 1 - endAddrOffset */
        0, /* 2 - startloopAddrOffset */
        0, /* 3 - endloopAddrOffset */
        0, /* 4 - startAddrCoarseOffset */
        AL_MOD_LFO_TO_PITCH_SOFT, /* 5 - modLfoToPitch */
        AL_VIBRATO_LFO_TO_PITCH_SOFT, /* 6 - vibLfoToPitch */
        AL_MOD_ENV_TO_PITCH_SOFT, /* 7 - modEnvToPitch */
        AL_FILTER_CUTOFF_SOFT, /* 8 - initialFilterFc */
        AL_FILTER_RESONANCE_SOFT, /* 9 - initialFilterQ */
        AL_MOD_LFO_TO_FILTER_CUTOFF_SOFT, /* 10 - modLfoToFilterFc */
        AL_MOD_ENV_TO_FILTER_CUTOFF_SOFT, /* 11 - modEnvToFilterFc */
        0, /* 12 - endAddrCoarseOffset */
        AL_MOD_LFO_TO_VOLUME_SOFT, /* 13 - modLfoToVolume */
        0, /* 14 -  */
        AL_CHORUS_SEND_SOFT, /* 15 - chorusEffectsSend */
        AL_REVERB_SEND_SOFT, /* 16 - reverbEffectsSend */
        AL_PAN_SOFT, /* 17 - pan */
        0, /* 18 -  */
        0, /* 19 -  */
        0, /* 20 -  */
        AL_MOD_LFO_DELAY_SOFT, /* 21 - delayModLFO */
        AL_MOD_LFO_FREQUENCY_SOFT, /* 22 - freqModLFO */
        AL_VIBRATO_LFO_DELAY_SOFT, /* 23 - delayVibLFO */
        AL_VIBRATO_LFO_FREQUENCY_SOFT, /* 24 - freqVibLFO */
        AL_MOD_ENV_DELAYTIME_SOFT, /* 25 - delayModEnv */
        AL_MOD_ENV_ATTACKTIME_SOFT, /* 26 - attackModEnv */
        AL_MOD_ENV_HOLDTIME_SOFT, /* 27 - holdModEnv */
        AL_MOD_ENV_DECAYTIME_SOFT, /* 28 - decayModEnv */
        AL_MOD_ENV_SUSTAINVOLUME_SOFT, /* 29 - sustainModEnv */
        AL_MOD_ENV_RELEASETIME_SOFT, /* 30 - releaseModEnv */
        AL_MOD_ENV_KEY_TO_HOLDTIME_SOFT, /* 31 - keynumToModEnvHold */
        AL_MOD_ENV_KEY_TO_DECAYTIME_SOFT, /* 32 - keynumToModEnvDecay */
        AL_VOLUME_ENV_DELAYTIME_SOFT, /* 33 - delayVolEnv */
        AL_VOLUME_ENV_ATTACKTIME_SOFT, /* 34 - attackVolEnv */
        AL_VOLUME_ENV_HOLDTIME_SOFT, /* 35 - holdVolEnv */
        AL_VOLUME_ENV_DECAYTIME_SOFT, /* 36 - decayVolEnv */
        AL_VOLUME_ENV_SUSTAINVOLUME_SOFT, /* 37 - sustainVolEnv */
        AL_VOLUME_ENV_RELEASETIME_SOFT, /* 38 - releaseVolEnv */
        AL_VOLUME_ENV_KEY_TO_HOLDTIME_SOFT, /* 39 - keynumToVolEnvHold */
        AL_VOLUME_ENV_KEY_TO_DECAYTIME_SOFT, /* 40 - keynumToVolEnvDecay */
        0, /* 41 -  */
        0, /* 42 -  */
        AL_KEY_RANGE_SOFT, /* 43 - keyRange */
        AL_VELOCITY_RANGE_SOFT, /* 44 - velRange */
        0, /* 45 - startloopAddrCoarseOffset */
        0, /* 46 - keynum */
        0, /* 47 - velocity */
        AL_ATTENUATION_SOFT, /* 48 - initialAttenuation */
        0, /* 49 -  */
        0, /* 50 - endloopAddrCoarseOffset */
        AL_TUNING_COARSE_SOFT, /* 51 - corseTune */
        AL_TUNING_FINE_SOFT, /* 52 - fineTune */
        0, /* 53 -  */
        AL_LOOP_MODE_SOFT, /* 54 - sampleModes */
        0, /* 55 -  */
        AL_TUNING_SCALE_SOFT, /* 56 - scaleTuning */
        AL_EXCLUSIVE_CLASS_SOFT, /* 57 - exclusiveClass */
        AL_BASE_KEY_SOFT, /* 58 - overridingRootKey */
        0, /* 59 -  */
    };
    const Generator *gen, *gen_end;

    if(zone->mods)
    {
        /* FIXME: Handle modulators */
    }

    gen = zone->gens;
    gen_end = gen + zone->gens_size;
    for(;gen != gen_end;gen++)
    {
        ALint value = (ALshort)gen->mAmount;
        if(gen->mGenerator == 0)
            sound->Start += value;
        else if(gen->mGenerator == 1)
            sound->End += value;
        else if(gen->mGenerator == 2)
            sound->LoopStart += value;
        else if(gen->mGenerator == 3)
            sound->LoopEnd += value;
        else if(gen->mGenerator == 4)
            sound->Start += value<<15;
        else if(gen->mGenerator == 12)
            sound->End += value<<15;
        else if(gen->mGenerator == 45)
            sound->LoopStart += value<<15;
        else if(gen->mGenerator == 50)
            sound->LoopEnd += value<<15;
        else if(gen->mGenerator == 43)
        {
            sound->MinKey = mini((value&0xff), 127);
            sound->MaxKey = mini(((value>>8)&0xff), 127);
        }
        else if(gen->mGenerator == 44)
        {
            sound->MinVelocity = mini((value&0xff), 127);
            sound->MaxVelocity = mini(((value>>8)&0xff), 127);
        }
        else
        {
            ALenum param = 0;
            if(gen->mGenerator < 60)
                param = Gen2Param[gen->mGenerator];
            if(param)
            {
                if(param == AL_BASE_KEY_SOFT && value == -1)
                    break;
                if(param == AL_FILTER_RESONANCE_SOFT || param == AL_ATTENUATION_SOFT)
                    value = maxi(0, value);
                else if(param == AL_CHORUS_SEND_SOFT || param == AL_REVERB_SEND_SOFT)
                    value = clampi(value, 0, 1000);
                else if(param == AL_LOOP_MODE_SOFT)
                {
                    if(!(value == 0 || value == 1 || value == 3))
                        value = 0;
                }
                ALfontsound_setPropi(sound, context, param, value);
            }
            else if(gen->mGenerator < 256)
            {
                static ALboolean warned[256];
                if(!warned[gen->mGenerator])
                {
                    warned[gen->mGenerator] = AL_TRUE;
                    ERR("Unhandled generator %d\n", gen->mGenerator);
                }
            }
        }
    }
}

static void processInstrument(ALfontsound ***sounds, ALsizei *sounds_size, ALCcontext *context, InstrumentHeader *inst, const Soundfont *sfont, const GenModList *pzone)
{
    const Generator *gen, *gen_end;
    const Modulator *mod, *mod_end;
    const Zone *zone, *zone_end;
    GenModList gzone;
    ALvoid *temp;

    if((inst+1)->mZoneIdx == inst->mZoneIdx)
        ERR("Instrument with no zones!");

    GenModList_Construct(&gzone);
    zone = sfont->ibag + inst->mZoneIdx;
    zone_end = sfont->ibag + (inst+1)->mZoneIdx;
    if(zone_end-zone > 1)
    {
        gen = sfont->igen + zone->mGenIdx;
        gen_end = sfont->igen + (zone+1)->mGenIdx;

        // If no generators, or last generator is not a sample, this is a global zone
        for(;gen != gen_end;gen++)
        {
            if(gen->mGenerator == 53)
                break;
        }

        if(gen == gen_end)
        {
            gen = sfont->igen + zone->mGenIdx;
            gen_end = sfont->igen + (zone+1)->mGenIdx;
            for(;gen != gen_end;gen++)
                GenModList_insertGen(&gzone, gen, AL_FALSE);

            mod = sfont->imod + zone->mModIdx;
            mod_end = sfont->imod + (zone+1)->mModIdx;
            for(;mod != mod_end;mod++)
                GenModList_insertMod(&gzone, mod);

            zone++;
        }
    }

    temp = realloc(*sounds, (zone_end-zone + *sounds_size)*sizeof((*sounds)[0]));
    if(!temp)
    {
        ERR("Failed reallocating fontsound storage to %ld elements (from %d)\n",
            (zone_end-zone + *sounds_size), *sounds_size);
        return;
    }
    *sounds = temp;
    for(;zone != zone_end;zone++)
    {
        GenModList lzone = GenModList_clone(&gzone);
        mod = sfont->imod + zone->mModIdx;
        mod_end = sfont->imod + (zone+1)->mModIdx;
        for(;mod != mod_end;mod++)
            GenModList_insertMod(&lzone, mod);

        gen = sfont->igen + zone->mGenIdx;
        gen_end = sfont->igen + (zone+1)->mGenIdx;
        for(;gen != gen_end;gen++)
        {
            if(gen->mGenerator == 53)
            {
                const SampleHeader *samp;

                if(gen->mAmount >= sfont->shdr_size-1)
                {
                    ERR("Generator %ld has invalid sample ID generator (%d of %d)\n",
                        (long)(gen-sfont->igen), gen->mAmount, sfont->shdr_size-1);
                    break;
                }
                samp = &sfont->shdr[gen->mAmount];

                gen = pzone->gens;
                gen_end = gen + pzone->gens_size;
                for(;gen != gen_end;gen++)
                    GenModList_accumGen(&lzone, gen);

                mod = pzone->mods;
                mod_end = mod + pzone->mods_size;
                for(;mod != mod_end;mod++)
                    GenModList_accumMod(&lzone, mod);

                if(!ensureZoneSanity(&lzone, inst-sfont->inst, zone-sfont->ibag, samp-sfont->shdr))
                    break;

                (*sounds)[*sounds_size] = NewFontsound(context);
                (*sounds)[*sounds_size]->Start = samp->mStart;
                (*sounds)[*sounds_size]->End = samp->mEnd;
                (*sounds)[*sounds_size]->LoopStart = samp->mStartloop;
                (*sounds)[*sounds_size]->LoopEnd = samp->mEndloop;
                (*sounds)[*sounds_size]->SampleRate = samp->mSampleRate;
                (*sounds)[*sounds_size]->PitchKey = samp->mOriginalKey;
                (*sounds)[*sounds_size]->PitchCorrection = samp->mCorrection;
                (*sounds)[*sounds_size]->LoopMode = (samp->mSampleType&0x7ffff);
                fillZone((*sounds)[*sounds_size], context, &lzone);
                (*sounds_size)++;

                break;
            }
            GenModList_insertGen(&lzone, gen, AL_FALSE);
        }

        GenModList_Destruct(&lzone);
    }

    GenModList_Destruct(&gzone);
}

ALboolean loadSf2(Reader *stream, ALsoundfont *soundfont, ALCcontext *context)
{
    ALsfpreset **presets = NULL;
    ALsizei presets_size = 0;
    ALuint version = 0;
    ALuint ltype;
    Soundfont sfont;
    RiffHdr riff;
    RiffHdr list;
    ALsizei i;

    Soundfont_Construct(&sfont);

    RiffHdr_read(&riff, stream);
    if(riff.mCode != FOURCC('R','I','F','F'))
        ERROR_GOTO(error, "Invalid Format, expected RIFF got '%c%c%c%c'\n", FOURCCARGS(riff.mCode));
    if((ltype=read_le32(stream)) != FOURCC('s','f','b','k'))
        ERROR_GOTO(error, "Invalid Format, expected sfbk got '%c%c%c%c'\n", FOURCCARGS(ltype));

    if(READERR(stream) != 0)
        ERROR_GOTO(error, "Error reading file header\n");

    RiffHdr_read(&list, stream);
    if(list.mCode != FOURCC('L','I','S','T'))
        ERROR_GOTO(error, "Invalid Format, expected LIST (INFO) got '%c%c%c%c'\n", FOURCCARGS(list.mCode));
    if((ltype=read_le32(stream)) != FOURCC('I','N','F','O'))
        ERROR_GOTO(error, "Invalid Format, expected INFO got '%c%c%c%c'\n", FOURCCARGS(ltype));
    list.mSize -= 4;
    while(list.mSize > 0 && !READERR(stream))
    {
        RiffHdr info;

        RiffHdr_read(&info, stream);
        list.mSize -= 8;
        if(info.mCode == FOURCC('i','f','i','l'))
        {
            if(info.mSize != 4)
                ERR("Invalid ifil chunk size: %d\n", info.mSize);
            else
            {
                ALushort major = read_le16(stream);
                ALushort minor = read_le16(stream);

                info.mSize -= 4;
                list.mSize -= 4;

                version = (major<<16) | minor;
            }
        }
        list.mSize -= info.mSize;
        skip(stream, info.mSize);
    }

    if(READERR(stream) != 0)
        ERROR_GOTO(error, "Error reading INFO chunk\n");
    if(version>>16 != 2)
        ERROR_GOTO(error, "Unsupported format version: %d.%02d\n", version>>16, version&0xffff);
    TRACE("Loading SF2 format version: %d.%02d\n", version>>16, version&0xffff);

    RiffHdr_read(&list, stream);
    if(list.mCode != FOURCC('L','I','S','T'))
        ERROR_GOTO(error, "Invalid Format, expected LIST (sdta) got '%c%c%c%c'\n", FOURCCARGS(list.mCode));
    if((ltype=read_le32(stream)) != FOURCC('s','d','t','a'))
        ERROR_GOTO(error, "Invalid Format, expected sdta got '%c%c%c%c'\n", FOURCCARGS(ltype));
    list.mSize -= 4;
    {
        ALbyte *ptr;
        RiffHdr smpl;

        RiffHdr_read(&smpl, stream);
        if(smpl.mCode != FOURCC('s','m','p','l'))
            ERROR_GOTO(error, "Invalid Format, expected smpl got '%c%c%c%c'\n", FOURCCARGS(smpl.mCode));
        list.mSize -= 8;

        if(smpl.mSize > list.mSize)
            ERROR_GOTO(error, "Invalid Format, sample chunk size mismatch\n");

        if(!(ptr=realloc(soundfont->Samples, smpl.mSize)))
            SET_ERROR_AND_GOTO(context, AL_OUT_OF_MEMORY, error);
        soundfont->Samples = (ALshort*)ptr;
        soundfont->NumSamples = smpl.mSize/2;

        if(IS_LITTLE_ENDIAN)
            READ(stream, ptr, smpl.mSize);
        else
        {
            while(smpl.mSize > 0)
            {
                ALbyte buf[4096];
                ALuint todo = minu(smpl.mSize, sizeof(buf));
                ALuint i;

                READ(stream, buf, todo);
                for(i = 0;i < todo;i++)
                    ptr[i] = buf[i^1];
            }
        }
        list.mSize -= smpl.mSize;

        skip(stream, list.mSize);
    }

    if(READERR(stream) != 0)
        ERROR_GOTO(error, "Error reading sdta chunk\n");

    RiffHdr_read(&list, stream);
    if(list.mCode != FOURCC('L','I','S','T'))
        ERROR_GOTO(error, "Invalid Format, expected LIST (pdta) got '%c%c%c%c'\n", FOURCCARGS(list.mCode));
    if((ltype=read_le32(stream)) != FOURCC('p','d','t','a'))
        ERROR_GOTO(error, "Invalid Format, expected pdta got '%c%c%c%c'\n", FOURCCARGS(ltype));

    //
    RiffHdr_read(&list, stream);
    if(list.mCode != FOURCC('p','h','d','r'))
        ERROR_GOTO(error, "Invalid Format, expected phdr got '%c%c%c%c'\n", FOURCCARGS(list.mCode));
    if((list.mSize%38) != 0)
        ERROR_GOTO(error, "Invalid Format, bad phdr size\n");
    sfont.phdr_size = list.mSize/38;
    sfont.phdr = calloc(sfont.phdr_size, sizeof(sfont.phdr[0]));
    for(i = 0;i < sfont.phdr_size;i++)
        PresetHeader_read(&sfont.phdr[i], stream);

    RiffHdr_read(&list, stream);
    if(list.mCode != FOURCC('p','b','a','g'))
        ERROR_GOTO(error, "Invalid Format, expected pbag got '%c%c%c%c'\n", FOURCCARGS(list.mCode));
    if((list.mSize%4) != 0)
        ERROR_GOTO(error, "Invalid Format, bad pbag size\n");
    sfont.pbag_size = list.mSize/4;
    sfont.pbag = calloc(sfont.pbag_size, sizeof(sfont.pbag[0]));
    for(i = 0;i < sfont.pbag_size;i++)
        Zone_read(&sfont.pbag[i], stream);

    RiffHdr_read(&list, stream);
    if(list.mCode != FOURCC('p','m','o','d'))
        ERROR_GOTO(error, "Invalid Format, expected pmod got '%c%c%c%c'\n", FOURCCARGS(list.mCode));
    if((list.mSize%10) != 0)
        ERROR_GOTO(error, "Invalid Format, bad pmod size\n");
    sfont.pmod_size = list.mSize/10;
    sfont.pmod = calloc(sfont.pmod_size, sizeof(sfont.pmod[0]));
    for(i = 0;i < sfont.pmod_size;i++)
        Modulator_read(&sfont.pmod[i], stream);

    RiffHdr_read(&list, stream);
    if(list.mCode != FOURCC('p','g','e','n'))
        ERROR_GOTO(error, "Invalid Format, expected pgen got '%c%c%c%c'\n", FOURCCARGS(list.mCode));
    if((list.mSize%4) != 0)
        ERROR_GOTO(error, "Invalid Format, bad pgen size\n");
    sfont.pgen_size = list.mSize/4;
    sfont.pgen = calloc(sfont.pgen_size, sizeof(sfont.pgen[0]));
    for(i = 0;i < sfont.pgen_size;i++)
        Generator_read(&sfont.pgen[i], stream);

    //
    RiffHdr_read(&list, stream);
    if(list.mCode != FOURCC('i','n','s','t'))
        ERROR_GOTO(error, "Invalid Format, expected inst got '%c%c%c%c'\n", FOURCCARGS(list.mCode));
    if((list.mSize%22) != 0)
        ERROR_GOTO(error, "Invalid Format, bad inst size\n");
    sfont.inst_size = list.mSize/22;
    sfont.inst = calloc(sfont.inst_size, sizeof(sfont.inst[0]));
    for(i = 0;i < sfont.inst_size;i++)
        InstrumentHeader_read(&sfont.inst[i], stream);

    RiffHdr_read(&list, stream);
    if(list.mCode != FOURCC('i','b','a','g'))
        ERROR_GOTO(error, "Invalid Format, expected ibag got '%c%c%c%c'\n", FOURCCARGS(list.mCode));
    if((list.mSize%4) != 0)
        ERROR_GOTO(error, "Invalid Format, bad ibag size\n");
    sfont.ibag_size = list.mSize/4;
    sfont.ibag = calloc(sfont.ibag_size, sizeof(sfont.ibag[0]));
    for(i = 0;i < sfont.ibag_size;i++)
        Zone_read(&sfont.ibag[i], stream);

    RiffHdr_read(&list, stream);
    if(list.mCode != FOURCC('i','m','o','d'))
        ERROR_GOTO(error, "Invalid Format, expected imod got '%c%c%c%c'\n", FOURCCARGS(list.mCode));
    if((list.mSize%10) != 0)
        ERROR_GOTO(error, "Invalid Format, bad imod size\n");
    sfont.imod_size = list.mSize/10;
    sfont.imod = calloc(sfont.imod_size, sizeof(sfont.imod[0]));
    for(i = 0;i < sfont.imod_size;i++)
        Modulator_read(&sfont.imod[i], stream);

    RiffHdr_read(&list, stream);
    if(list.mCode != FOURCC('i','g','e','n'))
        ERROR_GOTO(error, "Invalid Format, expected igen got '%c%c%c%c'\n", FOURCCARGS(list.mCode));
    if((list.mSize%4) != 0)
        ERROR_GOTO(error, "Invalid Format, bad igen size\n");
    sfont.igen_size = list.mSize/4;
    sfont.igen = calloc(sfont.igen_size, sizeof(sfont.igen[0]));
    for(i = 0;i < sfont.igen_size;i++)
        Generator_read(&sfont.igen[i], stream);

    //
    RiffHdr_read(&list, stream);
    if(list.mCode != FOURCC('s','h','d','r'))
        ERROR_GOTO(error, "Invalid Format, expected shdr got '%c%c%c%c'\n", FOURCCARGS(list.mCode));
    if((list.mSize%46) != 0)
        ERROR_GOTO(error, "Invalid Format, bad shdr size\n");
    sfont.shdr_size = list.mSize/46;
    sfont.shdr = calloc(sfont.shdr_size, sizeof(sfont.shdr[0]));
    for(i = 0;i < sfont.shdr_size;i++)
        SampleHeader_read(&sfont.shdr[i], stream);

    if(READERR(stream) != 0)
        ERROR_GOTO(error, "Error reading pdta chunk\n");

    if(!ensureFontSanity(&sfont))
        goto error;

    presets = calloc(1, (sfont.phdr_size-1)*sizeof(presets[0]));
    if(!presets)
        ERROR_GOTO(error, "Error allocating presets\n");

    for(i = 0;i < sfont.phdr_size-1;i++)
    {
        const Generator *gen, *gen_end;
        const Modulator *mod, *mod_end;
        const Zone *zone, *zone_end;
        ALfontsound **sounds = NULL;
        ALsizei sounds_size = 0;
        GenModList gzone;

        if(sfont.phdr[i+1].mZoneIdx == sfont.phdr[i].mZoneIdx)
            continue;

        GenModList_Construct(&gzone);
        zone = sfont.pbag + sfont.phdr[i].mZoneIdx;
        zone_end = sfont.pbag + sfont.phdr[i+1].mZoneIdx;
        if(zone_end-zone > 1)
        {
            gen = sfont.pgen + zone->mGenIdx;
            gen_end = sfont.pgen + (zone+1)->mGenIdx;

            // If no generators, or last generator is not an instrument, this is a global zone
            for(;gen != gen_end;gen++)
            {
                if(gen->mGenerator == 41)
                    break;
            }

            if(gen == gen_end)
            {
                gen = sfont.pgen + zone->mGenIdx;
                gen_end = sfont.pgen + (zone+1)->mGenIdx;
                for(;gen != gen_end;gen++)
                    GenModList_insertGen(&gzone, gen, AL_TRUE);

                mod = sfont.pmod + zone->mModIdx;
                mod_end = sfont.pmod + (zone+1)->mModIdx;
                for(;mod != mod_end;mod++)
                    GenModList_insertMod(&gzone, mod);

                zone++;
            }
        }

        for(;zone != zone_end;zone++)
        {
            GenModList lzone = GenModList_clone(&gzone);

            mod = sfont.pmod + zone->mModIdx;
            mod_end = sfont.pmod + (zone+1)->mModIdx;
            for(;mod != mod_end;mod++)
                GenModList_insertMod(&lzone, mod);

            gen = sfont.pgen + zone->mGenIdx;
            gen_end = sfont.pgen + (zone+1)->mGenIdx;
            for(;gen != gen_end;gen++)
            {
                if(gen->mGenerator == 41)
                {
                    if(gen->mAmount >= sfont.inst_size-1)
                        ERR("Generator %ld has invalid instrument ID generator (%d of %d)\n",
                            (long)(gen-sfont.pgen), gen->mAmount, sfont.inst_size-1);
                    else
                        processInstrument(&sounds, &sounds_size, context,
                                          &sfont.inst[gen->mAmount], &sfont, &lzone);
                    break;
                }
                GenModList_insertGen(&lzone, gen, AL_TRUE);
            }
            GenModList_Destruct(&lzone);
        }

        if(sounds_size > 0)
        {
            ALsizei j;

            presets[presets_size] = NewPreset(context);
            presets[presets_size]->Preset = sfont.phdr[i].mPreset;
            presets[presets_size]->Bank = sfont.phdr[i].mBank;

            for(j = 0;j < sounds_size;j++)
                IncrementRef(&sounds[j]->ref);
            sounds = ExchangePtr((XchgPtr*)&presets[presets_size]->Sounds, sounds);
            ExchangeInt(&presets[presets_size]->NumSounds, sounds_size);
            presets_size++;
        }
        free(sounds);

        GenModList_Destruct(&gzone);
    }

    for(i = 0;i < presets_size;i++)
        IncrementRef(&presets[i]->ref);
    presets = ExchangePtr((XchgPtr*)&soundfont->Presets, presets);
    ExchangeInt(&soundfont->NumPresets, presets_size);

    free(presets);

    Soundfont_Destruct(&sfont);

    return AL_TRUE;

error:
    if(presets)
    {
        ALCdevice *device = context->Device;
        for(i = 0;i < presets_size;i++)
            DeletePreset(presets[i], device);
        free(presets);
    }

    Soundfont_Destruct(&sfont);

    return AL_FALSE;
}
