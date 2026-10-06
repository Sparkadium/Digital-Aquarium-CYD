#pragma once
#if defined(__GNUC__)
#define GT_INLINE inline __attribute__((always_inline))
#define GT_HOT __attribute__((optimize("O3")))
#else
#define GT_INLINE inline
#define GT_HOT
#endif

#if defined(__GNUC__)
typedef uint32_t PixelPair __attribute__((__may_alias__));
#else
typedef uint32_t PixelPair;
#endif
static GT_INLINE void storePair(uint16_t *out,uint32_t value) {
#if defined(__GNUC__)
  *reinterpret_cast<PixelPair*>(out)=value;
#else
  memcpy(out,&value,sizeof(value));
#endif
}

// Sixteen small independent DMA allocations avoid the original ESP32's static-data and
// largest-contiguous-block limits. Every scanline lies wholly within one bank.
static constexpr int FRAME_BANKS = 16, BANK_PIXELS = 4800;
static uint16_t *fbBanks[FRAME_BANKS] = {};
static uint16_t *fbRows[MAX_SIDE] = {};
static inline uint16_t *frameRow(int y) { return fbRows[y]; }
static inline uint16_t &framePixel(int i) { return fbBanks[i / BANK_PIXELS][i % BANK_PIXELS]; }
static void configureFrameRows() {
  for (int y=0;y<H;y++) { int i=y*W; fbRows[y]=fbBanks[i/BANK_PIXELS] ? fbBanks[i/BANK_PIXELS]+i%BANK_PIXELS : nullptr; }
}
static bool allocateFrame() {
  for (int i=0;i<FRAME_BANKS;i++) if (!fbBanks[i]) {
#ifdef ARDUINO_ARCH_ESP32
    fbBanks[i]=(uint16_t*)heap_caps_malloc(BANK_PIXELS*2,MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA|MALLOC_CAP_8BIT);
#else
    fbBanks[i]=(uint16_t*)malloc(BANK_PIXELS*2);
#endif
    if (!fbBanks[i]) { for(auto &p:fbBanks){free(p);p=nullptr;} return false; }
  }
  configureFrameRows(); return true;
}
// Lossless row coding chooses 4-pixel-pattern RLE or an exact local palette.
// Colors are retained verbatim: no quantization, resizing or image assets.
// Cache storage may be IRAM: ONLY aligned uint32_t accesses are allowed there.
class BackdropCache {
public:
  // Cache allocations must fit the small holes left by framebuffer/DMA/library
  // allocations. Total free heap is not a contiguous-allocation guarantee.
  static constexpr int BANK_BYTES=2048, BANK_WORDS=BANK_BYTES/4, MAX_BANKS=64;
  bool valid=false, fitFailed=false;
  uint32_t usedWords=0, capacityWords=0, peakWords=0, fullBakes=0;
  uint16_t iramBanks=0, dramBanks=0;
  uint16_t rowOffset[MAX_SIDE]={}, rowSize[MAX_SIDE]={}, rowCapacity[MAX_SIDE]={};
  void begin() {
    if(capacityWords)return;
#ifdef CYD_CACHE_KB
    const int cacheKiB=CYD_CACHE_KB;
#else
    const int cacheKiB=80;
#endif
    static_assert(cacheKiB>=2&&cacheKiB<=128&&cacheKiB%2==0,"CYD cache must be an even number of KiB, 2..128");
    const int count=cacheKiB*1024/BANK_BYTES;
    for(int i=0;i<count;i++) {
#ifdef ARDUINO_ARCH_ESP32
      wordBanks[i]=(volatile uint32_t*)heap_caps_malloc(BANK_BYTES,MALLOC_CAP_INTERNAL|MALLOC_CAP_EXEC|MALLOC_CAP_32BIT);
      if(wordBanks[i])iramBanks++;
      // Leave 16 KiB of byte-addressable internal RAM, plus allocator overhead.
      else if(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)>=16384+BANK_BYTES+64){
        wordBanks[i]=(volatile uint32_t*)heap_caps_malloc(BANK_BYTES,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT|MALLOC_CAP_32BIT);
        if(wordBanks[i])dramBanks++;
      }
#else
      wordBanks[i]=(volatile uint32_t*)malloc(BANK_BYTES);
      if(wordBanks[i])dramBanks++;
#endif
      if(!wordBanks[i])break;capacityWords+=BANK_WORDS;
    }
  }
  void invalidate(){valid=false;fitFailed=false;}
  void release(){for(auto&p:wordBanks){free((void*)p);p=nullptr;}capacityWords=usedWords=0;iramBanks=dramBanks=0;invalidate();}
  static uint32_t pair(const uint16_t *p){return uint32_t(p[0])|(uint32_t(p[1])<<16);}
  static bool same(const uint16_t *p,const uint16_t *q){return pair(p)==pair(q)&&pair(p+2)==pair(q+2);}
  static int encodeRle(const uint16_t *row,int width,uint32_t *out) {
    int at=0,n=0,blocks=width/4;
    while(at<blocks) {
      int run=1;while(at+run<blocks&&same(row+at*4,row+(at+run)*4))run++;
      if(run>=2){out[n++]=0x80000000u|run;out[n++]=pair(row+at*4);out[n++]=pair(row+at*4+2);at+=run;}
      else{
        int start=at++;
        while(at<blocks){if(at+1<blocks&&same(row+at*4,row+(at+1)*4))break;at++;}
        int count=at-start;out[n++]=count;
        for(int i=0;i<count;i++){out[n++]=pair(row+(start+i)*4);out[n++]=pair(row+(start+i)*4+2);}
      }
    }
    return n;
  }
  static int encodeWhole(const uint16_t *row,int width,uint32_t *out) {
    int best=encodeRle(row,width,out);
    uint16_t palette[256];uint8_t indices[MAX_SIDE];int count=0;
    for(int x=0;x<width;x++){
      int i=0;while(i<count&&palette[i]!=row[x])i++;
      if(i==count){if(count==256)return best;palette[count++]=row[x];}
      indices[x]=(uint8_t)i;
    }
    int bits=1;while((1<<bits)<count)bits++;
    int paletteWords=(count+1)/2,dataWords=(width*bits+31)/32;
    int n=1+paletteWords+dataWords;if(n>=best)return best;
    out[0]=0x40000000u|(uint32_t(bits)<<16)|count;
    for(int i=0;i<paletteWords;i++)out[1+i]=uint32_t(palette[i*2])|((i*2+1<count?uint32_t(palette[i*2+1]):0)<<16);
    uint32_t *data=out+1+paletteWords;for(int i=0;i<dataWords;i++)data[i]=0;
    for(int x=0;x<width;x++){
      int bit=x*bits,word=bit>>5,shift=bit&31;
      data[word]|=uint32_t(indices[x])<<shift;
      if(shift+bits>32)data[word+1]|=uint32_t(indices[x])>>(32-shift);
    }
    return n;
  }
  static int encode(const uint16_t *row,int width,uint32_t *out) {
    int best=encodeWhole(row,width,out);
    for(int block: {32,64}) {
      uint32_t tmp[MAX_SIDE*3/4];int n=1;tmp[0]=0x20000000u|block;
      for(int start=0;start<width;start+=block){
        int length=width-start;if(length>block)length=block;
        uint16_t palette[64];uint8_t indices[64];int count=0;
        for(int x=0;x<length;x++){
          int i=0;while(i<count&&palette[i]!=row[start+x])i++;
          if(i==count)palette[count++]=row[start+x];indices[x]=(uint8_t)i;
        }
        int bits=1;while((1<<bits)<count)bits++;
        tmp[n++]=(uint32_t(bits)<<16)|count;
        for(int i=0;i<(count+1)/2;i++)tmp[n++]=uint32_t(palette[i*2])|((i*2+1<count?uint32_t(palette[i*2+1]):0)<<16);
        int dataWords=(length*bits+31)/32;uint32_t *data=tmp+n;
        for(int i=0;i<dataWords;i++)data[i]=0;
        for(int x=0;x<length;x++){int bit=x*bits,shift=bit&31;data[bit>>5]|=uint32_t(indices[x])<<shift;if(shift+bits>32)data[(bit>>5)+1]|=uint32_t(indices[x])>>(32-shift);}
        n+=dataWords;
      }
      if(n<best){for(int i=0;i<n;i++)out[i]=tmp[i];best=n;}
    }
    return best;
  }
  // Full-image rebuild only on mode/rotation/light changes or slot growth.
  bool capture() {
    valid=false; fitFailed=false; fullBakes++;
    if(!capacityWords){fitFailed=true;usedWords=0;return false;}
    for(int slack=1;slack>=0;slack--){
      uint32_t pos=0;bool fits=true;
      for(int y=0;y<H;y++){
        uint32_t tmp[MAX_SIDE/2+1];int n=encode(frameRow(y),W,tmp);
        uint32_t cap=n+(slack?(n/4+4):0);
        if(pos+cap>capacityWords){fits=false;break;}
        rowOffset[y]=(uint16_t)pos;rowSize[y]=(uint16_t)n;rowCapacity[y]=(uint16_t)cap;
        for(int i=0;i<n;i++)cacheWord(pos+i)=tmp[i];pos+=cap;
        if((y&15)==15)GT_BACKGROUND_YIELD();
      }
      if(fits){usedWords=pos;if(pos>peakWords)peakWords=pos;valid=true;return true;}
    }
    usedWords=0;fitFailed=true;return false;
  }
  bool updateRow(int y,const uint16_t *row) {
    if(!valid)return false;
    uint32_t tmp[MAX_SIDE/2+1];int n=encode(row,W,tmp);
    if(n>rowCapacity[y])return false;
    for(int i=0;i<n;i++)cacheWord(rowOffset[y]+i)=tmp[i];rowSize[y]=(uint16_t)n;return true;
  }
  GT_HOT void decodeRow(int y,uint16_t *out) const {
    // Read each encoded word once. The cache can live in instruction RAM,
    // which requires aligned 32-bit volatile access; the local row is DRAM.
    // This removes bank lookup, call and memory-barrier costs per pixel.
    uint32_t encoded[MAX_SIDE/2+1];
    uint32_t source=rowOffset[y],remaining=rowSize[y];
    uint32_t *dest=encoded;
    while(remaining){
      uint32_t n=BANK_WORDS-(source&(BANK_WORDS-1));if(n>remaining)n=remaining;
      const volatile uint32_t *p=&cacheWord(source);
      for(uint32_t i=0;i<n;i++)*dest++=*p++;
      source+=n;remaining-=n;
    }
    const uint32_t *at=encoded,*end=at+rowSize[y];
    uint32_t head=*at;
    if(head&0x20000000u){
      int block=head&0xffff;at++;
      for(int start=0;start<W;start+=block){
        int length=W-start;if(length>block)length=block;
        uint32_t token=*at++;int count=token&0xffff,bits=(token>>16)&15;
        const uint32_t *data=at+(count+1)/2;
        decodePalette(at,data,count,bits,length,out+start);
        at=data+(length*bits+31)/32;
      }
      return;
    }
    if(head&0x40000000u){
      int count=head&0xffff,bits=(head>>16)&15;
      const uint32_t *pal=at+1,*data=pal+(count+1)/2;
      decodePalette(pal,data,count,bits,W,out);
      return;
    }
    while(at<end){
      uint32_t token=*at++;int count=token&0x7fffffff;
      if(token&0x80000000u){uint32_t a=*at++,b=*at++;for(int i=0;i<count;i++){storePair(out,a);storePair(out+2,b);out+=4;}}
      else {memcpy(out,at,count*8);out+=count*4;at+=count*2;}
    }
  }
