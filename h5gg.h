//
//  h5gg.h
//  h5gg
//
//  Created by admin on 11/3/2022.
//

/** 强烈提醒: 请不要修改此JS接口, 以保持js脚本的兼容性 */
/** Strong reminder: Please do not modify this JS interface to maintain the compatibility of js scripts */

#ifndef h5gg_h
#define h5gg_h

extern FloatMenu* floatH5;

//导入JavaScriptCore框架头文件
#include <libgen.h>
#include <sys/mount.h>
#import <JavaScriptCore/JavaScriptCore.h>
//导入JJ内存搜索引擎头文件(专为H5GG定制)
#include "MemScan.h"
#include "TopShow.h"

#import <sys/sysctl.h>
#import <mach-o/dyld_images.h>

extern "C" {
#include "dyld64.h"
#include "libproc.h"
#include "proc_info.h"
}

NSArray* getRunningProcess()
{
    //指定名字参数，按照顺序第一个元素指定本请求定向到内核的哪个子系统，第二个及其后元素依次细化指定该系统的某个部分。
    //CTL_KERN，KERN_PROC,KERN_PROC_ALL 正在运行的所有进程
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_ALL ,0};
    
    size_t miblen = 4;
    //值-结果参数：函数被调用时，size指向的值指定该缓冲区的大小；函数返回时，该值给出内核存放在该缓冲区中的数据量
    //如果这个缓冲不够大，函数就返回ENOMEM错误
    size_t size;
    //返回0，成功；返回-1，失败
    int st = sysctl(mib, miblen, NULL, &size, NULL, 0);
    NSLog(@"allproc=%d, %s", st, strerror(errno));
    
    struct kinfo_proc * process = NULL;
    struct kinfo_proc * newprocess = NULL;
    do
    {
        size += size / 10;
        newprocess = (struct kinfo_proc *)realloc(process, size);
        if (!newprocess)
        {
            if (process)
            {
                free(process);
                process = NULL;
            }
            return nil;
        }
        
        process = newprocess;
        st = sysctl(mib, miblen, process, &size, NULL, 0);
        NSLog(@"allproc=%d, %s", st, strerror(errno));
    } while (st == -1 && errno == ENOMEM);
    
    if (st == 0)
    {
        if (size % sizeof(struct kinfo_proc) == 0)
        {
            int nprocess = size / sizeof(struct kinfo_proc);
            if (nprocess)
            {
                NSMutableArray * array = [[NSMutableArray alloc] init];
                for (int i = nprocess - 1; i >= 0; i--)
                {
                    [array addObject:@{
                        @"pid": [NSNumber numberWithInt:process[i].kp_proc.p_pid],
                        @"name": [NSString stringWithUTF8String:process[i].kp_proc.p_comm]
                    }];
                }
                
                free(process);
                process = NULL;
                NSLog(@"allproc=%d, %@", array.count, array);
                return array;
            }
        }
    }
    
    return nil;
}

pid_t pid_for_name(const char* name)
{
    NSArray* allproc = getRunningProcess();
    for(NSDictionary* proc in allproc)
    {
        if([[proc valueForKey:@"name"] isEqualToString:[NSString stringWithUTF8String:name]])
            return [[proc valueForKey:@"pid"] intValue];
    }
    return 0;
}

@protocol h5ggJSExport <JSExport>

JSExportAs(searchNumber, -(void)searchNumber:(NSString*)value param2:(NSString*)type param3:(NSString*)memoryFrom param4:(NSString*)memoryTo);

JSExportAs(searchNearby, -(void)searchNearby:(NSString*)value param2:(NSString*)type param3:(NSString*)range);

JSExportAs(getValue, -(NSString*)getValue:(NSString*)address param2:(NSString*)type);
JSExportAs(setValue, -(BOOL)setValue:(NSString*)address param2:(NSString*)value param3:(NSString*)type);

JSExportAs(editAll, -(int)editAll:(NSString*)value param3:(NSString*)type);

