#import <CoreFoundation/CoreFoundation.h>
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <pthread.h>
#include <dlfcn.h>
#import <SystemConfiguration/SystemConfiguration.h>
#include <JavaScriptCore/JSTypedArray.h>

//忽略一些警告
#pragma GCC diagnostic ignored "-Warc-retain-cycles"

#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wincomplete-implementation"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#pragma GCC diagnostic ignored "-W#warnings"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wformat"

bool g_dylib_runmode = false;
bool g_testapp_runmode = false;
bool g_commonapp_runmode = false;
bool g_systemapp_runmode = false;
bool g_standalone_runmode = false;

//引入悬浮按钮头文件
#include "FloatButton.h"
//引入悬浮菜单头文件
#include "FloatMenu.h"

//引入h5gg的JS引擎头文件
#include "h5gg.h"

//使用incbin库用于嵌入其他资源文件
#include "incbin.h"

//嵌入图标文件
INCBIN(Icon, "icon.png");
//嵌入菜单H5文件
INCTXT(Menu, "Menu.html");

INCTXT(H5GG_JQUERY_FILE, "jquery.min.js");

INCBIN(H5ICON_STUB_FILE, "H5ICON_STUB_FILE");
INCBIN(H5MENU_STUB_FILE, "H5MENU_STUB_FILE");

NSString* makeDYLIB(NSString* iconfile, NSString* htmlurl)
{
    struct dl_info di={0};
    dladdr((void*)makeDYLIB, &di);
    
    NSMutableData* dylib = [NSMutableData dataWithContentsOfFile:[NSString stringWithUTF8String:di.dli_fname]];
    NSData* icon = [NSData dataWithContentsOfFile:iconfile];
    NSData* html;
    
    if([[htmlurl lowercaseString] hasPrefix:@"http"])
        html = [htmlurl dataUsingEncoding:NSUTF8StringEncoding];
    else
        html = [NSData dataWithContentsOfFile:htmlurl];
    
    if(!dylib || !icon || !html)
        return @"制作失败\n\n无法读取数据!";
    
    if(icon.length>=gH5ICON_STUB_FILESize)
        return @"制作失败\n\n图标文件超过512KB";
    
    if(html.length>=gH5MENU_STUB_FILESize)
        return @"制作失败\n\nH5文件超过2MB";
    
    NSData *pattern = [[NSString stringWithUTF8String:(char*)gH5ICON_STUB_FILEData] dataUsingEncoding:NSUTF8StringEncoding];
    NSRange range = [dylib rangeOfData:pattern options:0 range:NSMakeRange(0, dylib.length)];
    if(range.location == NSNotFound)
        return @"制作失败\n\n当前已经是定制版本";
    
    [dylib replaceBytesInRange:NSMakeRange(range.location, icon.length) withBytes:icon.bytes];
    
    NSData *pattern2 = [[NSString stringWithUTF8String:(char*)gH5MENU_STUB_FILEData] dataUsingEncoding:NSUTF8StringEncoding];
    NSRange range2 = [dylib rangeOfData:pattern2 options:0 range:NSMakeRange(0, dylib.length)];
    if(range2.location == NSNotFound)
        return @"制作失败\n\n当前已经是定制版本";
    
    [dylib replaceBytesInRange:NSMakeRange(range2.location, html.length) withBytes:html.bytes];
    
    if(![dylib writeToFile:[NSString stringWithFormat:@"%@/Documents/H5GG.dylib", NSHomeDirectory()] atomically:NO])
        return [NSString stringWithFormat:@"制作失败\n\n无法写入文件到", NSHomeDirectory()];
    
    return [NSString stringWithFormat:@"制作成功!\n\n专属H5GG.dylib已生成在当前App的Documents数据目录:\n\n%@/Documents/H5GG.dylib", NSHomeDirectory()];
}

//定义悬浮按钮和悬浮菜单全局变量, 防止被自动释放
UIWindow* floatWindow=NULL;
FloatButton* floatBtn=NULL;
FloatMenu* floatH5=NULL;
h5ggEngine* h5gg = NULL;

