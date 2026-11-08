#include <assert.h>
#include <stdio.h>
#include <map>
#include <string>
#include <vector>
#include "Preferences.h"
#include "esp_partition.h"
std::map<std::string,std::vector<unsigned char> > fakeNvs;
std::string failNvsKey;
esp_partition_t fakeParts[2] = {{0x380000,0},{0x380000,1}};
std::vector<uint8_t> fakeFlash[2] = {std::vector<uint8_t>(0x380000,0xff),std::vector<uint8_t>(0x380000,0xff)};
unsigned erasedSlot=99;
// Include the actual production implementation unchanged. This lets this host
// test invalidate its internal RAM cache to simulate a fresh boot without any
// test hooks or host conditionals in production firmware.
#include "../../src/screen_asset.cpp"
extern "C" int app_log_log(app_log_level_t,const char *,const char *,...) { return 0; }
using namespace exlink;
static void word(std::vector<uint8_t>&d,size_t p,uint16_t v) { d[p]=v;d[p+1]=v>>8; }
static void dword(std::vector<uint8_t>&d,size_t p,uint32_t v) { for(unsigned i=0;i<4;++i)d[p+i]=v>>(i*8); }
static std::vector<uint8_t> asset(uint16_t color,uint32_t duration) {
    std::vector<uint8_t>d(44+1920,0);memcpy(d.data(),"EXA1",4);
    word(d,4,1);word(d,6,32);word(d,8,480);word(d,10,480);word(d,12,1);word(d,14,1);
    dword(d,16,d.size());dword(d,20,duration);dword(d,24,32);
    dword(d,32,44);dword(d,36,1920);dword(d,40,duration);
    for(unsigned y=0;y<480;++y){word(d,44+y*4,480);word(d,46+y*4,color);}return d;
}
static void digest(const std::vector<uint8_t>&d,uint8_t output[32]) {
    mbedtls_sha256_context ctx;mbedtls_sha256_init(&ctx);
    mbedtls_sha256_update_ret(&ctx,d.data(),d.size());mbedtls_sha256_finish_ret(&ctx,output);
}
static UpdateResult upload(const std::vector<uint8_t>&d,bool wrong=false) {
    UpdateManifest manifest{};manifest.kind=UpdateKind::ScreenAsset;manifest.size=d.size();digest(d,manifest.sha256);
    if(wrong)manifest.sha256[0]^=1;
    UpdateSink*s=screenAssetUpdateSink();assert(s->begin(manifest));
    assert(s->write(d.data(),17));assert(s->write(d.data()+17,d.size()-17));
    return s->finish(manifest.sha256);
}
struct Draw {uint16_t *buffer;uint16_t color;unsigned rows;};
static bool draw(uint16_t y,const uint16_t *row,size_t width,void *context) {
    Draw*d=static_cast<Draw*>(context);assert(y==d->rows && width==480);
    for(size_t x=0;x<width;++x){assert(row[x]==d->color);d->buffer[(y%4)*480+x]=row[x];}
    ++d->rows;return true;
}
int main() {
    assert(!screenAssetAvailable());
    const auto a=asset(0x1234,100),b=asset(0x5678,200),c=asset(0x9999,300);
    assert(upload(a)==UpdateResult::Ok && erasedSlot==0);
    assert(!screenAssetAvailable());assert(fakeNvs["active"].empty()); // FINISH is not ACTIVATE.
    assert(screenAssetUpdateSink()->activate());assert(screenAssetAvailable());
    assert(screenAssetDurationMs()==100);
    const auto savedA=fakeFlash[0];
    assert(upload(b)==UpdateResult::Ok && erasedSlot==1);
    assert(fakeFlash[0]==savedA && screenAssetDurationMs()==100);
    assert(screenAssetUpdateSink()->activate());assert(screenAssetDurationMs()==200);
    // Wrong stream hash never becomes ready or changes active resource.
    assert(upload(c,true)==UpdateResult::HashMismatch);assert(!screenAssetUpdateSink()->activate());
    assert(screenAssetDurationMs()==200);
    auto invalid=c;invalid[0]='!';assert(upload(invalid)==UpdateResult::InvalidManifest);
    assert(!screenAssetUpdateSink()->activate() && screenAssetDurationMs()==200);
    // Previous or active NVS commit failures retain old selected and persisted active.
    const auto activeB=fakeNvs["active"];
    assert(upload(c)==UpdateResult::Ok);failNvsKey="previous";
    assert(!screenAssetUpdateSink()->activate() && fakeNvs["active"]==activeB);
    assert(screenAssetDurationMs()==200);failNvsKey="active";
    assert(!screenAssetUpdateSink()->activate() && fakeNvs["active"]==activeB);
    invalidateCache();assert(screenAssetDurationMs()==200);failNvsKey.clear();
    assert(screenAssetUpdateSink()->activate());assert(screenAssetDurationMs()==300);
    // Simulate reboot with corrupted newly active A: fall back to previous B.
    fakeFlash[0][44]^=1;invalidateCache();assert(screenAssetDurationMs()==200);
    uint16_t guard[4*480+2];for(auto &x:guard)x=0xbeef;
    uint16_t scratch[482];scratch[0]=scratch[481]=0xcafe;
    Draw state{guard+1,0x5678,0};
    assert(screenAssetDecodeFrameRows(0,scratch+1,480,draw,&state));assert(state.rows==480);
    assert(guard[0]==0xbeef && guard[4*480+1]==0xbeef && scratch[0]==0xcafe && scratch[481]==0xcafe);
    assert(!screenAssetDecodeFrameRows(0,scratch+1,479,draw,&state));
    // Explicit restore survives reboot and does not accidentally select previous.
    assert(screenAssetRestoreDefault());assert(!screenAssetAvailable());
    invalidateCache();assert(!screenAssetAvailable() && screenAssetDurationMs()==0);
    const auto preservedB=fakeFlash[1];assert(upload(a)==UpdateResult::Ok && erasedSlot==0);
    assert(fakeFlash[1]==preservedB && !screenAssetAvailable());
    screenAssetUpdateSink()->abort();assert(!screenAssetUpdateSink()->activate());
    puts("screen_asset transaction host tests: OK (digest is a NON-SHA256 stub)");
}