JSExportAs(getResults, -(NSArray*)getResults:(int)maxCount param1:(int)skipCount);

-(long)getResultsCount;
-(void)clearResults;

-(void)setFloatTolerance:(NSString*)value;

-(NSArray*)getLocalScripts;
-(void)pickScriptFile:(JSValue*)callback;

-(NSArray*)getRangesList:(JSValue*)filter;

-(void)make;

-(JSValue*)getProcList:(JSValue*)filter;
-(BOOL)setTargetProc:(pid_t)pid;

@end

@interface h5ggEngine : NSObject <h5ggJSExport>
@property JJMemoryEngine* engine;
@property NSString* lastSearchType;
@property BOOL firstSearchDone;
@property pid_t targetpid;
@property task_port_t targetport;
@end

@implementation h5ggEngine

-(instancetype)init {
    if (self = [super init]) {
        self.firstSearchDone = FALSE;
        self.targetpid = getpid();
        self.targetport = mach_task_self();
        self.engine = new JJMemoryEngine(self.targetport);
    }
    return self;
}

-(JSValue*)getProcList:(JSValue*)filter {
    
    NSArray* allproc = getRunningProcess();
    if(!allproc)
        return [JSValue valueWithNullInContext:[JSContext currentContext]];
    
    NSMutableArray* newarr = [[NSMutableArray alloc] init];
    
    for(NSDictionary* proc in allproc)
    {
        char path[PATH_MAX]={0};
        
        if(!proc_pidpath([[proc valueForKey:@"pid"] intValue], path, sizeof(path)))
            continue;
        
        if(strstr(path, "/private/var/")!=path && strstr(path, "/var/")!=path)
            continue;

//        if(![[NSString stringWithUTF8String:dirname(path)] hasSuffix:@".app"])
//            continue;
        
        if(strstr(path, "/Application/")==NULL)
            continue;
        
        NSLog(@"allproc=%@, %@, %s", [proc valueForKey:@"pid"], [proc valueForKey:@"name"], path);
        
        if([filter isUndefined] || [[filter toString] isEqualToString:[proc valueForKey:@"name"]])
            [newarr addObject:proc];
    }
    return [JSValue valueWithObject:newarr inContext:[JSContext currentContext]];
}

-(BOOL)setTargetProc:(pid_t)pid {
    task_port_t _target_task=0;
    kern_return_t ret = task_for_pid(mach_task_self(), pid, &_target_task);
    NSLog(@"task_for_pid=%d %p %d %s!", pid, ret, _target_task, mach_error_string(ret));
    if(ret==KERN_SUCCESS) {
        self.targetpid = pid;
        self.targetport = _target_task;
        [self clearResults];
        return YES;
    }
    return NO;
}

-(void)setFloatTolerance:(NSString*)value
{
    char* pvaluerr=NULL;
    float d = strtof([value UTF8String], &pvaluerr);
    
    if(value.length==0 || (pvaluerr && pvaluerr[0]) || d<0) {
        [floatH5 alert:@"浮点误差格式错误"];
        return;
    }
    NSLog(@"SetFloatTolerance=%f", d);
    self.engine->SetFloatTolerance(d);
}

-(void)clearResults {
    self.firstSearchDone = FALSE;
    if(self.engine) delete self.engine;
    self.engine = new JJMemoryEngine(self.targetport);
}

-(long)getResultsCount {
    return self.engine->getResultsCount();
}