JSValue* gButtonAction=NULL;
NSThread* gWebThread=NULL;

//for gloablview call in other source file without header file
void setButtonKeepWindow(BOOL keep){floatBtn.keepWindow = keep;}

@interface FloatWindow : UIWindow
@end

@implementation FloatWindow
// recursively calls -pointInside:withEvent:. point is in the receiver's coordinate system
//-(nullable UIView *)hitTest:(CGPoint)point withEvent:(nullable UIEvent *)event /
//{
//
//}
// default returns YES if point is in bounds
- (BOOL)pointInside:(CGPoint)point withEvent:(nullable UIEvent *)event;
{
    int count = (int)self.subviews.count;
    for (int i = count - 1; i >= 0;i-- ) {
        UIView *childV = self.subviews[i];
        // 把当前坐标系上的点转换成子控件坐标系上的点.
        CGPoint childP = [self convertPoint:point toView:childV];
        UIView *fitView = [childV hitTest:childP withEvent:event];
        if(fitView) {
            //NSLog(@"FloatWindow pointInside=%@", fitView);
            return YES;
        }
    }
    return NO;
}
@end

@interface FloatController : UIViewController
@end

@implementation FloatController

static UIWindow* FloatController_lastKeyWindow=nil;

-(instancetype)init {
    self = [super init];
    if(self) {
        FloatController_lastKeyWindow = [UIApplication sharedApplication].keyWindow;
    }
    return self;
}

//如果不定义旋转相关委托函数, 并且屏幕锁定开关没有打开, 则UIAlertController会跟随陀螺仪旋转, 并且界面全部卡死
//主要是supportedInterfaceOrientations返回的支持方向集合, 如果原window不支持竖屏, 新window旋转为横屏, 则原window会卡死

-(UIInterfaceOrientation)interfaceOrientation {
    NSLog(@"FloatWindow interfaceOrientation=%d", [FloatController_lastKeyWindow.rootViewController interfaceOrientation]);
    return [FloatController_lastKeyWindow.rootViewController interfaceOrientation];
}
-(BOOL)shouldAutorotateToInterfaceOrientation:(UIInterfaceOrientation)toInterfaceOrientation
{
    NSLog(@"FloatWindow shouldAutorotateToInterfaceOrientation=%d", toInterfaceOrientation);
    return [FloatController_lastKeyWindow.rootViewController shouldAutorotateToInterfaceOrientation:toInterfaceOrientation];
}
//上面两个废弃方法似乎没啥作用
- (BOOL)shouldAutorotate {
    NSLog(@"FloatWindow shouldAutorotate=%d", [FloatController_lastKeyWindow.rootViewController shouldAutorotate]);
    return [FloatController_lastKeyWindow.rootViewController shouldAutorotate];
}

- (UIInterfaceOrientationMask)supportedInterfaceOrientations {
    NSLog(@"FloatWindow supportedInterfaceOrientations=%d", [FloatController_lastKeyWindow.rootViewController supportedInterfaceOrientations]);
    return [FloatController_lastKeyWindow.rootViewController supportedInterfaceOrientations];
}

-(UIInterfaceOrientation)preferredInterfaceOrientationForPresentation {
    NSLog(@"FloatWindow preferredInterfaceOrientationForPresentation=%d", [FloatController_lastKeyWindow.rootViewController preferredInterfaceOrientationForPresentation]);

    NSLog(@"orientation=%d statusBarOrientation=%d", [[UIDevice currentDevice] orientation], [UIApplication sharedApplication].statusBarOrientation);

    return [FloatController_lastKeyWindow.rootViewController preferredInterfaceOrientationForPresentation];
}

@end

UIWindow* makeWindow()
{
    UIWindow* w = nil;
    
    if (@available(iOS 13.0, *)) {
        UIWindowScene* theScene=nil;
        for (UIWindowScene* windowScene in [UIApplication sharedApplication].connectedScenes) {
            NSLog(@"windowScene=%@ %@ state=%d", windowScene, windowScene.windows, windowScene.activationState);
            if(!theScene && windowScene.activationState==UISceneActivationStateForegroundInactive)
                theScene = windowScene;
            if (windowScene.activationState == UISceneActivationStateForegroundActive) {
                theScene = windowScene;
                break;
            }
        }
        w = [[FloatWindow alloc] initWithWindowScene:theScene];
    }else{
        w = [[FloatWindow alloc] initWithFrame:[UIScreen mainScreen].bounds];
    }
    
    return w;
}



