/* ---------------------------------------------------------------- */
//
// AppDelegate.h
// 
// Created by Christoph Birk on 2018-09-04
// Copyright (c) 2018 Christoph Birk. All rights reserved.
//
/* ---------------------------------------------------------------- */

#import <Cocoa/Cocoa.h>

#import "Logger.h"
#import "ImageView.h"

/* ---------------------------------------------------------------- */

#define P_TITLE  "ZWO-Finder"

/* ---------------------------------------------------------------- */

extern Logger *main_logger;

/* ---------------------------------------------------------------- */

@interface AppDelegate : NSObject <NSApplicationDelegate>
{
  IBOutlet NSWindow *configWindow;
  IBOutlet NSTextField *edit_zwohost;
  IBOutlet NSButton *but_startup;
  // Main window
  IBOutlet NSWindow *window;
  IBOutlet NSButton *but_image,*but_video;
  IBOutlet NSPopUpButton *pop_binning,*pop_filter; 
  IBOutlet NSTextField *edit_fps,*edit_scale,*edit_temp,*edit_cool;
  IBOutlet NSTextField *edit_exptime,*edit_loop,*edit_object,*edit_comment;
  IBOutlet ImageView *view_image;
  IBOutlet NSButton *chk_reticle,*chk_loop,*chk_fits,*chk_flipx,*chk_flipy;
  // Preferences panel
  IBOutlet NSPanel  *panel_preferences;
  IBOutlet NSTextField *edit_fitspath,*edit_setpoint,*edit_window;
  IBOutlet NSTextField *edit_offset,*edit_gain,*edit_run;
  IBOutlet NSTextField *edit_wbred,*edit_wblue; 
  IBOutlet NSButton *chk_cooler,*chk_window,*chk_autostart;
  IBOutlet NSPopUpButton *pop_vbits;
  IBOutlet NSButton *but_calibrate;
}

// @property (assign) IBOutlet NSWindow *window,*configWindow;
@property (assign,atomic)    BOOL running,stop;
@property (retain,nonatomic) NSDate *lastImage;
@property (retain,nonatomic) NSString *fitsPath;

- (IBAction)action_startup:(id)sender;
- (IBAction)action_button:(id)sender;
- (IBAction)action_check:(id)sender;
- (IBAction)action_edit:(id)sender;
- (IBAction)action_binning:(id)sender;
- (IBAction)action_menu:(id)sender;
- (IBAction)action_pref:(id)sender;
- (IBAction)action_filter:(id)sender;

@end

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