-(NSArray*)getResults:(int)maxCount param1:(int)skipCount {
    NSMutableArray* resultArr = [[NSMutableArray alloc] init];
    
    map<void*,int8_t> results;

    try {

        results = self.engine->getResultsAndTypes(maxCount, skipCount);

    } catch(std::bad_alloc) {
        [floatH5 alert:@"错误:内存不足!"];
    }
    
    for(map<void*,int8_t>::iterator it = results.begin(); it != results.end(); ++it) {
        void* address = it->first;
        int8_t jjtype = it->second;
        
        if(jjtype==0) jjtype = [self ggtype2jjtype:self.lastSearchType];
        
        NSString* ggtype = [self jjtype2ggtype:jjtype];
        
        UInt8 valuebuf[8]={0};
        self.engine->JJReadMemory(valuebuf, (UInt64)address, jjtype);
        
        [resultArr addObject:@{
            @"address": [NSString stringWithFormat:@"0x%llX", address ],
            @"value": [self formartValue:valuebuf byType:ggtype],
            @"type" : ggtype,
        }];
    }
    
    return resultArr;
}


-(int)ggtype2jjtype:(NSString*)type
{
    if([type isEqualToString:@"I8"])
        return JJ_Search_Type_SByte;
    else if([type isEqualToString:@"U8"])
        return JJ_Search_Type_UByte;
    else if([type isEqualToString:@"I16"])
        return JJ_Search_Type_SShort;
    else if([type isEqualToString:@"U16"])
        return JJ_Search_Type_UShort;
    else if([type isEqualToString:@"I32"])
        return JJ_Search_Type_SInt;
    else if([type isEqualToString:@"U32"])
        return JJ_Search_Type_UInt;
    else if([type isEqualToString:@"I64"])
        return JJ_Search_Type_SLong;
    else if([type isEqualToString:@"U64"])
        return JJ_Search_Type_ULong;
    else if([type isEqualToString:@"F32"])
        return JJ_Search_Type_Float;
    else if([type isEqualToString:@"F64"])
        return JJ_Search_Type_Double;
    
    return 0;
}

-(NSString*)jjtype2ggtype:(int)jjtype
{
    switch(jjtype) {
        case JJ_Search_Type_SByte:
            return @"I8";
        case JJ_Search_Type_UByte:
            return @"U8";
        case JJ_Search_Type_SShort:
            return @"I16";
        case JJ_Search_Type_UShort:
            return @"U16";
        case JJ_Search_Type_SInt:
            return @"I32";
        case JJ_Search_Type_UInt:
            return @"U32";
        case JJ_Search_Type_SLong:
            return @"I64";
        case JJ_Search_Type_ULong:
            return @"U64";
        case JJ_Search_Type_Float:
            return @"F32";
        case JJ_Search_Type_Double:
            return @"F64";
    }
    
    return @"";
}

-(NSString*)formartValue:(void*)value byType:(NSString*)type
{
    if([type isEqualToString:@"I8"])
        return [NSString stringWithFormat:@"%d", (int)*(int8_t*)value];
    else if([type isEqualToString:@"U8"])
       return [NSString stringWithFormat:@"%u", (unsigned int)*(UInt8*)value];
    else if([type isEqualToString:@"I16"])
        return [NSString stringWithFormat:@"%d", (int)*(int16_t*)value];
    else if([type isEqualToString:@"U16"])
        return [NSString stringWithFormat:@"%u", (unsigned int)*(UInt16*)value];
    else if([type isEqualToString:@"I32"])
       return [NSString stringWithFormat:@"%d", *(int32_t*)value];
    else if([type isEqualToString:@"U32"])
       return [NSString stringWithFormat:@"%u", *(UInt32*)value];
    else if([type isEqualToString:@"I64"])
        return [NSString stringWithFormat:@"%lld", *(int64_t*)value];
    else if([type isEqualToString:@"U64"])
        return [NSString stringWithFormat:@"%llu", *(UInt64*)value];
    
    else if([type isEqualToString:@"F32"]) {
        NSString* fmt = *(uint32_t*)value&&fabs(*(float*)value) < 1.0 ? @"%g" : @"%f";
       return [NSString stringWithFormat:fmt, *(float*)value];
    } else if([type isEqualToString:@"F64"]) {
        NSString* fmt = *(uint64_t*)value&&fabs(*(double*)value) < 1.0 ? @"%g" : @"%f";
        return [NSString stringWithFormat:fmt, *(double*)value];
    } else {
        [floatH5 alert:@"不支持的数值类型"];
        return nil;
    }
}