extern "C" {
CGContextRef m_cgContext=0;
IOSurfaceRef gCanvasSurface=0;
}

void initFloatMenu()
{
    //创建悬浮菜单, 设置位置=居中  尺寸=380宽x屏幕高(最大400)
    CGRect MenuRect = CGRectMake(0, 0, 370, 370);
    MenuRect.origin.x = (floatWindow.frame.size.width-MenuRect.size.width)/2;
    MenuRect.origin.y = (floatWindow.frame.size.height-MenuRect.size.height)/2;
    
    floatH5 = [[FloatMenu alloc] initWithFrame:MenuRect];
        
    //创建并初始化h5gg内存搜索引擎
    h5gg = [[h5ggEngine alloc] init];
    //将h5gg内存搜索引擎添加到H5的JS环境中以便JS可以调用
    [floatH5 setAction:@"h5gg" callback:h5gg];
    
    //隐藏悬浮菜单, 已废弃, 保持旧版API兼容
    [floatH5 setAction:@"closeMenu" callback:^{}];
    //设置网络图标, 已废弃, 保持旧版API兼容
    [floatH5 setAction:@"setFloatButton" callback:^{}];
    //设置悬浮窗位置尺寸, 已废弃, 保持旧版API兼容性
    [floatH5 setAction:@"setFloatWindow" callback:^{}];
    
    //给H5菜单添加一个JS函数setButtonImage用于设置网络图标
    [floatH5 setAction:@"setButtonImage" callback:^(NSString* url) {
        NSURL* imageUrl = [NSURL URLWithString:url];
        NSData* data = [NSData dataWithContentsOfURL:imageUrl];
        NSLog(@"setFloatButton=%@", data);
        //通过主线程执行下面的代码
        dispatch_async(dispatch_get_main_queue(), ^{
            if(data) floatBtn.image = [UIImage imageWithData:data];
        });
        return data?YES:NO;
    }];
    
    [floatH5 setAction:@"setButtonAction" callback:^(JSValue* callback) {
        gButtonAction = callback;
        gWebThread = [NSThread currentThread];
    }];
    
    //给H5菜单添加一个JS函数setFloatWindow用于设置悬浮窗位置尺寸
    [floatH5 setAction:@"setWindowRect" callback:^(int x, int y, int w, int h) {
        //通过主线程执行下面的代码
        dispatch_async(dispatch_get_main_queue(), ^{
            floatH5.frame = CGRectMake(x,y,w,h);
        });
    }];
    
    [floatH5 setAction:@"setWindowDrag" callback:^(int x, int y, int w, int h) {
        //通过主线程执行下面的代码
        dispatch_async(dispatch_get_main_queue(), ^{
            [floatH5 setDragRect: CGRectMake(x,y,w,h)];
        });
    }];
    
    [floatH5 setAction:@"setWindowTouch" callback:^(int x, int y, int w, int h) {
        NSLog(@"setWindowTouch %d %d %d %d", x, y, w, h);
        if((y==0&&w==0&&h==0) && (x==0||x==1)) {
            floatH5.touchableAll = x==1;
            floatH5.touchableRect = CGRectZero;
        } else {
            floatH5.touchableAll = NO;
            floatH5.touchableRect = CGRectMake(x,y,w,h);
        }
    }];
     
     [floatH5 setAction:@"setWindowVisible" callback:^(bool visible) {
         NSLog(@"setWindowVisible=%d", visible);
        //通过主线程执行下面的代码
        dispatch_async(dispatch_get_main_queue(), ^{
            void showFloatWindow(bool show);
            showFloatWindow(visible);
        });
    }];
    
    
    /* 三种加载方式任选其一 */

    NSString* htmlstub = [NSString stringWithUTF8String:(char*)gH5MENU_STUB_FILEData];
    NSLog(@"html stub hash=%p", [htmlstub hash]);
    
    //ipa的.app目录中的H5文件名
    NSString* h5file = [[NSBundle mainBundle] pathForResource:@"H5Menu" ofType:@"html"];
    
    if([htmlstub hash] != 0xc25ce928da0ca2de) {
        //第一优先级: 从网址加载H5
        if([[htmlstub lowercaseString] hasPrefix:@"http"])
            [floatH5 loadRequest:[[NSURLRequest alloc] initWithURL:[NSURL URLWithString:htmlstub]]];
        else
            [floatH5 loadHTMLString:htmlstub baseURL:[NSURL URLWithString:@"html@dylib"]];
    } else if([[NSFileManager defaultManager] fileExistsAtPath:h5file]) {
        //第二优先级: 从文件加载H5
        [floatH5 loadRequest:[[NSURLRequest alloc] initWithURL:[NSURL URLWithString:h5file]]];
    } else {
        //第三优先级: 从dylib加载H5
        NSString* h5gghtml = [NSString stringWithUTF8String:gMenuData];
        NSString* jquery = [NSString stringWithUTF8String:gH5GG_JQUERY_FILEData];
        h5gghtml = [h5gghtml stringByReplacingOccurrencesOfString:@"var h5gg_jquery_stub;" withString:jquery];
        [floatH5 loadHTMLString:h5gghtml baseURL:[NSURL URLWithString:@"html@dylib"]];
    }
}


