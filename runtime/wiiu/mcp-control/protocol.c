#include "protocol.h"
#include "monocypher-ed25519.h"
#include <string.h>

static uint64_t field(const uint8_t *data,unsigned length) {
    uint64_t value=0;
    for(unsigned i=0;i<length;++i)value=(value<<8)|data[i];
    return value;
}

bool ctr_control_application(uint64_t title) {
    return (title>>32)==0x00050000u && (uint32_t)title!=0;
}

static bool hex_name(const char *name,size_t length) {
    return strspn(name,"0123456789abcdef")==length;
}

static bool package_name(const char *name) {
    return !strcmp(name,"title.tmd") || !strcmp(name,"title.tik") || !strcmp(name,"title.cert") ||
           (strlen(name)==12 && hex_name(name,8) && !strcmp(name+8,".app"));
}

static bool title_path(const char *name) {
    if(strncmp(name,"code/",5) && strncmp(name,"content/",8) && strncmp(name,"meta/",5))return false;
    const char *component=name;
    for(const char *p=name;;++p) {
        char c=*p;
        if(c=='/' || !c) {
            size_t length=(size_t)(p-component);
            if(!length || (length==1 && *component=='.') || (length==2 && !memcmp(component,"..",2)))return false;
            if(!c)return true;
            component=p+1;
        } else if(!((c>='A' && c<='Z') || (c>='a' && c<='z') || (c>='0' && c<='9') || c=='.' || c=='-' || c=='_'))return false;
    }
}

static bool inventory(CtrInstallCaptureFile *files,unsigned count,const uint8_t *raw,bool installed) {
    uint64_t total=0;
    for(unsigned i=0;i<count;++i,raw+=168) {
        const char *name=(const char *)raw;
        if(!*name || !memchr(name,0,128))return false;
        uint64_t size=field(raw+128,8);
        if(size>UINT64_C(1073741824)-total)return false;
        total+=size;
        if(installed && !title_path(name))return false;
        for(unsigned j=0;j<i;++j)if(!strcmp(files[j].name,name))return false;
        files[i].name=name;files[i].size=size;memcpy(files[i].sha256,raw+136,32);
    }
    return true;
}

bool ctr_control_decode(const uint8_t *packet,size_t size,const uint8_t public_key[32],
                        uint64_t epoch,uint32_t sequence,CtrControlRequest *r) {
    if(!packet || !r || size<112 || size>CTR_CONTROL_PACKET_BYTES)return false;
    const uint8_t *header=packet+64;
    if(memcmp(header,"CTR1",4) || field(header+4,8)!=epoch || field(header+12,4)!=sequence)return false;
    unsigned count=(unsigned)field(header+18,2),installed=(unsigned)field(header+20,2);
    if(count>CTR_CONTROL_FILES || installed>CTR_CONTROL_FILES || size!=112u+168u*(count+installed))return false;
    if(crypto_ed25519_check(packet,public_key,header,size-64)!=0)return false;
    memset(r,0,sizeof(*r));
    r->epoch=epoch;r->sequence=sequence;r->action=header[16];r->target=header[17];
    r->file_count=count;r->installed_count=installed;r->version=(unsigned)field(header+22,2);
    r->title=field(header+24,8);
    static const char hex[]="0123456789abcdef";
    for(unsigned i=0;i<16;++i) {
        r->job[2*i]=hex[header[32+i]>>4];r->job[2*i+1]=hex[header[32+i]&15];
    }
    if(r->target>1 || !inventory(r->files,count,packet+112,false) ||
       !inventory(r->installed,installed,packet+112+168u*count,true))return false;
    switch(r->action) {
        case CTR_CONTROL_REBOOT:
            return !r->title && !count && !installed && !r->target && !r->version;
        case CTR_CONTROL_HOMEBREW: {
            if(r->title || count!=1 || installed || r->target || r->version ||
               r->files[0].size<4 || r->files[0].size>64u*1024u*1024u)return false;
            const char *name=r->files[0].name;
            return !strncmp(name,r->job,32) && (!strcmp(name+32,".rpx") || !strcmp(name+32,".wuhb"));
        }
        case CTR_CONTROL_TITLE:
            return ctr_control_application(r->title) && !count && !installed && !r->target && !r->version;
        case CTR_CONTROL_INSTALL:
            if(!ctr_control_application(r->title) || count<7 || !installed)return false;
            for(unsigned i=0;i<count;++i)if(!package_name(r->files[i].name) || !r->files[i].size)return false;
            return true;
        default:return false;
    }
}