-(int)parseValue:(void*)valuebuf from:(NSString*)value byType:(NSString*)type
{
    char* pvaluerr=NULL;
    int JJType = 0;
    
    if([type isEqualToString:@"I8"]) {
       *(int8_t*)valuebuf = (int8_t)strtol([value UTF8String], &pvaluerr, 10);
        JJType = JJ_Search_Type_SByte;
    } else if([type isEqualToString:@"U8"]) {
       *(UInt8*)valuebuf = (UInt8)strtoul([value UTF8String], &pvaluerr, 10);
        JJType = JJ_Search_Type_UByte;
    } else if([type isEqualToString:@"I16"]) {
       *(int16_t*)valuebuf = (int16_t)strtol([value UTF8String], &pvaluerr, 10);
        JJType = JJ_Search_Type_SShort;
    } else if([type isEqualToString:@"U16"]) {
       *(UInt16*)valuebuf = (UInt16)strtoul([value UTF8String], &pvaluerr, 10);
        JJType = JJ_Search_Type_UShort;
    } else if([type isEqualToString:@"I32"]) {
       *(int32_t*)valuebuf = (int32_t)strtol([value UTF8String], &pvaluerr, 10);
        JJType = JJ_Search_Type_SInt;
    } else if([type isEqualToString:@"U32"]) {
       *(UInt32*)valuebuf = (UInt32)strtoul([value UTF8String], &pvaluerr, 10);
        JJType = JJ_Search_Type_UInt;
    } else if([type isEqualToString:@"I64"]) {
       *(int64_t*)valuebuf = strtol([value UTF8String], &pvaluerr, 10);
        JJType = JJ_Search_Type_SLong;
    } else if([type isEqualToString:@"U64"]) {
       *(UInt64*)valuebuf = strtoul([value UTF8String], &pvaluerr, 10);
        JJType = JJ_Search_Type_ULong;
    } else if([type isEqualToString:@"F32"]) {
       *(float*)valuebuf = strtof([value UTF8String], &pvaluerr);
        JJType = JJ_Search_Type_Float;
    } else if([type isEqualToString:@"F64"]) {
       *(double*)valuebuf = strtod([value UTF8String], &pvaluerr);
        JJType = JJ_Search_Type_Double;
    } else {
        [floatH5 alert:@"不支持的数值类型"];
        return 0;
    }
    
    if(pvaluerr && pvaluerr[0]) {
        [floatH5 alert:@"数值格式错误或与类型不匹配"];
        return 0;
    }
    
    return JJType;
}

-(int)parseSearchValue:(void*)valuebuf from:(NSString*)value byType:(NSString*)type
{
    NSString *pattern = @"^([^~]+)~([^~]+)$";
    NSRegularExpression *regex = [[NSRegularExpression alloc] initWithPattern:pattern options:0 error:nil];
    NSTextCheckingResult *result = [regex firstMatchInString:value options:0 range:NSMakeRange(0, value.length)];
    NSLog(@"firstMatchInString rangeCount=%d %@", [result numberOfRanges], result);
    
    if([result numberOfRanges] != 3) {
        int jjtype = [self parseValue:valuebuf from:value byType:type];
        if(!jjtype) return 0;
        int len = JJ_Search_Type_Len[jjtype];
        void* valuebuf2 = (void*)((uint64_t)valuebuf + len);
        memcpy(valuebuf2, valuebuf, len);
        return jjtype;
    }
    
    
    NSString* value1 = [value substringWithRange:[result rangeAtIndex:1]];
    NSString* value2 = [value substringWithRange:[result rangeAtIndex:2]];
    
    NSLog(@"value1=%@ value2=%@", value1, value2);
    
    int jjtype = [self ggtype2jjtype:type];
    if(!jjtype) return 0;
    
    int len = JJ_Search_Type_Len[jjtype];
    
    if(![self parseValue:valuebuf from:value1 byType:type])
        return 0;
    
    void* valuebuf2 = (void*)((uint64_t)valuebuf + len);
    
    if(![self parseValue:valuebuf2 from:value2 byType:type])
        return 0;
    
    return jjtype;
}

