/*
    This file is part of reicast-osx
*/
#include "license/bsd"

/*
    The start of the emulator on macOS (PL-D43; docs/nulldc3/automated-runs.md).

    nulldc_macos_main() (main.m calls it) decides nothing about AppKit before the emulator has read its command line
    and its configuration: stdout line-buffered, the directories (Foundation only), common_linux_setup() (the fault
    handler), the words macOS and Xcode add to the command line taken out, nulldc_init(). The command line, the
    configuration and run control (config:Headless) are read in there, and the emulator then asks for its window,
    os_CreateWindow() below:
    - headless, it returns: no NSApplication, no window, no menu, no Dock icon, nothing of AppKit or LaunchServices.
      The user interface's loop runs on the main thread, as on Linux, then nulldc_term(), and main returns
      runctl_exit_code(). Nothing on that path uses the main dispatch queue (there is no run loop).
    - with a window, it makes the NSApplication there (the regular policy: the Dock icon, the menu, this delegate),
      then the window; main then runs the application. applicationDidFinishLaunching starts the console capture,
      the virtual serial port and the thread of the user interface's loop. When that loop ends by itself (an exit
      key, a failed start) the application terminates itself; the exit status is runctl_exit_code() either way
      (applicationWillTerminate). Windows are not restorable: a run writes no window state outside its directories.
    After nulldc_init: --help or --version 0, a refused command line 2, another failure 1. A refused command line
    shows an alert only when nobody can read its message: no --config item sets config:Headless=yes, stderr is
    /dev/null or closed, and LaunchServices started the process (__CFBundleIdentifier in the environment, or the
    parent is launchd); otherwise the message is on stderr alone.

    The test hooks (tests/macos): NULLDC_TEST_FATAL=<kind> raises a fault right after nulldc_init (segv, ill, trap,
    abort; div on x86_64 only: an integer divide by zero does not trap on arm64), NULLDC_TEST_UI_REPORT=1 prints
    "nulldc-ui: app=<0|1> windows=<n>" when main ends: whether there is an NSApplication, and its windows.
*/

#import "AppDelegate.h"
#import "MainMenu.h"
#import "libswirl.h"
#import "gui/gui_renderer.h"
#include "cfg/cfg.h"
#include "nulldc_version.h"
#include "utils/run_control.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <vector>

#if FEAT_HAS_SERIAL_TTY
#include <util.h>
bool common_serial_pty_setup();
#endif

void common_linux_setup();
void common_linux_fault_block(bool block);
void rend_resize(int width, int height);
extern int screen_dpi;

#if FEAT_HAS_NIXPROF
void install_prof_handler(int id);
#endif

static AppDelegate *_sharedInstance;
static CGFloat _backingScaleFactor;
static CGSize _glViewSize;
static void gl_resize();
static void test_ui_report();

#if FEAT_HAS_SERIAL_TTY
static bool _removePTYSymlink = false;
#endif

@interface AppDelegate()
@property (strong) NSTextStorage *consoleTextStorage;
@end

@implementation AppDelegate {
    NSThread *_uiThread;

    // Console redirection
    dispatch_source_t _stdoutSource;
    dispatch_source_t _stderrSource;

    // the emulator has ended (nulldc_term), and AppKit asked to terminate
    BOOL _emulatorEnded;
    BOOL _terminateAsked;
}

#pragma mark - Delegate Methods -

