// macOS-native HTTP POST for the high-score API. Uses NSURLSession from
// Foundation — no curl shell-out, no libcurl dependency. Compiled only for
// the macOS native target; the web build uses EM_JS fetch (in game.c) and
// the Windows target currently no-ops the submission.
//
// The session task is submitted asynchronously and its completion handler
// parses the JSON response for the rank field, then calls back into the C
// side via IronFistRankReceived (defined in game.c). UI doesn't block on
// the network — the rank arrives a frame or two after the POST and the
// death screen picks it up on the next frame.
#import <Foundation/Foundation.h>

extern void IronFistRankReceived(int rank);  // defined in src/game.c

void IronFistPostScoreMacOS(const char *jsonCStr) {
    if (!jsonCStr) return;
    @autoreleasepool {
        NSString *jsonStr = [NSString stringWithUTF8String:jsonCStr];
        if (!jsonStr) return;
        NSData   *body = [jsonStr dataUsingEncoding:NSUTF8StringEncoding];
        NSURL    *url  = [NSURL URLWithString:@"https://ironfist.ximg.app/api/scores"];
        NSMutableURLRequest *req = [NSMutableURLRequest requestWithURL:url];
        [req setHTTPMethod:@"POST"];
        [req setValue:@"application/json" forHTTPHeaderField:@"Content-Type"];
        [req setHTTPBody:body];
        NSURLSessionDataTask *task = [[NSURLSession sharedSession]
            dataTaskWithRequest:req
            completionHandler:^(NSData * _Nullable data,
                                NSURLResponse * _Nullable resp,
                                NSError * _Nullable err) {
                (void)resp; (void)err;
                if (!data) return;
                NSError *jerr = nil;
                NSDictionary *json = [NSJSONSerialization JSONObjectWithData:data
                                                                     options:0
                                                                       error:&jerr];
                if (json && !jerr && [json isKindOfClass:[NSDictionary class]]) {
                    NSNumber *rank = json[@"rank"];
                    if (rank && [rank isKindOfClass:[NSNumber class]]) {
                        // NSURLSession completion fires on a background queue;
                        // a single int write is fine to race here (worst case
                        // the death screen shows the rank one frame later).
                        IronFistRankReceived([rank intValue]);
                    }
                }
            }];
        [task resume];
    }
}