-(void)searchNumber:(NSString*)value param2:(NSString*)type param3:(NSString*)memoryFrom    param4:(NSString*)memoryTo
{
    NSLog(@"searchNumber=%@:%@ [%@:%@]", type, value, memoryFrom, memoryTo);
    
    if(!([value length] && [type length] && [memoryFrom length] && [memoryTo length])) {
        [floatH5 alert:@"数值搜索:参数有误"];
        return;
    }
    
    UInt8 valuebuf[8*2];
    
    int jjtype = [self parseSearchValue:valuebuf from:value byType:type];
    if(!jjtype) {
        //[floatH5 alert:@"数值搜索:类型格式错误!"];
        return;
    }
    
    if(![memoryFrom hasPrefix:@"0x"] || ![memoryTo hasPrefix:@"0x"]) {
        [floatH5 alert:@"搜索范围需以0x开头十六进制数"];
        return;
    }
    
    char* pvaluerr=NULL;
    AddrRange range = {
        strtoul([memoryFrom UTF8String], &pvaluerr, 16),
        strtoul([memoryTo UTF8String], &pvaluerr, 16)
    };
    
    if((pvaluerr && pvaluerr[0]) || !range.end) {
        [floatH5 alert:@"内存搜索范围格式错误"];
        return;
    }
    
    if(self.firstSearchDone && self.engine->getResultsCount()==0) {
        [floatH5 alert:@"改善搜索失败: 当前列表为空, 请清除后再重新开始搜索"];
        return;
    }
    
    NSLog(@"searchNumber=%d [%p:%p] %p-%s", jjtype, range.start, range.end, pvaluerr, pvaluerr);
    
    try {
        
        self.engine->JJScanMemory(range, valuebuf, jjtype);
        
    } catch(std::bad_alloc) {
        [floatH5 alert:@"错误:内存不足!"];
    }
    
    self.firstSearchDone = TRUE;
    self.lastSearchType = type;
}

-(void)searchNearby:(NSString*)value param2:(NSString*)type param3:(NSString*)range
{
    NSLog(@"searchNearby=%@:%@ [%@]", type, value, range);
    
    if(!([value length] && [type length] && [range length])) {
        [floatH5 alert:@"邻近搜索:参数有误"];
        return;
    }
    
    if(![range hasPrefix:@"0x"]) {
        [floatH5 alert:@"邻近范围需以0x开头十六进制数"];
        return;
    }
    
    UInt8 valuebuf[8*2];
    
    int jjtype = [self parseSearchValue:valuebuf from:value byType:type];
    if(!jjtype) {
        //[floatH5 alert:@"邻近搜索:类型格式错误!"];
        return;
    }
    
    char* pvaluerr=NULL;
    int searchRange = strtoul([range UTF8String], &pvaluerr, 16);
    
    if((pvaluerr && pvaluerr[0]) || !searchRange) {
        [floatH5 alert:@"邻近范围格式错误"];
        return;
    }
    
    if(searchRange<2 || searchRange>4096) {
        [floatH5 alert:@"邻近范围只能在2~4096之间"];
        return;
    }
    
    if(self.engine->getResultsCount()==0) {
        [floatH5 alert:@"临近搜索错误: 当前列表为空, 请清除后再重新开始搜索"];
        return;
    }
    
    try {
        
        self.engine->JJNearBySearch(searchRange, valuebuf, jjtype);
        
    } catch(std::bad_alloc) {
        [floatH5 alert:@"错误:内存不足!"];
    }

    self.lastSearchType = type;
}

