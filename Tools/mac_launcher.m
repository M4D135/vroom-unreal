#import <Cocoa/Cocoa.h>
#import <CommonCrypto/CommonDigest.h>

static int Fail(NSString *message) {
    NSLog(@"Vroom: %@", message);
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = @"Vroom — lancement impossible";
    alert.informativeText = message;
    [alert runModal];
    return 1;
}

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        NSString *root = [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Application Support/Vroom"];
        NSData *data = [NSData dataWithContentsOfFile:[root stringByAppendingPathComponent:@"active.json"]];
        id decoded = data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:nil] : nil;
        NSDictionary *manifest = [decoded isKindOfClass:NSDictionary.class] ? decoded : nil;
        NSString *bundle = manifest[@"bundle"], *expected = manifest[@"executable_sha256"];
        if (![bundle isKindOfClass:NSString.class] || ![expected isKindOfClass:NSString.class])
            return Fail(@"Aucune version validée installée. Utiliser Tools/install_mac.py après validation.");
        NSString *executable = [bundle stringByAppendingPathComponent:@"Contents/MacOS/Vroom"];
        NSInputStream *stream = [NSInputStream inputStreamWithFileAtPath:executable];
        [stream open];
        CC_SHA256_CTX context; CC_SHA256_Init(&context);
        uint8_t buffer[65536]; NSInteger count;
        while ((count = [stream read:buffer maxLength:sizeof(buffer)]) > 0)
            CC_SHA256_Update(&context, buffer, (CC_LONG)count);
        [stream close];
        if (count < 0) return Fail(@"Le package validé est absent ou illisible. Réinstaller cette version.");
        unsigned char digest[CC_SHA256_DIGEST_LENGTH]; CC_SHA256_Final(digest, &context);
        NSMutableString *hash = [NSMutableString string];
        for (int i = 0; i < CC_SHA256_DIGEST_LENGTH; i++) [hash appendFormat:@"%02x", digest[i]];
        if (![hash isEqualToString:expected]) return Fail(@"Le binaire a changé depuis sa validation. Réinstaller le package validé.");
        // Keep the low-level memory tracker active throughout Cocoa startup.
        NSMutableArray *arguments = [NSMutableArray arrayWithArray:@[@"-n", bundle, @"--args", @"-LLM", @"-windowed", @"-ResX=1440", @"-ResY=900"]];
        for (int i = 1; i < argc; i++) [arguments addObject:[NSString stringWithUTF8String:argv[i]]];
        NSTask *task = [[NSTask alloc] init];
        task.executableURL = [NSURL fileURLWithPath:@"/usr/bin/open"];
        task.arguments = arguments;
        NSError *error = nil;
        if (![task launchAndReturnError:&error]) return Fail(error.localizedDescription);
        [task waitUntilExit];
        return task.terminationStatus;
    }
}
