// Tools/mac_portable_launcher.m
#import <Cocoa/Cocoa.h>
#import <CommonCrypto/CommonDigest.h>

static int Fail(NSString *message) {
    NSLog(@"Vroom Portable Launcher: %@", message);
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = @"Vroom — lancement impossible";
    alert.informativeText = message;
    [alert runModal];
    return 1;
}

static NSString *FileHash(NSString *path) {
    NSInputStream *stream = [NSInputStream inputStreamWithFileAtPath:path];
    if (!stream) return nil;
    [stream open];

    CC_SHA256_CTX context;
    CC_SHA256_Init(&context);

    uint8_t buffer[65536];
    NSInteger count;
    while ((count = [stream read:buffer maxLength:sizeof(buffer)]) > 0) {
        CC_SHA256_Update(&context, buffer, (CC_LONG)count);
    }

    NSError *error = stream.streamError;
    [stream close];

    if (count < 0 || error) return nil;

    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256_Final(digest, &context);

    NSMutableString *hash = [NSMutableString string];
    for (int i = 0; i < CC_SHA256_DIGEST_LENGTH; i++) {
        [hash appendFormat:@"%02x", digest[i]];
    }
    return hash;
}

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        NSString *bundlePath = [NSBundle mainBundle].bundlePath;
        if (!bundlePath) {
            return Fail(@"Impossible de déterminer le chemin du bundle principal.");
        }

        NSString *childBundlePath = [bundlePath stringByAppendingPathComponent:@"Contents/SharedSupport/Vroom.app"];
        NSString *childExecutablePath = [childBundlePath stringByAppendingPathComponent:@"Contents/MacOS/Vroom"];

        if (![[NSFileManager defaultManager] isExecutableFileAtPath:childExecutablePath]) {
            return Fail([NSString stringWithFormat:@"Le fichier exécutable enfant est introuvable :\n%@", childExecutablePath]);
        }

        NSString *expectedHash = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"VroomGameSHA256"];
        NSCharacterSet *invalidHex = [[NSCharacterSet characterSetWithCharactersInString:@"0123456789abcdefABCDEF"] invertedSet];
        if (![expectedHash isKindOfClass:NSString.class] || expectedHash.length != 64 ||
            [expectedHash rangeOfCharacterFromSet:invalidHex].location != NSNotFound) {
            return Fail(@"Le manifeste du lanceur est incomplet. Extraire à nouveau l’archive du jeu.");
        }

        NSString *actualHash = FileHash(childExecutablePath);
        if (!actualHash) {
            return Fail(@"Impossible de calculer le SHA256 du binaire enfant.");
        }

        if (![actualHash.lowercaseString isEqualToString:expectedHash.lowercaseString]) {
            return Fail([NSString stringWithFormat:@"Échec de vérification d'intégrité SHA256.\nAttendu : %@\nObtenu : %@", expectedHash, actualHash]);
        }

        NSMutableArray *arguments = [NSMutableArray arrayWithArray:@[@"-n", childBundlePath, @"--args", @"-NoSplash", @"-LLM", @"-windowed", @"-ResX=1440", @"-ResY=900"]];

        for (int i = 1; i < argc; i++) {
            NSString *argument = [NSString stringWithUTF8String:argv[i]];
            if (!argument) return Fail(@"Un paramètre de lancement est illisible.");
            [arguments addObject:argument];
        }

        NSTask *task = [[NSTask alloc] init];
        task.executableURL = [NSURL fileURLWithPath:@"/usr/bin/open"];
        task.arguments = arguments;

        NSError *error = nil;
        if (![task launchAndReturnError:&error]) {
            return Fail([NSString stringWithFormat:@"Échec du lancement via /usr/bin/open : %@", error.localizedDescription]);
        }

        [task waitUntilExit];

        return task.terminationStatus;
    }
}