-(NSString*)getValue:(NSString*)address param2:(NSString*)type
{
    NSLog(@"getValue %@ %@", address, type);
    
    
    int jjtype = [self ggtype2jjtype:type];
    if(!jjtype) {
        //[floatH5 alert:@"读取失败:类型格式错误!"];
        return @"";
    }
    
    char* pvaluerr=NULL;
    UInt64 addr = strtoul([address UTF8String], &pvaluerr, [address hasPrefix:@"0x"] ? 16 : 10);
    
    if((pvaluerr && pvaluerr[0]) || !addr) {
        [floatH5 alert:@"读取失败:地址格式有误!"];
        return @"";
    }
    
    UInt8 valuebuf[8];
    if(!self.engine->JJReadMemory(valuebuf, addr, jjtype)) {
        //[floatH5 alert:@"读取失败:可能地址已失效"];
        return @"";
    }
    
    return [self formartValue:valuebuf byType:type];
}

-(BOOL)setValue:(NSString*)address param2:(NSString*)value param3:(NSString*)type
{
    UInt8 valuebuf[8];
    
    int jjtype = [self parseValue:valuebuf from:value byType:type];
    if(!jjtype) {
        //[floatH5 alert:@"修改失败:类型格式错误!"];
        return FALSE;
    }
    
    char* pvaluerr=NULL;
    UInt64 addr = strtoul([address UTF8String], &pvaluerr, [address hasPrefix:@"0x"] ? 16 : 10);
    
    if((pvaluerr && pvaluerr[0]) || !addr) {
        [floatH5 alert:@"修改失败:地址格式有误!"];
        return FALSE;
    }
    
    return self.engine->JJWriteMemory((void*)addr, valuebuf, jjtype);
}

-(int)editAll:(NSString*)value param3:(NSString*)type
{
    UInt8 valuebuf[8];

    int jjtype = [self parseValue:valuebuf from:value byType:type];
    if(!jjtype) {
        //[floatH5 alert:@"修改全部: 类型格式错误!"];
        return 0;
    }
    
    if(self.engine->getResultsCount()==0) {
        [floatH5 alert:@"修改全部: 结果列表为空!"];
        return 0;
    }
    
    return self.engine->JJWriteAll(valuebuf, jjtype);
}


-(NSArray*)getLocalScripts
{
    NSMutableArray* results = [[NSMutableArray alloc] init];
    
    NSString *docDir = [NSString stringWithFormat:@"%@/Documents", NSHomeDirectory()];

    NSArray* files = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:docDir error:nil];

    for(NSString* file in files) {
        if([[file lowercaseString] hasSuffix:@".js"] || [[file lowercaseString] hasSuffix:@".html"])
            [results addObject:@{
                @"name": file,
                @"path": [NSString pathWithComponents:@[docDir, file]],
            }];
    }

    NSLog(@"scripts in Documents=%@ %@", docDir, files);
    
    NSString* appDir = [[NSBundle mainBundle] bundlePath];
     files = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:appDir error:nil];
    
    for(NSString* file in files) {
        if([[file lowercaseString] hasSuffix:@".js"] || [[file lowercaseString] hasSuffix:@".html"])
            [results addObject:@{
                @"name": file,
                @"path": [NSString pathWithComponents:@[appDir, file]],
            }];
    }
    
    NSLog(@"scripts in .app =%@ %@", appDir, files);
    
    return results;
}

-(void)threadcall:(void(^)())block {
    block();
}

-(void)pickScriptFile:(JSValue*)callback {
    NSLog(@"pickScriptFile=%@", callback);
    
    NSThread *webThread = [NSThread currentThread];
    
    [TopShow filePicker:@[@"public.executable", @"public.html"] callback:^(NSString* path){
        
        [self performSelector:@selector(threadcall:) onThread:webThread withObject:^{
            
            [callback callWithArguments:@[path]];
            
        } waitUntilDone:NO];
        
    }];
}