void showFloatWindowContinue(bool show);
void showFloatWindow(bool show)
{
    if(!floatWindow)
    {
        SCNetworkReachabilityFlags flags;
        SCNetworkReachabilityRef reachability = SCNetworkReachabilityCreateWithName(NULL, "www.baidu.com");
        if(!SCNetworkReachabilityGetFlags(reachability, &flags) || (flags & kSCNetworkReachabilityFlagsReachable)==0)
        {
            NSString* tips = g_standalone_runmode ? @"请尝试使用以下越狱插件修复联网权限:\n\n<连个锤子>\n\n<FixNets>\n\n<NetworkManage>\n":@"H5GG可能无法正确加载!";
            
            [TopShow present:^{
                UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"没有网络" message:tips preferredStyle:UIAlertControllerStyleAlert];

                [alert addAction:[UIAlertAction actionWithTitle:@"继续启动" style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
                    [TopShow dismiss];
                    
                    showFloatWindowContinue(show);
                }]];
                return alert;
            }];
            
            return;
        }
        CFRelease(reachability);
    }
    
    showFloatWindowContinue(show);
}

void showFloatWindowContinue(bool show)
{
    if(!floatWindow) {
        initFloatMenu();
        
        //获取窗口
        floatWindow = makeWindow();
        floatWindow.windowLevel = UIWindowLevelAlert - 1;
        floatWindow.rootViewController = [[FloatController alloc] init];
        
        //添加H5悬浮菜单到窗口上
        [floatWindow addSubview:floatH5];
    }
    
    if(show)
    {
        FloatController_lastKeyWindow = [UIApplication sharedApplication].keyWindow;
        
        [floatWindow makeKeyAndVisible];
        [floatWindow addSubview:floatBtn];
        floatBtn.keepFront = NO;
        [floatH5 setHidden:NO];
        
        static dispatch_once_t predicate;
         dispatch_once(&predicate, ^{
             //因为在makeKeyAndVisible之前就addSubView了, 所以需要加view移到前台才有响应
             [floatWindow bringSubviewToFront:floatH5];
             //第一次如果悬浮窗口全屏会遮挡按钮无响应, 重置一次前台
             [floatWindow bringSubviewToFront:floatBtn];
             
             //移除vc自动创建的全屏view (ipad模式还会有多层superview)
             UIView* superview = floatWindow.rootViewController.view;
             while(superview && superview!=floatWindow)
             {
                 [superview setHidden:YES];
                 superview = superview.superview;
             }
             

//             floatWindow.clipsToBounds = TRUE;
//             //floatH5.bounds = CGRectMake(-floatH5.frame.origin.x, -floatH5.frame.origin.y, floatH5.bounds.size.width, floatH5.bounds.size.height);;
//             //floatH5.frame = CGRectMake(0, 0, floatH5.frame.size.width, floatH5.frame.size.height);
//                #define angle2Rad(angle) ((angle) / 180.0 * M_PI)
//             floatH5.transform = CGAffineTransformMakeRotation((angle2Rad(90)));
//             //floatWindow.frame = CGRectMake(0, 0, floatWindow.frame.size.height, floatWindow.frame.size.width);
//             NSLog(@"superview=%@", FloatController_lastKeyWindow.superview);
             
         });
    } else {
        [FloatController_lastKeyWindow addSubview:floatBtn];
        [FloatController_lastKeyWindow makeKeyAndVisible];
        [floatWindow setHidden:YES];
        floatBtn.keepFront = YES;
        
        FloatController_lastKeyWindow = nil;
    }
}

