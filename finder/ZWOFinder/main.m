/* ---------------------------------------------------------------- */
//
// main.m
//  
// Created by Christoph Birk on 2018-09-04
// Copyright (c) 2018 Christoph Birk. All rights reserved.
//
// on "zwoserver" host:
//   copy libASICamera2.so,libEFWFilter.so to /usr/local/lib
//   .bashrc: export LD_LIBRARY_PATH=/usr/local/lib
//   install asi.rules,efw.rules /lib/udev/rules.d
//   -- open firewall port 53211
//    NUC/CentOS: System--Administration--Firewall (andor:jobs)
//    Rock64/Ubuntu: no firewall (rock64:rock)
//    RaspberryPi: no firewall (pi:_worker)
//    Minnow: no firewall (andor:jobs)
//
// 'nc' connects for testing from terminal
//
// todo cleanup Preferences -- HardHat
// todo move flipX/Y to Prefs ?Povilas
//
/* ---------------------------------------------------------------- */

#define DEBUG   1

#include "main.h"

/* ---------------------------------------------------------------- */

#import <Cocoa/Cocoa.h>

/* ---------------------------------------------------------------- */

/* -- change library name/location ---
  otool -L libASICamera2.dylib
    libASICamera2.dylib:
      /usr/local/lib/libusb-1.0.0.dylib (compatibility version 2.0.0, current version 2.0.0)
  install_name_tool -change /usr/local/lib/libusb-1.0.0.dylib @executable_path/../Frameworks/libusb-1.0.0.dylib libASICamera2.dylib
  otool -L libASICamera2.dylib
    libASICamera2.dylib:
      @executable_path/../Frameworks/libusb-1.0.0.dylib

  otool -D libASICamera2.dylib
    @loader_path/libASICamera2.dylib
  install_name_tool -id @rpath/libASICamera2.dylib libASICanera2.dylib
    @rpath/libASICamera2.dylib
 
  install_name_tool -id @rpath/libEFWFilter.dylib libEFWFilter.dylib

  Build Phases:
    copy phase: libASICamera2.dylib --> Frameworks
    copy phase: libusb-1.0.0.dylib --> Frameworks
    copy phase: EFWFilter.dylib --> Frameworks
  Build Settings:
    Runpath Search Path: @loader_path/../Frameworks
*/

/* -- change project name / duplicate project ---
  • In the Finder, duplicate the project folder to the desired location of your new project. Do not rename the .xcodeproj file name or any associated folders at this stage.
  — open Xcode —
  • In Xcode, rename the project. Select your project from the navigator pane (left pane). In the Utilities pane (right pane) rename your project, Accept the changes Xcode proposes.
  • In Xcode, rename the virtual folders
  • In Xcode, rename the schemes in "Manage Schemes", also rename any targets you may have.
  • If you're not using the default Bundle Identifier which contains the current PRODUCT_NAME at the end (so will update automatically), then change your Bundle Identifier to the new one you will be using for your duplicated project.
  Restart Xcode
  Check “old name” in settings (eg. Info.plist path)
*/

/* ---------------------------------------------------------------- */

int main(int argc, const char * argv[])
{
#if (DEBUG > 0)
  { char buf[128];
#ifdef NDEBUG
    sprintf(buf,"%s: Release",PREFUN);
#else
    sprintf(buf,"%s: Debug",PREFUN);
#endif
    if (sizeof(void*) == 8) strcat(buf,"/64-bit");
    assert(sizeof(long) == 8);         // require 64-bit
    assert(sizeof(int) == 4);          // LP64 data model
    fprintf(stderr,"%s\n",buf);
  }
#endif

  return NSApplicationMain(argc, argv);
}

/* ---------------------------------------------------------------- */

#if 0 // unused
void u_sort(u_short* zahl, int l, int r)  // quick-sort
{
  unsigned short x,h;
  int    i=l,j=r;
  
  if (j>i) {
    x = zahl[(i+j)/2];
    do {
      while (zahl[i] < x) i++;
      while (zahl[j] > x) j--;
      if (i<=j) {
        h         = zahl[i];
        zahl[i++] = zahl[j];
        zahl[j--] = h;
      }
    } while (!(i>j));
    u_sort(zahl,l,j);
    u_sort(zahl,i,r);
  }
}
#endif

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