+ (AppDelegate *)sharedInstance {
    return _sharedInstance;
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    [self captureConsoleOutput];
    #if FEAT_HAS_SERIAL_TTY
    _removePTYSymlink = common_serial_pty_setup();
    #endif
    [self setupUIThread];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
    // Exit the app when clicking the window's close button
    return YES;
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender {
    // The emulator has ended already (the loop of the user interface ended by itself)
    if (_emulatorEnded)
        return NSTerminateNow;

    // Start shutting down the emulator
    _terminateAsked = YES;
    [self shutdownEmulator];

    // Keep the app open until it's safe to exit
    return NSTerminateLater;
}

- (void)applicationWillTerminate:(NSNotification *)notification {
    #if FEAT_HAS_SERIAL_TTY
    if (_removePTYSymlink)
        unlink(settings.debug.VirtualSerialPortFile.c_str());
    #endif
    test_ui_report();
    // AppKit would end the process with 0 next: the exit status of the run instead
    fflush(stdout);
    exit(runctl_exit_code());
}

- (void)showAboutPanel:(id)sender {
    // ApplicationVersion is the version of the build (nulldc_version()), Version its CFBundleVersion, X.Y.Z.N: a
    // pre-release never shows as the 3.0.0 of CFBundleShortVersionString
    NSString *version = [NSString stringWithUTF8String:nulldc_version()];
    NSString *build = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleVersion"];
    NSMutableDictionary *options = [NSMutableDictionary dictionary];
    if (version)
        options[NSAboutPanelOptionApplicationVersion] = version;
    options[NSAboutPanelOptionVersion] = build ? build : @"";
    [NSApp orderFrontStandardAboutPanelWithOptions:options];
}

// TODO: BEN This works while in BIOS or game, but doesn't correctly update the DPI for ImGUI
- (void)windowDidChangeBackingProperties:(NSNotification *)notification {
    // Handle DPI changes dragging between monitors
    _backingScaleFactor = _mainWindow.screen.backingScaleFactor;
    [self setupScreenDPI];
    gl_resize();
}

- (void)windowWillClose:(NSNotification *)notification {
    if (notification.object == _mainWindow) {
        // Make sure to close the console window if we're closing nullDC
        [_consoleWindow close];
    } else if (notification.object == _consoleWindow) {
        _consoleWindow = nil;
        _consoleViewController = nil;
        
        // Change menu item
        NSMenuItem *menuItem = [MainMenu toggleConsoleMenuItem];
        menuItem.title = @"Show Console";
        menuItem.action = @selector(showConsoleWindow:);
    }
}

- (void)emuGLViewIsResizing:(EmuGLView *)emuGLView {
    // Smoother rendering while resizing the window
    _glViewSize = emuGLView.frame.size;
    gl_resize();
}

#pragma mark - Action Methods -

- (void)showConsoleWindow:(NSMenuItem *)menuItem {
    if (!_consoleWindow) {
        // Create the ConsoleViewController
        _consoleViewController = [[ConsoleViewController alloc] initWithTextStorage:_consoleTextStorage];
        
        // Create the Window
        NSWindowStyleMask styleMask = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
        _consoleWindow = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 640, 480) styleMask:styleMask backing:NSBackingStoreBuffered defer:NO];
        _consoleWindow.delegate = self;
        _consoleWindow.contentViewController = _consoleViewController;
        _consoleWindow.releasedWhenClosed = NO;
        _consoleWindow.restorable = NO;
        _consoleWindow.title = @"Console Output";
        [_consoleWindow makeKeyAndOrderFront:_consoleWindow];
        [_consoleWindow setFrameOrigin:NSMakePoint(NSMaxX(_mainWindow.frame) + 50, NSMinY(_mainWindow.frame))];
        [_mainWindow makeKeyAndOrderFront:_mainWindow];
        
        // Change menu item
        menuItem.title = @"Hide Console";
        menuItem.action = @selector(hideConsoleWindow:);
    }
}

- (void)hideConsoleWindow:(NSMenuItem *)menuItem {
    if (_consoleWindow) {
        [_consoleWindow close];
    }
}

#pragma mark - Setup Methods -

