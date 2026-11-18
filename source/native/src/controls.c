/* Included by collector.c for Bootmod3 and xDrive controls. */
#ifdef BM3_ONLY
#include "bm3_profiles.h"

static int bm3_profile(control_link fd, unsigned char *reply) {
    const unsigned char define[]={0x2c,2,0xf3,1,0x24,0x80,0x7f,0xfe,0x50,0,0xff};
    const unsigned char defined[]={0x6c,2,0xf3,1}, read[]={0x22,0xf3,1}, prefix[]={0x62,0xf3,1};
    unsigned negative, i;
    int matched=-1, n;
    if (exchange(fd,0x12,define,sizeof define,defined,sizeof defined,reply,512,1600,&negative)<4) return -1;
    n=exchange(fd,0x12,read,sizeof read,prefix,sizeof prefix,reply,512,1600,&negative);
    if (n!=258) return -1;
    {
        int marker=0, j;
        for (j=3;j+5<=n;++j) if (!memcmp(reply+j,"BM3_R",5)) marker=1;
        if (!marker) return -1;
    }
    for (i=0;i<sizeof profiles/sizeof profiles[0];++i) {
        const size_t size=strlen(profiles[i].rom);
        unsigned j;
        for (j=3;j+size+1<=(unsigned)n;++j) {
            if (reply[j]=='#' && !memcmp(reply+j+1,profiles[i].rom,size) &&
                (j+size+1==(unsigned)n || !isalnum(reply[j+size+1]))) {
                if (matched>=0 && matched!=(int)i) return -1;
                matched=(int)i;
            }
        }
    }
    return matched;
}

static int write_memory(control_link fd, unsigned char sid, const unsigned char *address,
                       const unsigned char *bytes, unsigned size) {
    unsigned char req[31]={sid,0x14,address[0],address[1],address[2],address[3],(unsigned char)size};
    const unsigned char ack[]={ (unsigned char)(sid+0x40),0x3d };
    unsigned char reply[64];
    unsigned negative;
    memcpy(req+7,bytes,size);
    return exchange(fd,0x12,req,7+size,ack,sid==0x3e?2:1,reply,sizeof reply,1600,&negative)>0;
}

static int verify_memory(control_link fd, const unsigned char *address, const unsigned char *bytes, unsigned size) {
    const unsigned char req[]={0x23,0x14,address[0],address[1],address[2],address[3],(unsigned char)size};
    const unsigned char ack[]={0x63};
    unsigned char reply[32];
    unsigned negative;
    return exchange(fd,0x12,req,sizeof req,ack,sizeof ack,reply,sizeof reply,1600,&negative)==(int)size+1
           && !memcmp(reply+1,bytes,size);
}

