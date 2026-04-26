// macOS-native HTTP POST for the high-score API. Uses NSURLSession from
// Foundation — no curl shell-out, no libcurl dependency. Compiled only for
// the macOS native target; the web build uses EM_JS fetch (in game.c) and
// the Windows target currently no-ops the submission.
//
// The session task is submitted asynchronously and its completion handler
// discards the response — this is fire-and-forget; the UI doesn't wait on
// the network. Errors are silently ignored (a failed submission shouldn't
// take down the death screen).
#import <Foundation/Foundation.h>

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
                (void)data; (void)resp; (void)err;
            }];
        [task resume];
    }
}
