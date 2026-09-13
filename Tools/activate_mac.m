#import <Cocoa/Cocoa.h>
#import <ApplicationServices/ApplicationServices.h>

static BOOL ScreenLocked(void) {
    NSDictionary *session = CFBridgingRelease(CGSessionCopyCurrentDictionary());
    return [session[@"CGSSessionScreenIsLocked"] boolValue];
}

// Give the benchmark the same foreground state as a normal app launch.
// Only the exact child process and bundle supplied by verify_mac are eligible.
int main(int argc, char **argv) {
    @autoreleasepool {
        if (argc != 3) return 2;
        pid_t pid = (pid_t)atoi(argv[1]);
        NSString *expected = [[NSString stringWithUTF8String:argv[2]] stringByStandardizingPath];
        NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:8];
        NSRunningApplication *app = nil;
        while (deadline.timeIntervalSinceNow > 0) {
            app = [NSRunningApplication runningApplicationWithProcessIdentifier:pid];
            if (app.finishedLaunching) break;
            [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.1]];
        }
        if (!app || app.terminated || ![app.bundleURL.path.stringByStandardizingPath isEqualToString:expected]) {
            fprintf(stderr, "Benchmark process absent, terminated or bundle mismatch (pid=%d).\n", pid);
            return 3;
        }
        BOOL wasActive = app.active;
        if (ScreenLocked()) {
            printf("foreground_before=%d foreground_after=%d locked=1\n", wasActive, app.active);
            return 5; // Render measurements are possible; interactive foreground is not.
        }
        [app activateWithOptions:NSApplicationActivateIgnoringOtherApps];
        deadline = [NSDate dateWithTimeIntervalSinceNow:2];
        while (!app.active && deadline.timeIntervalSinceNow > 0)
            [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.05]];
        BOOL locked = ScreenLocked();
        printf("foreground_before=%d foreground_after=%d locked=%d\n", wasActive, app.active, locked);
        return locked ? 5 : app.active ? 0 : 4;
    }
}