private:
  template<int Bits> static GT_HOT void unpackPalette(const uint16_t *pal,const uint32_t *data,int length,uint16_t *out) {
    const uint32_t mask=(1u<<Bits)-1;
    if(32%Bits==0){
      // Two pixels per aligned store; constant shifts/masks are folded by GCC.
      const int perWord=32/Bits;
      for(int x=0;x<length;x+=perWord){
        uint32_t word=*data++;int n=length-x;if(n>perWord)n=perWord;
        for(int i=0;i<n;i+=2){
          uint32_t a=pal[word&mask];word>>=Bits;
          uint32_t b=pal[word&mask];word>>=Bits;
          storePair(out+x+i,a|(b<<16));
        }
      }
    }else{
      // 3/5/6/7-bit indices can straddle packed-word boundaries.
      uint32_t word=*data++;int available=32;
      for(int x=0;x<length;x++){
        uint32_t ix=word;
        if(available<Bits){word=*data++;ix|=word<<available;word>>=Bits-available;available+=32-Bits;}
        else{word>>=Bits;available-=Bits;}
        out[x]=pal[ix&mask];
      }
    }
  }
  static GT_HOT void decodePalette(const uint32_t *packed,const uint32_t *data,int count,int bits,int length,uint16_t *out) {
    uint16_t palette[256];
    for(int i=0;i<count;i+=2){uint32_t p=*packed++;palette[i]=(uint16_t)p;palette[i+1]=(uint16_t)(p>>16);}
    switch(bits){
      case 1:unpackPalette<1>(palette,data,length,out);break;
      case 2:unpackPalette<2>(palette,data,length,out);break;
      case 3:unpackPalette<3>(palette,data,length,out);break;
      case 4:unpackPalette<4>(palette,data,length,out);break;
      case 5:unpackPalette<5>(palette,data,length,out);break;
      case 6:unpackPalette<6>(palette,data,length,out);break;
      case 7:unpackPalette<7>(palette,data,length,out);break;
      case 8:unpackPalette<8>(palette,data,length,out);break;
    }
  }
  volatile uint32_t *wordBanks[MAX_BANKS]={};
  GT_INLINE volatile uint32_t &cacheWord(uint32_t i) const {return wordBanks[i>>9][i&511];}
};
static BackdropCache backdrop;
static void freeGraphics(){backdrop.release();for(auto&p:fbBanks){free(p);p=nullptr;}configureFrameRows();}
