/* ---------------------------------------------------------------- */
//
// Notice.h
//
// Created by Christoph Birk on 2012-10-18
// Copyright 2012 Carnegie Observatories. All rights reserved.
//
/* ---------------------------------------------------------------- */

#import <Cocoa/Cocoa.h>

#define INCLUDE_NOTICE_H

/* ---------------------------------------------------------------- */

@interface Notice : NSWindowController <NSWindowDelegate>
{
}

@property (readonly,atomic) BOOL closed;

+ (void)initialize;
+ (void)fireTimer:(NSTimer*)theTimer;

+ (Notice*)openNotice:(const char*)text title:(const char*)title level:(int)level
              target:(id)object selector:(SEL)sel;
+ (Notice*)openNotice:(const char*)text title:(const char*)title time:(int)wait
                level:(int)level;
+ (Notice*)openNotice:(const char*)text title:(const char*)title time:(int)wait;
+ (Notice*)openNotice:(const char*)text title:(const char*)title;

#ifndef NDEBUG
- (void)check_me;
#endif
- (void)action_ok:(id)sender;
- (void)close;

@end

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