static void control(control_link fd, const char *mode, unsigned value) {
    unsigned char reply[512];
    unsigned negative;
    int profile=bm3_profile(fd,reply), n;
    if (profile<0) { puts("Bootmod3 CustomROM could not be verified."); return; }
    if (!strcmp(mode,"--map") && value>=1 && value<=4) {
        const unsigned char *a=profiles[profile].map;
        const unsigned char sid=profiles[profile].sid;
        const unsigned char command[]={ (unsigned char)(value-1),1 };
        if (!write_memory(fd,sid,a,command,sizeof command)) { puts("Map write could not be confirmed."); return; }
        if (verify_memory(fd,a,command,1)) printf("Map %u verified by DME readback.\n",value);
        else puts("Map readback did not confirm the request.");
    } else if (!strcmp(mode,"--burbles") && value<=3) {
        const unsigned char sid=profiles[profile].sid;
        const unsigned char *enable=profiles[profile].enable, *timing=profiles[profile].timing;
        const unsigned factor=profiles[profile].factor;
        const unsigned char enabled=value!=0;
        unsigned char table[24];
        if ((!enable[0] && !enable[1] && !enable[2] && !enable[3]) ||
            (!timing[0] && !timing[1] && !timing[2] && !timing[3]) || !factor || 12*factor>127) {
            puts("Burble preset unavailable for this ROM.");
            return;
        }
        memset(table,(unsigned char)(0u-value*4*factor),sizeof table);
        if ((!value && !write_memory(fd,sid,enable,&enabled,1)) ||
            !write_memory(fd,sid,timing,table,sizeof table) ||
            (value && !write_memory(fd,sid,enable,&enabled,1)) ||
            !verify_memory(fd,timing,table,sizeof table) || !verify_memory(fd,enable,&enabled,1)) {
            puts("Burble request incomplete or unconfirmed.");
            return;
        }
        if (value) printf("Burble preset %u verified by DME readback.\n",value*4);
        else puts("Burbles off, verified by DME readback.");
    } else if (!strcmp(mode,"--flap") && value<=2) {
        const unsigned char req[]={0x2f,0x60,0xfd,value?3:0,value==1?0x14:0x23,value==1?0xfe:0x28};
        const unsigned char ack[]={0x6f,0x60,0xfd,req[3]};
        n=exchange(fd,0x12,req,value?6:4,ack,sizeof ack,reply,sizeof reply,1600,&negative);
        if (n>=4) puts("Exhaust flap request accepted by DME.");
        else puts("Exhaust flap request could not be confirmed.");
    } else if (!strcmp(mode,"--sound") && value<=1) {
        const unsigned char req[]={0x2e,0x5f,0xfd,value?0:1}, ack[]={0x6e,0x5f,0xfd};
        n=exchange(fd,0x12,req,sizeof req,ack,sizeof ack,reply,sizeof reply,1600,&negative);
        if (n>=3) puts("Active Sound request accepted by DME.");
        else puts("Active Sound request could not be confirmed.");
    }
}
#else
static int read_mode(int fd) {
    const unsigned char req[]={0x22,0xdb,0x32}, ack[]={0x62,0xdb,0x32};
    unsigned char reply[64];
    unsigned negative;
    const int n=exchange(fd,0x19,req,sizeof req,ack,sizeof ack,reply,sizeof reply,800,&negative);
    if (n<11) return -1;
    if ((reply[10]&0xc0)==0xc0) return 0;
    if (reply[10]==0x20 || reply[10]==0x21) return 1;
    return -1;
}

static int parked(int fd) {
    const unsigned char speed[]={1,0x0d}, speed_ack[]={0x41,0x0d};
    const unsigned char gear[]={0x22,0xda,0x2e}, gear_ack[]={0x62,0xda,0x2e};
    const unsigned char rpm[]={1,0x0c}, rpm_ack[]={0x41,0x0c};
    unsigned char reply[8];
    unsigned negative;
    // Same Park read as the 340i parked probe. Missing or different replies block the write.
    return exchange(fd,0x18,gear,3,gear_ack,3,reply,sizeof reply,350,&negative)==5 && reply[3]==0 &&
           exchange(fd,0x12,rpm,2,rpm_ack,2,reply,sizeof reply,350,&negative)==4 && !(reply[2]|reply[3]) &&
           exchange(fd,0x12,speed,2,speed_ack,2,reply,sizeof reply,350,&negative)==3 && !reply[2];
}

