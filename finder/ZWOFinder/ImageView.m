/* ---------------------------------------------------------------- */
//
// ImageView.m
// 
// Created by Christoph Birk on 2018-11-13
// Copyright © 2018 Christoph Birk. All rights reserved.
//
/* ---------------------------------------------------------------- */

#define DEBUG     1

/* ---------------------------------------------------------------- */

#import "ImageView.h"

/* ---------------------------------------------------------------- */

@implementation ImageView

/* ---------------------------------------------------------------- */

- (void)drawRect:(NSRect)rect
{
  const int zoom_f=1,img_x=0,img_y=0,radius=10; 
#if (DEBUG > 1)                        // 764,512
  fprintf(stderr,"%s(%.0f,%.0f)\n",PREFUN,rect.size.width,rect.size.height);
#endif
  assert([NSThread isMainThread]);     // NSView is mainThread only

  [super drawRect:rect];

#if (MAC_OS_X_VERSION_MIN_REQUIRED <= MAC_OS_X_VERSION_10_13)
  if (![self lockFocusIfCanDraw]) return;
#endif
  CGContextRef context = [[NSGraphicsContext currentContext] CGContext];

  CGFloat img_w = self.frame.size.width; 
  CGFloat img_h = self.frame.size.height;

  if (self.reticleMode) { CGFloat x,y,r,x1,x2,y1,y2; // cross-hair
    CGContextSetRGBStrokeColor(context,0,0,1,0.7);
    CGContextSetLineWidth(context,1);             // line widht=1
    CGContextSetLineDash(context,0.0,NULL,0);     // solid line
    CGContextBeginPath(context);
    x = (img_x + img_w/2)*zoom_f;
    y = (img_y + img_h/2)*zoom_f;
    r = 2.0*radius*sqrt(zoom_f);
    CGContextAddArc(context,x,y,  r,0,2.0*M_PI,0);  // target circle
    CGContextAddArc(context,x,y,2*r,0,2.0*M_PI,0);
    x1 = 1+img_x*zoom_f; x2 = (img_x+img_w)*zoom_f-2;
    y1 = 1+img_y*zoom_f; y2 = (img_y+img_h)*zoom_f-2;
    CGContextMoveToPoint(context,x,y1); CGContextAddLineToPoint(context,x,y2);
    CGContextMoveToPoint(context,x1,y); CGContextAddLineToPoint(context,x2,y);
    CGContextDrawPath(context,kCGPathStroke);
  }
#if (MAC_OS_X_VERSION_MIN_REQUIRED <= MAC_OS_X_VERSION_10_13)
  [self unlockFocus];
#endif
}

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */

@end

/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
/* ---------------------------------------------------------------- */