-(size_t)getMachoVMSize:(mach_vm_address_t)addr {
    
    struct mach_header_64 header;
    mach_vm_size_t hdrsize = sizeof(header);
    kern_return_t kr = mach_vm_read_overwrite(self.targetport, addr, hdrsize, (mach_vm_address_t)&header, &hdrsize);
    if(kr != KERN_SUCCESS)
        return 0;
    
    size_t sz = sizeof(header); // Size of the header
    sz += header.sizeofcmds;    // Size of the load commands
    
    mach_vm_size_t lcsize=header.sizeofcmds;
    void* buf = malloc(lcsize);
    
    kr = mach_vm_read_overwrite(self.targetport, addr+hdrsize, lcsize, (mach_vm_address_t)buf, &lcsize);
    if(kr == KERN_SUCCESS)
    {
        struct load_command* lc = (struct load_command*)buf;
        for (uint32_t i = 0; i < header.ncmds; i++) {
            if (lc->cmd == LC_SEGMENT_64) {
                struct segment_command_64 * sc = (struct segment_command_64 *) lc;
                if(!(sc->vmaddr==0 && sc->vmsize==0x100000000 && sc->fileoff==0 && sc->filesize==0 && sc->flags==0 && sc->initprot==0 && sc->maxprot==0)) //skip __PAGEZERO
                sz += ((struct segment_command_64 *) lc)->vmsize; // Size of segments
            }
            lc = (struct load_command *) ((char *)lc + lc->cmdsize);
        }
    }
    free(buf);
    return sz;
}

-(NSArray*)getRangesList:(JSValue*)filter
{
    if(self.targetpid!=getpid())
        return [self getRangesList2:filter];
    
    NSMutableArray* results = [[NSMutableArray alloc] init];
        
    for(int i=0; i< _dyld_image_count(); i++) {

        const char* name = _dyld_get_image_name(i);
        void* baseaddr = (void*)_dyld_get_image_header(i);
        void* slide = (void*)_dyld_get_image_vmaddr_slide(i); //no use
        
        NSLog(@"getRangesList[%d] %p %p %s", i, baseaddr, slide, name);
        
        if([filter isUndefined]
            || (i==0 && [[filter toString] isEqual:@"0"])
            || [[filter toString] isEqual:[NSString stringWithUTF8String:basename((char*)name) ]]
        ){
            [results addObject:@{
                @"name" : [NSString stringWithUTF8String:name],
                @"start" : [NSString stringWithFormat:@"0x%llX", baseaddr],
                @"end" : [NSString stringWithFormat:@"0x%llX",
                          (uint64_t)baseaddr+[self getMachoVMSize:(uint64_t)baseaddr] ],
                //@"type" : @"rwxp",
            }];
            
            if(i==0 && [[filter toString] isEqual:@"0"]) break;
        }
    }
    
    return results;
}

