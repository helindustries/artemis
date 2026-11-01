// A host program's Dock icon in coherence mode. Copies of this app, one per program with
// windows, carry the program's icon and name; each connects to the stream process over the Unix
// socket named in its Info.plist (ArtemisSocket), says which program it stands for
// (ArtemisProgram), asks for that program's windows whenever it is activated, and quits when the
// stream process closes the connection.

#import <AppKit/AppKit.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

@interface DockTile : NSObject <NSApplicationDelegate>
@property (nonatomic) int socket;
@property (nonatomic, copy) NSString* program;
@property (nonatomic, strong) dispatch_source_t reader;
@property (nonatomic) BOOL closing;
@property (nonatomic) CFAbsoluteTime activatedAt;
@end

// A Dock click on an inactive tile activates it and asks it to reopen; one click, one raise
static const CFAbsoluteTime REOPEN_AFTER_ACTIVATION_S = 0.5;

@implementation DockTile

- (void)send:(NSString*)command
{
    NSData* line = [[NSString stringWithFormat:@"%@ %@\n", command, self.program] dataUsingEncoding:NSUTF8StringEncoding];
    if (self.socket >= 0 && write(self.socket, line.bytes, line.length) != (ssize_t)line.length) {
        NSLog(@"Expected the stream process to take '%@', it did not: %s", command, strerror(errno));
    }
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification
{
    NSDictionary* info = NSBundle.mainBundle.infoDictionary;
    self.program = info[@"ArtemisProgram"];
    const char* path = [info[@"ArtemisSocket"] fileSystemRepresentation];

    struct sockaddr_un address = { .sun_family = AF_UNIX };
    self.socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (self.socket < 0 || path == NULL || strlen(path) >= sizeof(address.sun_path)) {
        NSLog(@"Expected a socket to the stream process at %s", path);
        self.closing = YES;
        [NSApp terminate:nil];
    }
    else {
        strcpy(address.sun_path, path);
        if (connect(self.socket, (struct sockaddr*)&address, sizeof(address)) != 0) {
            NSLog(@"Expected the stream process to listen at %s: %s", path, strerror(errno));
            self.closing = YES;
            [NSApp terminate:nil];
        }
        else {
            [self send:@"hello"];

            // The stream process only ever closes the connection: the program has no windows
            // left, or coherence mode ended
            self.reader = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ, self.socket, 0, dispatch_get_main_queue());
            dispatch_source_set_event_handler(self.reader, ^{
                char buffer[64];
                if (read(self.socket, buffer, sizeof(buffer)) <= 0) {
                    self.closing = YES;
                    [NSApp terminate:nil];
                }
            });
            dispatch_resume(self.reader);
        }
    }
}

- (void)applicationDidBecomeActive:(NSNotification*)notification
{
    self.activatedAt = CFAbsoluteTimeGetCurrent();
    [self send:@"raise"];
}

// The Dock's click on a tile that is active already: no activation comes then
- (BOOL)applicationShouldHandleReopen:(NSApplication*)sender hasVisibleWindows:(BOOL)visible
{
    if (CFAbsoluteTimeGetCurrent() - self.activatedAt > REOPEN_AFTER_ACTIVATION_S) {
        [self send:@"raise"];
    }
    return NO;
}

// Quit from the Dock closes the program's windows on the host (Start's ends coherence mode);
// the tile goes when the stream process closes its connection
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender
{
    NSApplicationTerminateReply reply = NSTerminateNow;
    if (!self.closing && self.socket >= 0) {
        [self send:@"quit"];
        reply = NSTerminateCancel;
    }
    return reply;
}

@end

int main(int argc, const char* argv[])
{
    @autoreleasepool {
        NSApplication* application = [NSApplication sharedApplication];
        DockTile* tile = [DockTile new];
        application.delegate = tile;
        [application setActivationPolicy:NSApplicationActivationPolicyRegular];
        [application run];
    }
    return 0;
}