- (void)captureConsoleOutput {
    // Create the console text storage
    _consoleTextStorage = [[NSTextStorage alloc] init];
    
    // Block to consolidate common code
    void (^handleConsoleEvent)(NSPipe*, void*, int) = ^void(NSPipe *pipe, void *buffer, int fdOrig) {
        // Read from stderr/out
        ssize_t readResult = 0;
        do {
            errno = 0;
            readResult = read(pipe.fileHandleForReading.fileDescriptor, buffer, 4096);
        } while (readResult == -1 && errno == EINTR);
        
        if(readResult > 0) {
            // There was output, so write it to the console window's text storage
            NSString* string = [[NSString alloc] initWithBytesNoCopy:buffer length:readResult encoding:NSUTF8StringEncoding freeWhenDone:NO];
            if (string) {
                NSDictionary *attributes = [ConsoleViewController defaultTextAttributes];
                NSAttributedString* attributedString = [[NSAttributedString alloc] initWithString:string attributes:attributes];
                dispatch_sync(dispatch_get_main_queue(),^{
                    [self.consoleTextStorage appendAttributedString:attributedString];
                });
            }
            

            // Write to normal console output as well
            write(fdOrig, buffer, readResult);
        }
    };
    
    // Read buffers
    static char stderrBuffer[4096];
    static char stdoutBuffer[4096];
    
    // Duplicate original file descriptors to allow writing to the standard console
    int stderrOrig = dup(fileno(stderr));
    int stdoutOrig = dup(fileno(stdout));
    
    // Create the pipes and divert the standard outputs
    NSPipe *stderrPipe = [NSPipe pipe];
    NSPipe *stdoutPipe = [NSPipe pipe];
    dup2(stderrPipe.fileHandleForWriting.fileDescriptor, fileno(stderr));
    dup2(stdoutPipe.fileHandleForWriting.fileDescriptor, fileno(stdout));
    
    // Set up dispatch event handlers to listen for output
    _stderrSource = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ, stderrPipe.fileHandleForReading.fileDescriptor, 0, dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_LOW, 0));
    _stdoutSource = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ, stdoutPipe.fileHandleForReading.fileDescriptor, 0, dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_LOW, 0));
    dispatch_source_set_event_handler(_stderrSource, ^{ handleConsoleEvent(stderrPipe, &stderrBuffer, stderrOrig); });
    dispatch_source_set_event_handler(_stdoutSource, ^{ handleConsoleEvent(stdoutPipe, &stdoutBuffer, stdoutOrig); });
    
    // Start listening
    dispatch_resume(_stderrSource);
    dispatch_resume(_stdoutSource);
}

- (void)setupWindow {
    // Create the OpenGL view and context
    // TODO: BEN See why background color is red when resizing window tall (it's likely due to default GL framebuffer color)
    _glView = [[EmuGLView alloc] init];
    _glView.delegate = self;
    
    // Create the Window
    NSWindowStyleMask styleMask = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
    _mainWindow = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 640, 480) styleMask:styleMask backing:NSBackingStoreBuffered defer:NO];
    _mainWindow.delegate = self;
    _mainWindow.contentView = _glView;
    _mainWindow.acceptsMouseMovedEvents = YES;
    // no window state in ~/Library/Saved Application State (PL-D49)
    _mainWindow.restorable = NO;
    [_mainWindow makeKeyAndOrderFront:_mainWindow];
    [_mainWindow center];
    
    // Set initial scale factor and view size
    _backingScaleFactor = _mainWindow.screen.backingScaleFactor;
    _glViewSize = _glView.bounds.size;
    
    [self setupScreenDPI];
}

- (void)setupScreenDPI {
    // Calculate screen DPI
    NSScreen *screen = _mainWindow.screen;
    NSDictionary *description = [screen deviceDescription];
    NSSize displayPixelSize = [[description objectForKey:NSDeviceSize] sizeValue];
    // Must multiply by the scale factor to account for retina displays
    displayPixelSize.width *= screen.backingScaleFactor;
    displayPixelSize.height *= screen.backingScaleFactor;
    CGSize displayPhysicalSize = CGDisplayScreenSize([[description objectForKey:@"NSScreenNumber"] unsignedIntValue]);
    screen_dpi = (int)(displayPixelSize.width / displayPhysicalSize.width) * 25.4f;
    NSLog(@"displayPixelSize: %@  displayPhysicalSize: %@  screen_dpi: %d", NSStringFromSize(displayPixelSize), NSStringFromSize(displayPhysicalSize), screen_dpi);
}

- (void)setupUIThread {
    [_mainWindow makeKeyAndOrderFront:_mainWindow];
    [[NSApplication sharedApplication] activateIgnoringOtherApps:YES];
    // Run the rendering in a dedicated thread
    _uiThread = [[NSThread alloc] initWithBlock:^{
        NSLog(@"Starting UI Loop");
        nulldc_ui_loop();
        NSLog(@"UI Loop ended");
        
        // Terminate emulator (must be done after UI Loop ends or it may crash, must be run on main thread or it will crash)
        [_sharedInstance performSelectorOnMainThread:@selector(uiThreadEnded) withObject:nil waitUntilDone:YES];
    }];
    
    // Ensure the thread has the highest priority for the best performance
    _uiThread.threadPriority = 1.0;
    _uiThread.qualityOfService = NSQualityOfServiceUserInteractive;
    [_uiThread start];
}

