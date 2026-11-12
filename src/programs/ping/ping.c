#include "../../libc/include/purec.h"

#define PING_ARG_CAPACITY 256

static bool space(char character){
    return character==' ' || character=='\t';
}

static const char *skip_spaces(const char *text){
    while(space(*text)) text++;
    return text;
}

static void write_ipv4(uint32_t address){
    pc_write_u64((address>>24)&255);
    pc_write(".");
    pc_write_u64((address>>16)&255);
    pc_write(".");
    pc_write_u64((address>>8)&255);
    pc_write(".");
    pc_write_u64(address&255);
}

static bool parse_count(const char *text, uint32_t length, uint32_t *count){
    if(!length) return false;
    uint32_t value=0;
    for(uint32_t index=0;index<length;index++){
        if(text[index]<'0' || text[index]>'9') return false;
        value=value*10U+(uint32_t)(text[index]-'0');
        if(value>100) return false;
    }
    if(!value) return false;
    *count=value;
    return true;
}

static void ping_error(int32_t status){
    if(status==-2) pc_write("ping: no active network interface\n");
    else if(status==-3) pc_write("ping: network is not configured; waiting for DHCP\n");
    else if(status==-4) pc_write("ping: cannot resolve host name\n");
    else if(status==-5) pc_write("Request timeout\n");
    else if(status==-6) pc_write("ping: another network query is active\n");
    else if(status==-7) pc_write("ping: network transmission failed\n");
    else pc_write("ping: invalid target or arguments\n");
}

static int ping_main(void){
    char arguments[PING_ARG_CAPACITY];
    if(pc_get_command_line(arguments,sizeof(arguments))<0) arguments[0]='\0';
    const char *cursor=skip_spaces(arguments);
    if(pc_strcmp(cursor,"--help")==0 || pc_strcmp(cursor,"-h")==0){
        pc_write("usage: ping [-c count] <ip|host|url>\n");
        pc_write("Send 4 ICMP echo requests by default; count range is 1..100.\n");
        return 0;
    }
    uint32_t count=4;
    if(cursor[0]=='-' && cursor[1]=='c' && space(cursor[2])){
        cursor=skip_spaces(cursor+2);
        const char *end=cursor;
        while(*end && !space(*end)) end++;
        if(!parse_count(cursor,(uint32_t)(end-cursor),&count)){
            pc_write("ping: count must be between 1 and 100\n");
            return 2;
        }
        cursor=skip_spaces(end);
    }
    if(!*cursor){
        pc_write("ping: target required; use ping --help\n");
        return 2;
    }
    char target[NETWORK_PING_TARGET_CAPACITY];
    uint32_t length=0;
    while(cursor[length] && !space(cursor[length])){
        if(length+1>=sizeof(target)){
            pc_write("ping: target is too long\n");
            return 2;
        }
        target[length]=cursor[length];
        length++;
    }
    target[length]='\0';
    if(*skip_spaces(cursor+length)){
        pc_write("ping: unexpected extra argument\n");
        return 2;
    }
    bool heading=false;
    uint32_t received=0;
    uint32_t min_ms=0xffffffffu, max_ms=0;
    uint64_t sum_ms=0;
    for(uint32_t sequence=1;sequence<=count;sequence++){
        struct network_ping_result result={0};
        int32_t status=pc_ping(target,(uint16_t)sequence,2000,&result);
        if(status==0){
            if(!heading){
                pc_write("PING ");
                pc_write(target);
                pc_write(" (");
                write_ipv4(result.address);
                pc_write("): 56 data bytes\n");
                heading=true;
            }
            pc_write("64 bytes from ");
            write_ipv4(result.address);
            pc_write(": icmp_seq=");
            pc_write_u64(result.sequence);
            pc_write(" ttl=");
            pc_write_u64(result.ttl);
            pc_write(" time=");
            pc_write_u64(result.round_trip_ms);
            pc_write(" ms\n");
            received++;
            sum_ms+=result.round_trip_ms;
            if(result.round_trip_ms<min_ms) min_ms=result.round_trip_ms;
            if(result.round_trip_ms>max_ms) max_ms=result.round_trip_ms;
        } else {
            ping_error(status);
            if(status!=-5) return 1;
        }
        if(sequence<count) pc_sleep(1000);
    }
    pc_write("\n--- ");
    pc_write(target);
    pc_write(" ping statistics ---\n");
    pc_write_u64(count);
    pc_write(" packets transmitted, ");
    pc_write_u64(received);
    pc_write(" received, ");
    pc_write_u64((uint64_t)(count-received)*100U/count);
    pc_write("% packet loss\n");
    if(received){
        pc_write("rtt min/avg/max = ");
        pc_write_u64(min_ms);
        pc_write("/");
        pc_write_u64(sum_ms/received);
        pc_write("/");
        pc_write_u64(max_ms);
        pc_write(" ms\n");
    }
    return received?0:1;
}

void _start(void){
    pc_exit(ping_main());
}