static void control(int fd, const char *mode, unsigned value) {
    const unsigned char id[]={0x22,0xf1,0x50}, id_ack[]={0x62,0xf1,0x50};
    const unsigned char capability[]={0x22,0xdb,0xe1}, cap_ack[]={0x62,0xdb,0xe1};
    unsigned char reply[64];
    unsigned negative;
    int n=exchange(fd,0x29,id,3,id_ack,3,reply,sizeof reply,800,&negative);
    unsigned index=0, i;
    int current;
    if (n>=6 && n<=10) {
        for (i=3;i<(unsigned)n-3;++i) if (reply[i]) break;
        if (i==(unsigned)n-3) index=(reply[i]<<16)|(reply[i+1]<<8)|reply[i+2];
    }
    if (n == -2) { printf("DSC identification refused (NRC %02X).\n",negative); return; }
    if (n < 0) { puts("No DSC reply at 0x29. Check the diagnostic gateway."); return; }
    if (!index) { puts("DSC replied with an unrecognized identification format."); return; }
    if (index!=0x0f2510 && index!=0x0f2850) {
        printf("DSC %06X is outside the supported control list.\n",index);
        return;
    }
    n=exchange(fd,0x29,capability,3,cap_ack,3,reply,sizeof reply,800,&negative);
    if (n<4) {
        if (n==-2) printf("DSC mode read refused (NRC %02X).\n",negative);
        else puts("DSC mode read unavailable.");
        return;
    }
    current=read_mode(fd);
    if (!strcmp(mode,"--status")) {
        puts(current<0?"Transfer case mode unavailable.":current?"Transfer case reports AWD.":"Transfer case reports RWD.");
        return;
    }
    if (value && index!=0x0f2850) { puts("Cycle the ignition to restore AWD on this DSC."); return; }
    if (current<0) { puts("Transfer case mode unavailable."); return; }
    if (!parked(fd)) { puts("Needs ignition on, engine off, stationary, and confirmed Park."); return; }
    {
        const unsigned char request[]={0x2e,0xdb,0xe1,(unsigned char)value}, ack[]={0x6e,0xdb,0xe1};
        n=exchange(fd,0x29,request,4,ack,3,reply,sizeof reply,2400,&negative);
        if (n>=3) {
            current=read_mode(fd);
            if (current==(int)value) puts(value?"Transfer case confirms AWD.":"Transfer case confirms RWD.");
            else puts("DSC accepted. Transfer case mode is unconfirmed.");
        } else if (n==-2) printf("DSC refused this request (NRC %02X).\n",negative);
        else puts("DSC request could not be confirmed.");
    }
}
#endif

int main(int argc,char **argv) {
    unsigned value, port=6801;
#ifndef BM3_ONLY
    int fd,lock;
#endif
    if ((argc!=5 && argc!=7) || strcmp(argv[1],"--host")) return 2;
    if (argc==7) {
        if (strcmp(argv[5],"--port")) return 2;
        port=number(argv[6],1,65535);
    }
#ifdef BM3_ONLY
    if (strcmp(argv[3],"--map") && strcmp(argv[3],"--flap") && strcmp(argv[3],"--sound") && strcmp(argv[3],"--burbles")) return 2;
#else
    if (strcmp(argv[3],"--status") && strcmp(argv[3],"--drive-mode")) return 2;
#endif
    value=number(argv[4],0,!strcmp(argv[3],"--map") ? 4 : !strcmp(argv[3],"--flap") ? 2 : !strcmp(argv[3],"--burbles") ? 3 : 1);
    if (!strcmp(argv[3],"--map") && !value) return 2;
#ifdef BM3_ONLY
    struct diag_link link;
    struct diag_link_cfg cfg;
    const int burbles = !strcmp(argv[3], "--burbles");
    diag_link_signals();
    memset(&cfg, 0, sizeof cfg);
    cfg.host = argv[2];
    cfg.port = port;
    cfg.transport = burbles ? DIAG_HSFZ : DIAG_AUTO;
    cfg.kind = DIAG_ONESHOT;
    cfg.dsi_name = "bootmod3";
    diag_link_init(&link, &cfg);
    if (!diag_link_reserve_oneshot(&link)) {
        puts("Diagnostics busy. Try again.");
        return 0;
    }
    if (diag_link_up(&link)) control(&link, argv[3], value);
    else if (burbles) puts("Burble presets require the HSFZ gateway.");
    else puts("Diagnostic gateway unavailable.");
    diag_link_close(&link);
#else
    signal(SIGINT,stop); signal(SIGTERM,stop); signal(SIGPIPE,SIG_IGN);
    lock=reserve_gateway();
    if (lock<0) { puts("Diagnostics busy. Try again."); return 0; }
    fd=connect_gw(argv[2],port);
    if (fd>=0) { control(fd,argv[3],value); close(fd); }
    else puts("Diagnostic gateway unavailable.");
    close(lock);
#endif
    return 0;
}