// Terminate emulator and safely exit the app (must be called from the main thread)
- (void)uiThreadEnded {
    // Ensure that the gl view's context is the current context for this thread or it may crash
    [_glView.openGLContext makeCurrentContext];

    // Terminate the emulator
    nulldc_term();
    _emulatorEnded = YES;

    if (_terminateAsked) {
        // Notify the app that it can safely exit now
        [NSApp replyToApplicationShouldTerminate:YES];
    } else {
        // The loop ended by itself (an exit key, a run that cannot go on): the application ends with it
        [NSApp terminate:nil];
    }
}

- (void)shutdownEmulator {
    // Shut down emulator
    if (virtualDreamcast && sh4_cpu->IsRunning()) {
        virtualDreamcast->Stop([] {
            // Stop the UI thread (which will then complete the shutdown, otherwise doing it here will crash)
            g_GUIRenderer->Stop();
        });
    } else {
        // Stop the UI thread (which will then complete the shutdown, otherwise doing it here will crash)
        if (g_GUIRenderer) {
            g_GUIRenderer->Stop();
        }
    }
}

#pragma mark - OS functions -

void os_LaunchFromURL(const string& url) {
    if (runctl_headless())
        return;
    NSString *urlString = [NSString stringWithUTF8String:url.c_str()];
    [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:urlString]];
}

// The title of the window (the speed line, once a second; the paused title of the debugger). Headless: nothing, as
// on Linux; with a window, on the main thread
void os_SetWindowText(const char * text) {
    if (runctl_headless() || !_sharedInstance)
        return;
    NSString *title = [NSString stringWithUTF8String:text];
    if (!title)
        return;
    dispatch_async(dispatch_get_main_queue(), ^{
        _sharedInstance.mainWindow.title = title;
    });
}

// Code that runs at the beginning of every frame
void os_DoEvents() {
    // Left empty on purpose as we have nothing to run here
}

// Handle controller input
void UpdateInputState(u32 port) {
    // TODO: BEN not sure what, if anything, we need here
}

// Called by libswirl to create the emulator window, after the configuration is read (nulldc_init). Headless: no
// window, and no NSApplication either. Otherwise the application is made here: the Dock icon, the menu, this
// delegate; then the window
void os_CreateWindow() {
    if (runctl_headless())
        return;
    NSApplication *app = [NSApplication sharedApplication];
    [app setActivationPolicy:NSApplicationActivationPolicyRegular];
    // NSApplication keeps its delegate weakly: _sharedInstance holds it
    _sharedInstance = [[AppDelegate alloc] init];
    [app setDelegate:_sharedInstance];
    [MainMenu create];
    [_sharedInstance setupWindow];
    [app activateIgnoringOtherApps:YES];
}

// Called by libswirl when OpenGL is initialized
bool os_gl_init(void* hwnd, void* hdc) {
    [_sharedInstance.glView.openGLContext makeCurrentContext];
    gl_resize();
    return true;
}

// Called on every frame to swap the buffer
bool os_gl_swap() {
    NSOpenGLContext *glContext = _sharedInstance.glView.openGLContext;
    [glContext makeCurrentContext];
    [glContext flushBuffer];
    
    return true;
}

// Called when the application terminates
void os_gl_term() {
    // Left empty on purpose as we have nothing to run here
}

static void gl_resize() {
    // Handle HDPI by multiplying the scale factor
    rend_resize(_glViewSize.width * _backingScaleFactor, _glViewSize.height * _backingScaleFactor);
}

void* libPvr_GetRenderTarget() {
    return (__bridge void*)_sharedInstance.mainWindow;
}

void* libPvr_GetRenderSurface() {
    return (__bridge void*)_sharedInstance.glView.openGLContext;
}

int get_mic_data(u8* buffer) {
    return 0;
}

int push_vmu_screen(u8* buffer) {
    return 0;
}

@end

#pragma mark - The start -