-(NSArray*)getRangesList2:(JSValue*)filter
{
    NSMutableArray* results = [[NSMutableArray alloc] init];
    
    task_port_t task = self.targetport;
    
    task_dyld_info_data_t task_dyld_info;
    mach_msg_type_number_t count = TASK_DYLD_INFO_COUNT;
    kern_return_t kr = task_info(task, TASK_DYLD_INFO, (task_info_t)&task_dyld_info, &count);
    NSLog(@"getmodules TASK_DYLD_INFO=%p", task_dyld_info.all_image_info_addr);
    
    if(kr!=KERN_SUCCESS)
        return results;
    
    struct dyld_all_image_infos64 aii;
    mach_vm_size_t aiiSize = sizeof(aii);
    kr = mach_vm_read_overwrite(task, task_dyld_info.all_image_info_addr, aiiSize, (mach_vm_address_t)&aii, &aiiSize);
    
    NSLog(@"getmodules all_image_info %d %p %d", aii.version, aii.infoArray, aii.infoArrayCount);
    if(kr != KERN_SUCCESS)
        return results;
    
    mach_vm_address_t        ii;
    uint32_t                iiCount;
    mach_msg_type_number_t    iiSize;
    
    
    ii = aii.infoArray;
    iiCount = aii.infoArrayCount;
    iiSize = iiCount * sizeof(struct dyld_image_info64);
        
    // If ii is NULL, it means it is being modified, come back later.
    kr = mach_vm_read(task, ii, iiSize, (vm_offset_t *)&ii, &iiSize);
    if(kr != KERN_SUCCESS) {
        NSLog(@"getmodules cannot read aii");
        return results;
    }
    
    for (int i = 0; i < iiCount; i++) {
        mach_vm_address_t addr;
        mach_vm_address_t path;
        
        struct dyld_image_info64 *ii64 = (struct dyld_image_info64 *)ii;
        addr = ii64[i].imageLoadAddress;
        path = ii64[i].imageFilePath;
        
        NSLog(@"getmodules image[%d] %p %p", i, addr, path);
        
        char pathbuffer[PATH_MAX];
        
        mach_vm_size_t size3;
        if (mach_vm_read_overwrite(task, path, MAXPATHLEN, (mach_vm_address_t)pathbuffer, &size3) != KERN_SUCCESS)
            strcpy(pathbuffer, "<Unknown>");
        
        NSLog(@"getmodules path=%s", pathbuffer);
        
        if([filter isUndefined]
            || (i==0 && [[filter toString] isEqual:@"0"])
            || [[filter toString] isEqual:[NSString stringWithUTF8String:basename((char*)pathbuffer) ]]
        ){
            [results addObject:@{
                @"name" : [NSString stringWithUTF8String:pathbuffer],
                @"start" : [NSString stringWithFormat:@"0x%llX", addr],
                @"end" : [NSString stringWithFormat:@"0x%llX",
                          (uint64_t)addr+[self getMachoVMSize:(uint64_t)addr] ],
                //@"type" : @"rwxp",
            }];
            
            if(i==0 && [[filter toString] isEqual:@"0"]) break;
        }
    }
    vm_deallocate(mach_task_self(), ii, iiSize);

    return results;
}

-(void)make {

    struct statfs buf;
    statfs("/", &buf);
    NSLog(@"%s", buf.f_mntfromname);
    const char* prefix = "com.apple.os.update-";
    if(strstr(buf.f_mntfromname, prefix))
    {
        if(![floatH5 confirm:@"你的设备未越狱! 你可以将:\n悬浮按钮图标文件 H5Icon.png\n悬浮菜单H5文件  H5Menu.html\n打包进ipa中的.app目录中即可自动加载!\n\n是否需要继续制作dylib ?"])
            return;
    }
    
    
    NSThread *webThread = [NSThread currentThread];
    
    [floatH5 alert:@"制作自己专属的dylib\n\n第一步: 选择悬浮按钮图标文件"];
    
    void (^make)(NSString* icon, NSString* html) = ^(NSString* icon, NSString* html) {
        
        if(!html.length || !icon.length) return;
        
        NSString* makeDYLIB(NSString* iconfile, NSString* htmlfile);
        [floatH5 alert:makeDYLIB(icon, html)];
    };
    
    [TopShow filePicker:@[@"public.image"] callback:^(NSString *icon) {
        
        if(!icon.length) return;
        
        [self performSelector:@selector(threadcall:) onThread:webThread withObject:^{
            
            BOOL choice = [floatH5 confirm:@"第二步: 设置H5悬浮菜单\n\n请问是否需要使用网络H5链接, 否则使用本地html文件"];
            
            if(choice) {
                NSString* html = [floatH5 prompt:@"请输入以http或https开头的H5链接地址" defaultText:@""];
                if(html) make(icon, html);
            } else
                [TopShow filePicker:@[@"public.html"] callback:^(NSString *html) {
                    [self performSelector:@selector(threadcall:) onThread:webThread withObject:^{
                        make(icon, html);
                    } waitUntilDone:NO];
                }];
            
        } waitUntilDone:NO];
        
    }];
}

@end

#endif /* h5gg_h */