void initFloatButton(void (^callback)(void))
{
    //获取窗口
    UIWindow *window = [UIApplication sharedApplication].keyWindow;
    
    //创建悬浮按钮
    floatBtn = [[FloatButton alloc] init];
    
    if(g_testapp_runmode)
        floatBtn.center = CGPointMake(150, 60);
    
    UIImage* iconImage=nil;
    
    NSString* iconstub = [NSString stringWithUTF8String:(char*)gH5ICON_STUB_FILEData];
    NSLog(@"icon stub hash=%p", [iconstub hash]);
    
    //ipa的.app目录中的图标文件名
    NSString* iconfile = [[NSBundle mainBundle] pathForResource:@"H5Icon" ofType:@"png"];
    
    if([iconstub hash] != 0x1fdd7fff7d401bd2) {
        //第一优先级:
        NSData* iconData = [[NSData alloc] initWithBytes:gH5ICON_STUB_FILEData length:gH5ICON_STUB_FILESize];
        iconImage = [[UIImage alloc] initWithData:iconData];
    } else if([[NSFileManager defaultManager] fileExistsAtPath:iconfile]) {
        //第二优先级: 从文件加载图标
        iconImage = [UIImage imageNamed:iconfile];
    } else {
        //第三优先级: 从dylib加载图标
        NSData* iconData = [[NSData alloc] initWithBytes:gIconData length:gIconSize];
        iconImage = [[UIImage alloc] initWithData:iconData];
    }
    
    //设置悬浮按钮图标
    [floatBtn setIcon:iconImage];
    
    //设置悬浮按钮点击处理, 点击时反转显示隐藏的状态
    [floatBtn setAction:callback];
    
    //将悬浮按钮添加到窗口上
    [window addSubview:floatBtn];
}

static void* thread_running(void* arg)
{
    //等一秒, 等系统框架初始化完
    sleep(1);
    
    //通过主线程执行下面的代码
    dispatch_async(dispatch_get_main_queue(), ^{
        
        if(g_standalone_runmode)
        {
            if(getRunningProcess()==nil)
                return;
            
//            for(UIWindow* window in UIApplication.sharedApplication.windows)
//            {
//                NSLog(@"GlobalView=windows=%@", window);
//
//                window.alpha = 0; //works fine
//                window.opaque = NO; //no effect
//                window.backgroundColor = [UIColor clearColor]; //no effect
//            }
        }
        
        NSString* app_package = [[NSBundle mainBundle] bundleIdentifier];
        NSString* htmlstub = [NSString stringWithUTF8String:(char*)gH5MENU_STUB_FILEData];
        if(app_package.hash==0xccca3dc699edf771 && [htmlstub hash]==0xc25ce928da0ca2de) {
            [TopShow alert:@"风险提示!" message:@"建议卸载当前deb, 使用H5GG跨进程版!"];
        }
        
        if(g_standalone_runmode) {
            showFloatWindow(true); //直接加载悬浮按钮和悬浮窗口
        } else {
            //三方app中第一次点击图标时再加载H5菜单,防止部分APP不兼容H5导致闪退卡死
             initFloatButton(^(void) {
                 if(gButtonAction) {
                     [h5gg performSelector:@selector(threadcall:) onThread:gWebThread withObject:^{
                         [gButtonAction callWithArguments:nil];
                     } waitUntilDone:NO];
                 } else {
                     bool show = floatWindow ? floatWindow.isHidden : YES;
                     NSLog(@"ButtonShowWindow=%d", show);
                     showFloatWindow(show);
                 }
             });
        }
    });
    
    return 0;
}