// The directories a run can keep its files in, cfg/cl.cpp chooses: the one of the executable, and
// ~/Library/Application Support/nulldc3 for --shared. For an executable inside an application bundle "the
// directory of the executable" is the directory that holds the bundle, never one inside it. Foundation only.
static void setup_paths() {
    HostDirs dirs;
    NSBundle *bundle = [NSBundle mainBundle];
    NSString *bundlePath = [[bundle bundlePath] stringByResolvingSymlinksInPath];
    NSString *executablePath = [[bundle executablePath] stringByResolvingSymlinksInPath];
    NSString *dir = nil;
    if (bundlePath && [[bundlePath pathExtension] caseInsensitiveCompare:@"app"] == NSOrderedSame)
        dir = [bundlePath stringByDeletingLastPathComponent];
    else if (executablePath)
        dir = [executablePath stringByDeletingLastPathComponent];
    if (dir.length > 0)
        dirs.exe_dir = [dir fileSystemRepresentation];
    else
        dirs.exe_dir_error = "the main bundle names no path";

    NSArray<NSURL *> *support = [[NSFileManager defaultManager] URLsForDirectory:NSApplicationSupportDirectory
                                                                       inDomains:NSUserDomainMask];
    if (support.count > 0) {
        NSURL *shared = [support.firstObject URLByAppendingPathComponent:@"nulldc3" isDirectory:YES];
        dirs.shared_config = dirs.shared_data = [shared.path fileSystemRepresentation];
    } else
        dirs.shared_error = "the system names no Application Support directory";

    // read behind the chosen directory in every mode
    NSString *resources = [bundle resourcePath];
    if (resources)
        dirs.data_ro.push_back([resources fileSystemRepresentation]);

    cl_SetHostDirs(dirs);
}

// Whether a --config item of the command line sets config:Headless, read ahead of the parser (cfg/cl.cpp, whose
// grammar this follows; the same scan as the Windows build's, windows/winmain.cpp): the alert is for a person who
// started the emulator, never for a headless run, also one whose command line is refused. The last item wins, as
// in the parser; a value is true as cfgLoadBool has it.
static bool argv_headless(int argc, char** argv) {
    bool headless = false;
    for (int i = 1; i < argc; i++) {
        string word = argv[i];
        if (word == "--" || word == "--help" || word == "--version")
            break;
        if (word != "--config")
            continue;
        while (i + 1 < argc) {
            string item = argv[++i];
            size_t colon = item.find(':');
            size_t equals = colon == string::npos ? string::npos : item.find('=', colon + 1);
            if (equals != string::npos) {
                auto trim = [](const string& t) {
                    size_t first = t.find_first_not_of(" \t\r\n\v\f"), last = t.find_last_not_of(" \t\r\n\v\f");
                    return first == string::npos ? string() : t.substr(first, last - first + 1);
                };
                string section = trim(item.substr(0, colon));
                string key = trim(item.substr(colon + 1, equals - colon - 1));
                string value = trim(item.substr(equals + 1));
                if (section == "config" && key == "Headless")
                    headless = !strcasecmp(value.c_str(), "yes") || !strcasecmp(value.c_str(), "true") ||
                               !strcasecmp(value.c_str(), "on") || !strcasecmp(value.c_str(), "1");
            }
            if (i + 1 < argc && !strcmp(argv[i + 1], ","))
                i++;
            else
                break;
        }
    }
    return headless;
}

// Nobody can read stderr: it is closed, or it is /dev/null (a start from the Finder). A terminal, a file, a pipe
// or a socket (a non-pty ssh session) can be read
static bool stderr_goes_nowhere() {
    struct stat err, null;
    if (fstat(STDERR_FILENO, &err) != 0)
        return true;
    return S_ISCHR(err.st_mode) && stat("/dev/null", &null) == 0 && err.st_rdev == null.st_rdev;
}

// LaunchServices started this process (the Finder, the Dock, open): it puts __CFBundleIdentifier into the
// environment, and the process is launchd's child
static bool started_by_launchservices() {
    const char* id = getenv("__CFBundleIdentifier");
    return (id && *id) || getppid() == 1;
}

// The alert of a refused command line, for a person who cannot read stderr: the only dialog of the emulator
static void refused_alert(const string& text) {
    @autoreleasepool {
        NSApplication *app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        [app activateIgnoringOtherApps:YES];
        NSAlert *alert = [[NSAlert alloc] init];
        alert.alertStyle = NSAlertStyleCritical;
        alert.messageText = @"nullDC";
        NSString *info = [NSString stringWithUTF8String:text.c_str()];
        alert.informativeText = info ? info : @"the command line was refused";
        [alert addButtonWithTitle:@"Exit"];
        [alert runModal];
    }
}

static volatile uintptr_t bad_address = 16;
#if HOST_CPU == CPU_X64
static volatile int zero = 0;
#endif

// NULLDC_TEST_FATAL=<kind>: the fault of that kind, right after nulldc_init. Every kind ends the run through the
// fatal policy of linux/common.cpp; a kind that did not end it says so on stderr and the run goes on
static void test_fatal() {
    const char* kind = getenv("NULLDC_TEST_FATAL");
    if (!kind || !*kind)
        return;
    string k = kind;
    fprintf(stderr, "NULLDC_TEST_FATAL: %s\n", kind);
    if (k == "segv")
        *(volatile int*)bad_address = 1;
    else if (k == "ill") {
#if HOST_CPU == CPU_ARM64
        __asm__ __volatile__(".inst 0x00000000");	// udf #0
#elif HOST_CPU == CPU_X64
        __asm__ __volatile__("ud2");
#endif
    }
    else if (k == "trap") {
        die("NULLDC_TEST_FATAL");
    }
    else if (k == "abort")
        abort();
#if HOST_CPU == CPU_X64
    else if (k == "div") {
        volatile int result = 1 / zero;
        (void)result;
    }
#endif
    else {
        fprintf(stderr, "NULLDC_TEST_FATAL: unknown kind %s\n", kind);
        return;
    }
    fprintf(stderr, "NULLDC_TEST_FATAL: %s did not end the run\n", kind);
}

// NULLDC_TEST_UI_REPORT=1: one line when main ends, what the run had of AppKit: an NSApplication (NSApp, which a
// read does not make), and its windows
static void test_ui_report() {
    const char* on = getenv("NULLDC_TEST_UI_REPORT");
    if (!on || strcmp(on, "1"))
        return;
    int app = NSApp != nil ? 1 : 0;
    int windows = app ? (int)[[NSApp windows] count] : 0;
    printf("nulldc-ui: app=%d windows=%d\n", app, windows);
    fflush(stdout);
}

extern "C" int nulldc_macos_main(int argc, char* argv[]) {
    // A process that a signal ends loses what stdio still holds: with a file or a pipe behind stdout that would be
    // the lines right before the fault
    setvbuf(stdout, nullptr, _IOLBF, 0);

    @autoreleasepool {
        setup_paths();
    }
    common_linux_setup();

    // Words that macOS and Xcode put on the command line of an application, not switches of the command line
    // (cfg/cl.cpp refuses every word that begins with a dash): -psn_<n>_<n>, the process serial number of a start
    // from the Finder on older macOS, and -NSDocumentRevisionsDebugMode YES|NO, which a run from Xcode gets
    // when its scheme has "Allow debugging when using document Versions Browser" (debugDocumentVersioning) on
    std::vector<char*> args;
    for (int i = 0; i < argc; i++) {
        if (i > 0 && strncmp(argv[i], "-psn_", 5) == 0)
            continue;
        if (i > 0 && strcmp(argv[i], "-NSDocumentRevisionsDebugMode") == 0) {
            if (i + 1 < argc)
                i++;
            continue;
        }
        args.push_back(argv[i]);
    }
    args.push_back(nullptr);
    int argn = (int)args.size() - 1;

    int result = nulldc_init(argn, args.data());
    if (result == rv_cli_finish) {
        // --help or --version: what was asked for is on stdout
        test_ui_report();
        return 0;
    }
    if (result == rv_cli_error) {
        // the command line was refused, cfg/cl.cpp has said why on stderr
        if (!argv_headless(argn, args.data()) && stderr_goes_nowhere() && started_by_launchservices())
            refused_alert(cl_ErrorText());
        test_ui_report();
        return 2;
    }
    if (result != 0) {
        fprintf(stderr, "nullDC initialization failed\n");
        test_ui_report();
        return 1;
    }

    // see fatal_error() in linux/common.cpp, the environment has the same switch for faults before this point
    if (cfgLoadBool("config", "Debug.BlockOnFault", false))
        common_linux_fault_block(true);

    test_fatal();

    if (runctl_headless()) {
        #if FEAT_HAS_SERIAL_TTY
        bool cleanup_pty_symlink = common_serial_pty_setup();
        #endif
        #if FEAT_HAS_NIXPROF
        install_prof_handler(1);
        #endif

        nulldc_ui_loop();

        #if FEAT_HAS_SERIAL_TTY
        if (cleanup_pty_symlink)
            unlink(settings.debug.VirtualSerialPortFile.c_str());
        #endif
        nulldc_term();
        test_ui_report();
        return runctl_exit_code();
    }

    // with a window: os_CreateWindow made the application; it ends in applicationWillTerminate
    @autoreleasepool {
        [NSApp run];
    }
    test_ui_report();
    return runctl_exit_code();
}