extern "C" {
int memorystatus_control(uint32_t command, pid_t pid, uint32_t flags, void *buffer, size_t buffersize);
}

//初始化函数, 插件加载后系统自动调用
static void __attribute__((constructor)) _init_()
{
    struct dl_info di={0};
    dladdr((void*)_init_, &di);

    NSString* app_path = [[NSBundle mainBundle] bundlePath];
    NSString* app_package = [[NSBundle mainBundle] bundleIdentifier];
    
    NSLog(@"H5GGLoad:%d %d hash:%p app_path=%@\nfirst module header=%p slide=%p current=%p\nmodule=%s\n",
          getuid(), getgid(), [app_package hash], app_path,
          _dyld_get_image_header(0), _dyld_get_image_vmaddr_slide(0),
          di.dli_fbase, di.dli_fname);
    
    //判断是APP程序加载插件(排除后台程序和APP扩展)
    if(![app_path hasSuffix:@".app"]) return;
    
    if([app_path hasPrefix:@"/Applications/"])
        g_standalone_runmode = true;
    
    if([app_package isEqualToString:@"com.test.h5gg"])
        g_testapp_runmode = true;
    
    if([[NSString stringWithUTF8String:di.dli_fname] hasSuffix:@".dylib"])
        g_dylib_runmode = true;
    
    if([app_path containsString:@"/var/"]||[app_path containsString:@"/Application/"])
        g_commonapp_runmode = true;
    
    if(g_testapp_runmode && g_dylib_runmode)
        return;
    
    //判断是普通版还是跨进程版, 防止混用
    if(g_standalone_runmode && g_dylib_runmode)
    {
        NSString* plistPath = [NSString stringWithUTF8String:di.dli_fname];
        char* p = (char*)plistPath.UTF8String + strlen(di.dli_fname) - 5;
        strcpy(p, "plist");
        
        NSDictionary* plist = [[NSDictionary alloc] initWithContentsOfFile:plistPath];
        NSLog(@"plist=%@\n%@\n%@\n%@", plistPath, plist, plist[@"Filter"], plist[@"Filter"][@"Bundles"]);
        if(plist) {
            for(NSString* bundleId in plist[@"Filter"][@"Bundles"]) {
                if([bundleId isEqualToString:app_package]) {
                    g_systemapp_runmode = true;
                    break;
                }
            }
            
            if(!g_systemapp_runmode) for(NSString* bundleId in plist[@"Filter"][@"Bundles"]) {
                NSBundle* test = [NSBundle bundleWithIdentifier:bundleId];
                NSLog(@"filter bundle id=%@, %@, %d", bundleId, test, [test isLoaded]);
                if(test && ![bundleId isEqualToString:app_package]) {
                    NSLog(@"found common bundle inject! this deb is not a crossproc version!");
                    return;
                }
            }

        }
    }
    
    
    if(g_standalone_runmode||g_commonapp_runmode)
    {
        pthread_t thread;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_create(&thread, &attr, thread_running, nil);
        
        /* Set active memory limit = inactive memory limit, both non-fatal    */
        #define MEMORYSTATUS_CMD_SET_JETSAM_HIGH_WATER_MARK   5
        memorystatus_control(MEMORYSTATUS_CMD_SET_JETSAM_HIGH_WATER_MARK, getpid (), 1024, NULL, 0); //igg
        /* Set active memory limit = inactive memory limit, both fatal    */
        #define MEMORYSTATUS_CMD_SET_JETSAM_TASK_LIMIT          6
        memorystatus_control(MEMORYSTATUS_CMD_SET_JETSAM_TASK_LIMIT, getpid (), 256, NULL, 0); //the other way
        
    }
}
