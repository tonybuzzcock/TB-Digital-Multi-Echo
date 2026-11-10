// PanelView.mm
// The custom panel for TB Digital Multi Echo, laid out like the Multi-Echo's
// front panel: a silver faceplate between black end cheeks, a row of gold-capped
// knobs with orange skirts over black tick scales, and a lower rail with the
// input jacks and Echo switches, the ECHO 1-4 and REPEAT A-D push buttons, the
// output jacks and the Overload lamp. Original artwork (no maker's logo); the
// TB Electronic mark sits in the plugin strip along the bottom with Echo Rate,
// the footswitch functions, Dry, Output, the presets and the readout.
//
// Cocoa, manual retain/release, and only APIs that exist in Mac OS X 10.8.
// Everything is drawn in code; no image files.

#import <Cocoa/Cocoa.h>

#include "Editor.h"
#include "Params.h"
#include "Presets.h"

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace tbme;

namespace
{

enum CtlType { kCtlKnob, kCtlButton, kCtlSlide, kCtlRocker, kCtlSmallKnob };
enum ScaleType { kScale010, kScale505 };

struct Ctl
{
    int param;
    int type;
    CGFloat cx, cy, r;
    const char* label;      // title above the control; also the readout name
    const char* posA;       // button legend / rocker off / small knob left
    const char* posB;       // rocker on / small knob right
    int scale;
};

const CGFloat kCheek = 26.0;            // black end cheeks
const CGFloat kRailTop = 236.0, kRailBottom = 330.0;
const CGFloat kStripTop = 344.0;
const CGFloat kKnobY = 136.0, kButtonY = 296.0;

const Ctl kCtls[] = {
    // Knob row, left to right as on the unit
    { kVolume1,   kCtlKnob,      122, kKnobY, 27, "VOLUME",        0, 0, kScale010 },
    { kVolume2,   kCtlKnob,      254, kKnobY, 27, "VOLUME",        0, 0, kScale010 },
    { kReverb,    kCtlKnob,      386, kKnobY, 27, "REVERB",        0, 0, kScale010 },
    { kEchoVol,   kCtlKnob,      560, kKnobY, 32, "ECHO VOLUME",   0, 0, kScale010 },
    { kRepeatVol, kCtlKnob,      738, kKnobY, 22, "REPEAT VOLUME", 0, 0, kScale010 },
    { kBass,      kCtlKnob,      866, kKnobY, 22, "BASS",          0, 0, kScale505 },
    { kTreble,    kCtlKnob,      988, kKnobY, 22, "TREBLE",        0, 0, kScale505 },
    // Lower rail: the Echo switch beside each input
    { kChEcho1,   kCtlSlide,     152, kButtonY, 0, "ECHO 1",       0, 0, 0 },
    { kChEcho2,   kCtlSlide,     296, kButtonY, 0, "ECHO 2",       0, 0, 0 },
    // ECHO 1-4, REPEAT A-D push buttons
    { kEcho1,     kCtlButton,    432, kButtonY, 0, "ECHO 1",       "1", 0, 0 },
    { kEcho2,     kCtlButton,    486, kButtonY, 0, "ECHO 2",       "2", 0, 0 },
    { kEcho3,     kCtlButton,    540, kButtonY, 0, "ECHO 3",       "3", 0, 0 },
    { kEcho4,     kCtlButton,    594, kButtonY, 0, "ECHO 4",       "4", 0, 0 },
    { kRepeatA,   kCtlButton,    656, kButtonY, 0, "REPEAT A",     "A", 0, 0 },
    { kRepeatB,   kCtlButton,    710, kButtonY, 0, "REPEAT B",     "B", 0, 0 },
    { kRepeatC,   kCtlButton,    764, kButtonY, 0, "REPEAT C",     "C", 0, 0 },
    { kRepeatD,   kCtlButton,    818, kButtonY, 0, "REPEAT D",     "D", 0, 0 },
    // Plugin strip
    { kEchoRate,  kCtlSmallKnob, 214, 402, 16, "ECHO RATE",     "SLOW", "FAST", 0 },
    { kEchoSw,    kCtlRocker,    306, 404, 0,  "ECHO",          "OFF", "ON", 0 },
    { kReverbSw,  kCtlRocker,    374, 404, 0,  "REVERB",        "OFF", "ON", 0 },
    { kDry,       kCtlRocker,    450, 404, 0,  "DRY",           "OUT", "IN", 0 },
    { kOutput,    kCtlSmallKnob, 546, 402, 16, "OUTPUT",        "-24", "+24", 0 },
};
const int kNumCtls = (int) (sizeof (kCtls) / sizeof (kCtls[0]));

const CGFloat kButtonW = 44.0, kButtonH = 22.0;
const CGFloat kSlideW = 36.0, kSlideH = 16.0;

// Presets and readout in the strip
const NSRect kSaveRect = { { 616.0, 390.0 }, { 54.0, 24.0 } };
const CGFloat kSlotX0 = 680.0, kSlotPitch = 27.0, kSlotW = 24.0, kSlotY = 390.0, kSlotH = 24.0;
const NSRect kReadoutRect = { { 908.0, 380.0 }, { 262.0, 42.0 } };
inline NSRect slotRect (int k) { return NSMakeRect (kSlotX0 + k * kSlotPitch, kSlotY, kSlotW, kSlotH); }

// Overload lamp where the unit has its mains lamp
const NSRect kLampRect = { { 1096.0, 284.0 }, { 40.0, 24.0 } };
// Decorative jacks: inputs, echo out, output
const CGFloat kJackX[4] = { 76.0, 220.0, 902.0, 990.0 };

const double kFrameRate = 20.0;

// Pointer angle (radians, 0 = up, clockwise positive) for a 0..1 value.
inline CGFloat valueAngle (CGFloat v) { return (CGFloat) ((-150.0 + 300.0 * v) * M_PI / 180.0); }

inline NSPoint polar (CGFloat cx, CGFloat cy, CGFloat r, CGFloat a)
{
    return NSMakePoint (cx + r * std::sin (a), cy - r * std::cos (a));
}

NSColor* rgb (int r, int g, int b) { return [NSColor colorWithCalibratedRed: r / 255.0 green: g / 255.0 blue: b / 255.0 alpha: 1.0]; }
NSColor* rgba (int r, int g, int b, double a) { return [NSColor colorWithCalibratedRed: r / 255.0 green: g / 255.0 blue: b / 255.0 alpha: a]; }

NSFont* boldFont (CGFloat size)
{
    NSFont* f = [NSFont fontWithName: @"Helvetica-Bold" size: size];
    return f ? f : [NSFont boldSystemFontOfSize: size];
}

NSFont* plainFont (CGFloat size)
{
    NSFont* f = [NSFont fontWithName: @"Helvetica" size: size];
    return f ? f : [NSFont systemFontOfSize: size];
}

NSFont* lightFont (CGFloat size)
{
    NSFont* f = [NSFont fontWithName: @"Helvetica-Light" size: size];
    return f ? f : plainFont (size);
}

NSFont* firstFont (const char* const* names, int count, CGFloat size, NSFont* fallback)
{
    for (int i = 0; i < count; ++i)
    {
        NSFont* f = [NSFont fontWithName: [NSString stringWithUTF8String: names[i]] size: size];
        if (f) return f;
    }
    return fallback;
}

// Eurostile for the TB mark when it is installed (it cannot be bundled), else Helvetica.
NSFont* markBoldFont (CGFloat size)
{
    const char* const names[] = { "Eurostile-Bold", "EurostileBold", "Eurostile-Heavy", "EurostileLTStd-Bold", "EurostileT-Bold" };
    return firstFont (names, 5, size, boldFont (size));
}

NSFont* markPlainFont (CGFloat size)
{
    const char* const names[] = { "Eurostile", "Eurostile-Regular", "EurostileLTStd", "EurostileT-Regular" };
    return firstFont (names, 4, size, plainFont (size));
}

void drawText (const char* s, NSFont* font, NSColor* color, CGFloat cx, CGFloat cy)
{
    NSString* str = [NSString stringWithUTF8String: s];
    NSDictionary* attrs = [NSDictionary dictionaryWithObjectsAndKeys:
                             font, NSFontAttributeName, color, NSForegroundColorAttributeName, nil];
    const NSSize sz = [str sizeWithAttributes: attrs];
    [str drawAtPoint: NSMakePoint (cx - sz.width * 0.5, cy - sz.height * 0.5) withAttributes: attrs];
}

void drawTextLeft (const char* s, NSFont* font, NSColor* color, CGFloat x, CGFloat cy)
{
    NSString* str = [NSString stringWithUTF8String: s];
    NSDictionary* attrs = [NSDictionary dictionaryWithObjectsAndKeys:
                             font, NSFontAttributeName, color, NSForegroundColorAttributeName, nil];
    const NSSize sz = [str sizeWithAttributes: attrs];
    [str drawAtPoint: NSMakePoint (x, cy - sz.height * 0.5) withAttributes: attrs];
}

void strokeLine (CGFloat x0, CGFloat y0, CGFloat x1, CGFloat y1, CGFloat w)
{
    NSBezierPath* p = [NSBezierPath bezierPath];
    [p moveToPoint: NSMakePoint (x0, y0)];
    [p lineToPoint: NSMakePoint (x1, y1)];
    [p setLineWidth: w];
    [p stroke];
}

} // namespace

//==============================================================================
@interface TBMultiEchoPanelView : NSView
{
    EditorHost* host;
    float shown[kNumParams];
    float shownOverload;
    NSTimer* timer;
    int dragCtl;
    CGFloat dragStartY, dragStartValue;
    int readoutParam;
    bool saveMode;
    int activePreset;
    bool presetUsed[kNumPresetSlots];
    char status[64];
}
- (id) initWithFrame: (NSRect) frame host: (EditorHost*) h;
- (void) shutdown;
@end

@implementation TBMultiEchoPanelView

- (id) initWithFrame: (NSRect) frame host: (EditorHost*) h
{
    self = [super initWithFrame: frame];
    if (self)
    {
        host = h;
        for (int i = 0; i < kNumParams; ++i) shown[i] = host->getParam (i);
        shownOverload = 0.0f;
        dragCtl = -1;
        readoutParam = -1;
        saveMode = false;
        activePreset = -1;
        status[0] = 0;
        host->refreshPresets();
        for (int k = 0; k < kNumPresetSlots; ++k) presetUsed[k] = host->presetExists (k);
        timer = [[NSTimer timerWithTimeInterval: 1.0 / kFrameRate target: self selector: @selector (tick:)
                                       userInfo: nil repeats: YES] retain];
        [[NSRunLoop currentRunLoop] addTimer: timer forMode: NSDefaultRunLoopMode];
        [[NSRunLoop currentRunLoop] addTimer: timer forMode: NSEventTrackingRunLoopMode];
    }
    return self;
}

- (void) shutdown
{
    if (timer) { [timer invalidate]; [timer release]; timer = nil; }
    host = 0;
}

- (void) dealloc
{
    [self shutdown];
    [super dealloc];
}

- (BOOL) isFlipped { return YES; }
- (BOOL) isOpaque { return YES; }
- (BOOL) acceptsFirstMouse: (NSEvent*) e { (void) e; return YES; }

// Host-side changes (automation, presets) redraw the panel; the Overload lamp follows the audio.
- (void) tick: (NSTimer*) t
{
    (void) t;
    if (! host) return;
    bool changed = false;
    for (int i = 0; i < kNumParams; ++i)
    {
        const float v = host->getParam (i);
        if (v != shown[i]) { shown[i] = v; changed = true; }
    }
    if (changed) [self setNeedsDisplay: YES];
    const float ov = host->getOverload() > 0.3f ? 1.0f : 0.0f;
    if (ov != shownOverload)
    {
        shownOverload = ov;
        [self setNeedsDisplayInRect: NSInsetRect (kLampRect, -8.0, -8.0)];
    }
}

//------------------------------------------------------------------------------
// Drawing

- (void) drawBackground
{
    const NSRect b = [self bounds];
    NSColor* ink = [NSColor blackColor];

    // Black end cheeks
    [rgb (22, 22, 24) set];
    NSRectFill (NSMakeRect (0, 0, kCheek, kStripTop));
    NSRectFill (NSMakeRect (b.size.width - kCheek, 0, kCheek, kStripTop));
    NSGradient* cheekShine = [[[NSGradient alloc] initWithStartingColor: rgba (255, 255, 255, 0.16)
                                                            endingColor: rgba (255, 255, 255, 0.0)] autorelease];
    [cheekShine drawInRect: NSMakeRect (4, 0, 9, kStripTop) angle: 0.0];
    [cheekShine drawInRect: NSMakeRect (b.size.width - kCheek + 4, 0, 9, kStripTop) angle: 0.0];

    // Brushed silver faceplate (knob area) and the lower rail
    const CGFloat x0 = kCheek, w = b.size.width - 2.0 * kCheek;
    NSGradient* face = [[[NSGradient alloc] initWithStartingColor: rgb (214, 216, 219)
                                                      endingColor: rgb (186, 189, 193)] autorelease];
    [face drawInRect: NSMakeRect (x0, 0, w, kRailTop) angle: 90.0];
    // fine horizontal brushing
    for (CGFloat y = 3.0; y < kRailTop; y += 3.0)
    {
        [rgba (255, 255, 255, (((int) y / 3) % 3 == 0) ? 0.10 : 0.04) set];
        NSRectFill (NSMakeRect (x0, y, w, 1.0));
    }
    NSGradient* rail = [[[NSGradient alloc] initWithStartingColor: rgb (200, 203, 207)
                                                      endingColor: rgb (176, 180, 185)] autorelease];
    [rail drawInRect: NSMakeRect (x0, kRailTop, w, kRailBottom - kRailTop) angle: 90.0];
    // grooves above and below the rail, and the lip under it
    [rgb (54, 56, 59) set];
    NSRectFill (NSMakeRect (x0, kRailTop - 3.0, w, 3.0));
    NSRectFill (NSMakeRect (x0, kRailBottom, w, 3.0));
    [rgb (236, 238, 240) set];
    NSRectFill (NSMakeRect (x0, kRailTop, w, 1.0));
    NSGradient* lip = [[[NSGradient alloc] initWithStartingColor: rgb (150, 153, 158)
                                                     endingColor: rgb (205, 208, 212)] autorelease];
    [lip drawInRect: NSMakeRect (x0, kRailBottom + 3.0, w, kStripTop - kRailBottom - 3.0) angle: 90.0];

    // Plugin strip
    NSGradient* strip = [[[NSGradient alloc] initWithStartingColor: rgb (196, 199, 203)
                                                       endingColor: rgb (166, 170, 175)] autorelease];
    [strip drawInRect: NSMakeRect (0, kStripTop, b.size.width, b.size.height - kStripTop) angle: 90.0];
    [rgb (30, 30, 32) set];
    NSRectFill (NSMakeRect (0, kStripTop, b.size.width, 2.0));
    [rgb (238, 240, 242) set];
    NSRectFill (NSMakeRect (0, kStripTop + 2.0, b.size.width, 1.0));

    // Red-lined panels on the rail, as on the unit: inputs, buttons, outputs
    {
        const NSRect boxes[3] = {
            NSMakeRect (52.5, 274.5, 300.0, 44.0),
            NSMakeRect (402.5, 264.5, 446.0, 50.0),
            NSMakeRect (874.5, 274.5, 140.0, 44.0),
        };
        for (int i = 0; i < 3; ++i)
        {
            [NSGraphicsContext saveGraphicsState];
            NSRectClip (boxes[i]);
            [rgba (205, 60, 55, 0.20) set];
            for (CGFloat d = -60.0; d < boxes[i].size.width + 60.0; d += 5.0)
                strokeLine (boxes[i].origin.x + d, NSMaxY (boxes[i]), boxes[i].origin.x + d + 44.0, boxes[i].origin.y, 0.8);
            [NSGraphicsContext restoreGraphicsState];
            [rgb (196, 42, 38) set];
            NSBezierPath* p = [NSBezierPath bezierPathWithRect: boxes[i]];
            [p setLineWidth: 1.0];
            [p stroke];
        }
    }

    // Rail lettering
    drawText ("INPUT", boldFont (9.5), ink, kJackX[0], 260);
    drawText ("INPUT", boldFont (9.5), ink, kJackX[1], 260);
    drawText ("ECHO OUT", boldFont (9.5), ink, kJackX[2], 260);
    drawText ("OUTPUT", boldFont (9.5), ink, kJackX[3], 260);
    drawText ("OVERLOAD", boldFont (9.5), ink, NSMidX (kLampRect), 260);
    drawText ("1", boldFont (19.0), rgb (200, 30, 30), kJackX[0] + 30.0, kButtonY + 1.0);
    drawText ("2", boldFont (19.0), rgb (200, 30, 30), kJackX[1] + 30.0, kButtonY + 1.0);

    // ECHO and REPEAT brackets over the buttons, with the button legends
    [ink set];
    {
        const CGFloat yb = 258.5, l1 = 410.0, r1 = 616.0, l2 = 634.0, r2 = 840.0;
        strokeLine (l1, yb + 6, l1, yb, 1.0); strokeLine (l1, yb, 486, yb, 1.0);
        strokeLine (540, yb, r1, yb, 1.0);  strokeLine (r1, yb, r1, yb + 6, 1.0);
        strokeLine (l2, yb + 6, l2, yb, 1.0); strokeLine (l2, yb, 700, yb, 1.0);
        strokeLine (774, yb, r2, yb, 1.0);  strokeLine (r2, yb, r2, yb + 6, 1.0);
        drawText ("ECHO", boldFont (12.0), ink, 513, yb);
        drawText ("REPEAT", boldFont (12.0), ink, 737, yb);
    }
    for (int i = 0; i < kNumCtls; ++i)
        if (kCtls[i].type == kCtlButton)
            drawText (kCtls[i].posA, boldFont (9.0), ink, kCtls[i].cx, 271);

    // Nameplate where the unit has its model name
    drawText ("DIGITAL", lightFont (13.0), ink, 1106, 92);
    drawText ("MULTI ECHO", lightFont (20.0), ink, 1106, 114);
    [rgb (60, 62, 65) set];
    {
        NSBezierPath* p = [NSBezierPath bezierPathWithRect: NSMakeRect (1052.5, 130.5, 108.0, 1.0)];
        [p fill];
    }
    drawText ("UNIT", lightFont (11.0), ink, 1106, 146);

    // Owner's mark in the strip's corner: red TB over "Electronic", straight on the silver
    drawText ("TB", markBoldFont (28.0), rgb (200, 30, 30), 76, 385);
    drawText ("Electronic", markPlainFont (12.0), ink, 76, 408);
}

- (void) drawJackAt: (CGFloat) cx black: (bool) black
{
    const CGFloat cy = kButtonY;
    // Red (or black) plastic bush, chrome nut, dark hole
    NSBezierPath* bush = [NSBezierPath bezierPathWithOvalInRect: NSMakeRect (cx - 13, cy - 13, 26, 26)];
    NSGradient* bg = black
        ? [[[NSGradient alloc] initWithStartingColor: rgb (70, 70, 72) endingColor: rgb (14, 14, 16)] autorelease]
        : [[[NSGradient alloc] initWithStartingColor: rgb (232, 70, 62) endingColor: rgb (140, 18, 16)] autorelease];
    [bg drawInBezierPath: bush angle: -60.0];
    NSBezierPath* nut = [NSBezierPath bezierPathWithOvalInRect: NSMakeRect (cx - 8.5, cy - 8.5, 17, 17)];
    NSGradient* ng = [[[NSGradient alloc] initWithStartingColor: rgb (246, 247, 249) endingColor: rgb (130, 133, 138)] autorelease];
    [ng drawInBezierPath: nut angle: -60.0];
    [rgb (8, 8, 9) set];
    [[NSBezierPath bezierPathWithOvalInRect: NSMakeRect (cx - 4.5, cy - 4.5, 9, 9)] fill];
}

- (void) drawLamp
{
    const bool lit = shownOverload > 0.5f;
    NSBezierPath* bezel = [NSBezierPath bezierPathWithRoundedRect: NSInsetRect (kLampRect, -4, -4) xRadius: 2 yRadius: 2];
    [rgb (20, 20, 22) set];
    [bezel fill];
    NSBezierPath* p = [NSBezierPath bezierPathWithRect: kLampRect];
    NSGradient* g = lit
        ? [[[NSGradient alloc] initWithStartingColor: rgb (255, 120, 90) endingColor: rgb (226, 30, 20)] autorelease]
        : [[[NSGradient alloc] initWithStartingColor: rgb (120, 40, 36) endingColor: rgb (70, 18, 16)] autorelease];
    [g drawInBezierPath: p relativeCenterPosition: NSMakePoint (0.0, 0.0)];
    // grille lines
    [rgba (0, 0, 0, 0.25) set];
    for (CGFloat x = NSMinX (kLampRect) + 4.0; x < NSMaxX (kLampRect); x += 4.0)
        strokeLine (x, NSMinY (kLampRect), x, NSMaxY (kLampRect), 0.6);
}

- (void) drawScaleForCtl: (const Ctl&) c
{
    NSColor* ink = [NSColor blackColor];
    const CGFloat numR = c.r + (c.r > 24.0 ? 17.0 : 14.0);
    for (int i = 0; i < 11; ++i)
    {
        const CGFloat a = valueAngle (i / 10.0);
        NSBezierPath* t = [NSBezierPath bezierPath];
        [t moveToPoint: polar (c.cx, c.cy, c.r + 3.0, a)];
        [t lineToPoint: polar (c.cx, c.cy, c.r + 8.5, a)];
        [t setLineWidth: 2.6];
        [ink set];
        [t stroke];

        char num[4];
        if (c.scale == kScale505) std::snprintf (num, sizeof (num), "%d", i < 5 ? 5 - i : i - 5);
        else                      std::snprintf (num, sizeof (num), "%d", i);
        const NSPoint p = polar (c.cx, c.cy, numR, a);
        drawText (num, plainFont (9.0), ink, p.x, p.y);
    }
    if (c.scale == kScale505)
    {
        const NSPoint top = polar (c.cx, c.cy, numR, 0.0);
        drawText ("\xe2\x80\x93", plainFont (9.0), ink, top.x - 7.5, top.y);   // en dash
        drawText ("+", plainFont (9.0), ink, top.x + 7.5, top.y);
    }
    if (c.param == kEchoVol)
    {
        const NSPoint p0 = polar (c.cx, c.cy, numR, valueAngle (0.0));
        drawText ("ECHO OFF", plainFont (8.5), ink, p0.x - 30.0, p0.y + 9.0);
    }
}

// Gold-capped knob with an orange skirt and a black pointer line.
- (void) drawKnobAt: (NSPoint) ctr radius: (CGFloat) r value: (CGFloat) v
{
    // Drop shadow
    [NSGraphicsContext saveGraphicsState];
    NSShadow* sh = [[[NSShadow alloc] init] autorelease];
    [sh setShadowOffset: NSMakeSize (1.5, -2.5)];
    [sh setShadowBlurRadius: 4.0];
    [sh setShadowColor: [NSColor colorWithCalibratedWhite: 0.0 alpha: 0.45]];
    [sh set];
    [rgb (190, 96, 20) set];
    [[NSBezierPath bezierPathWithOvalInRect: NSMakeRect (ctr.x - r, ctr.y - r, 2 * r, 2 * r)] fill];
    [NSGraphicsContext restoreGraphicsState];

    // Orange skirt with fine fluting
    NSBezierPath* skirt = [NSBezierPath bezierPathWithOvalInRect: NSMakeRect (ctr.x - r, ctr.y - r, 2 * r, 2 * r)];
    NSGradient* sg = [[[NSGradient alloc] initWithStartingColor: rgb (255, 178, 70) endingColor: rgb (196, 92, 14)] autorelease];
    [sg drawInBezierPath: skirt angle: -60.0];
    const int flutes = 36;
    for (int k = 0; k < flutes; ++k)
    {
        const CGFloat a0 = (CGFloat) (2.0 * M_PI * k / flutes);
        const CGFloat lit = 0.5 + 0.5 * std::cos (a0 + 0.8);
        [rgba (255, 236, 200, 0.10 + 0.25 * lit) set];
        strokeLine (polar (ctr.x, ctr.y, r * 0.80, a0).x, polar (ctr.x, ctr.y, r * 0.80, a0).y,
                    polar (ctr.x, ctr.y, r * 0.98, a0).x, polar (ctr.x, ctr.y, r * 0.98, a0).y, 1.0);
    }
    [rgb (150, 70, 10) set];
    [skirt setLineWidth: 0.8];
    [skirt stroke];

    // Gold cap with turned rings and a highlight
    const CGFloat rc = r * 0.74;
    NSBezierPath* capPath = [NSBezierPath bezierPathWithOvalInRect: NSMakeRect (ctr.x - rc, ctr.y - rc, 2 * rc, 2 * rc)];
    NSGradient* cap = [[[NSGradient alloc] initWithColorsAndLocations:
                           rgb (255, 244, 196), 0.0, rgb (226, 190, 104), 0.45, rgb (160, 122, 48), 1.0, nil] autorelease];
    [cap drawInBezierPath: capPath relativeCenterPosition: NSMakePoint (-0.35, -0.45)];
    for (int k = 1; k <= 4; ++k)
    {
        const CGFloat rr = rc * k / 5.0;
        NSBezierPath* ring = [NSBezierPath bezierPathWithOvalInRect: NSMakeRect (ctr.x - rr, ctr.y - rr, 2 * rr, 2 * rr)];
        [rgba (120, 86, 30, 0.18) set];
        [ring setLineWidth: 0.6];
        [ring stroke];
    }
    [rgb (120, 88, 32) set];
    [capPath setLineWidth: 0.8];
    [capPath stroke];

    // Black pointer line
    const CGFloat a = valueAngle (v);
    NSBezierPath* line = [NSBezierPath bezierPath];
    [line moveToPoint: polar (ctr.x, ctr.y, r * 0.10, a)];
    [line lineToPoint: polar (ctr.x, ctr.y, r * 0.96, a)];
    [line setLineWidth: r > 24.0 ? 3.0 : 2.4];
    [line setLineCapStyle: NSRoundLineCapStyle];
    [rgb (16, 14, 12) set];
    [line stroke];
}

// Black latching push button: raised when out, pressed down when in, with a small lamp above.
- (void) drawButton: (const Ctl&) c
{
    const bool in = isOn (shown[c.param]);
    const NSRect well = NSMakeRect (c.cx - kButtonW * 0.5 - 2, c.cy - kButtonH * 0.5 - 2, kButtonW + 4, kButtonH + 4);
    [rgb (16, 16, 18) set];
    [[NSBezierPath bezierPathWithRoundedRect: well xRadius: 2 yRadius: 2] fill];

    const CGFloat inset = in ? 2.0 : 0.0;
    NSRect cap = NSMakeRect (c.cx - kButtonW * 0.5 + inset, c.cy - kButtonH * 0.5 + inset, kButtonW - 2 * inset, kButtonH - 2 * inset);
    if (! in) cap.origin.y -= 1.0;
    NSBezierPath* cp = [NSBezierPath bezierPathWithRoundedRect: cap xRadius: 2 yRadius: 2];
    [NSGraphicsContext saveGraphicsState];
    if (! in)
    {
        NSShadow* sh = [[[NSShadow alloc] init] autorelease];
        [sh setShadowOffset: NSMakeSize (0.0, -2.0)];
        [sh setShadowBlurRadius: 2.0];
        [sh setShadowColor: [NSColor colorWithCalibratedWhite: 0.0 alpha: 0.7]];
        [sh set];
    }
    NSGradient* g = in
        ? [[[NSGradient alloc] initWithStartingColor: rgb (18, 18, 20) endingColor: rgb (34, 34, 37)] autorelease]
        : [[[NSGradient alloc] initWithStartingColor: rgb (78, 79, 83) endingColor: rgb (24, 24, 26)] autorelease];
    [g drawInBezierPath: cp angle: -90.0];
    [NSGraphicsContext restoreGraphicsState];
    if (! in)
    {
        [rgba (255, 255, 255, 0.22) set];
        NSRectFill (NSMakeRect (NSMinX (cap) + 3, NSMinY (cap) + 1.5, cap.size.width - 6, 1.0));
    }
    [rgb (8, 8, 9) set];
    [cp setLineWidth: 0.8];
    [cp stroke];

    // Lamp: lit while the button is in
    const NSRect led = NSMakeRect (c.cx + kButtonW * 0.5 - 9.0, c.cy - kButtonH * 0.5 - 11.0, 6.0, 6.0);
    NSBezierPath* lp = [NSBezierPath bezierPathWithOvalInRect: led];
    [(in ? rgb (255, 64, 40) : rgb (96, 40, 38)) set];
    [lp fill];
    [rgb (40, 14, 12) set];
    [lp setLineWidth: 0.6];
    [lp stroke];
}

// Black slide switch: the knob sits right (towards ON) when the channel feeds the echo.
- (void) drawSlide: (const Ctl&) c
{
    NSColor* ink = [NSColor blackColor];
    const bool on = isOn (shown[c.param]);
    const NSRect body = NSMakeRect (c.cx - kSlideW * 0.5, c.cy - kSlideH * 0.5, kSlideW, kSlideH);
    NSBezierPath* bp = [NSBezierPath bezierPathWithRoundedRect: body xRadius: 2 yRadius: 2];
    [rgb (14, 14, 16) set];
    [bp fill];
    const CGFloat kw = 16.0;
    const NSRect knob = NSMakeRect (on ? NSMaxX (body) - kw - 2.0 : NSMinX (body) + 2.0, NSMinY (body) + 2.0, kw, kSlideH - 4.0);
    NSGradient* g = [[[NSGradient alloc] initWithStartingColor: rgb (92, 94, 98) endingColor: rgb (40, 41, 44)] autorelease];
    [g drawInRect: knob angle: -90.0];
    [rgba (255, 255, 255, 0.25) set];
    for (int k = 1; k < 4; ++k) NSRectFill (NSMakeRect (NSMinX (knob) + 4.0 * k, NSMinY (knob) + 2.0, 1.0, knob.size.height - 4.0));
    drawText ("ECHO", boldFont (9.5), ink, c.cx, c.cy - 18.0);
    drawTextLeft ("ON", boldFont (9.0), ink, NSMaxX (body) + 6.0, c.cy);
}

// Rocker switch: the pressed-in end is the active position.
- (void) drawRocker: (const Ctl&) c
{
    NSColor* ink = [NSColor blackColor];
    const bool on = isOn (shown[c.param]);
    const CGFloat w = 20.0, h = 32.0;
    const NSRect bezel = NSMakeRect (c.cx - w * 0.5 - 4, c.cy - h * 0.5 - 4, w + 8, h + 8);

    NSBezierPath* bz = [NSBezierPath bezierPathWithRoundedRect: bezel xRadius: 4 yRadius: 4];
    [rgb (35, 36, 38) set];
    [bz fill];

    const NSRect top = NSMakeRect (c.cx - w * 0.5, c.cy - h * 0.5, w, h * 0.5);
    const NSRect bot = NSMakeRect (c.cx - w * 0.5, c.cy, w, h * 0.5);
    NSGradient* raised = [[[NSGradient alloc] initWithStartingColor: rgb (120, 122, 126) endingColor: rgb (66, 68, 71)] autorelease];
    NSGradient* pressed = [[[NSGradient alloc] initWithStartingColor: rgb (40, 41, 43) endingColor: rgb (58, 60, 63)] autorelease];
    [(on ? pressed : raised) drawInRect: top angle: on ? 90.0 : -90.0];
    [(on ? raised : pressed) drawInRect: bot angle: on ? 90.0 : -90.0];

    [[NSColor colorWithCalibratedWhite: 0.92 alpha: 1.0] set];
    NSRectFill (NSMakeRect (c.cx - 1.0, c.cy - h * 0.5 + 4, 2.0, 7.0));
    NSBezierPath* ring = [NSBezierPath bezierPathWithOvalInRect: NSMakeRect (c.cx - 3.5, c.cy + h * 0.5 - 11, 7, 7)];
    [ring setLineWidth: 1.5];
    [ring stroke];

    drawText (c.label, boldFont (9.5), ink, c.cx, c.cy - h * 0.5 - 13.0);
    drawTextLeft (c.posB, boldFont (8.5), ink, c.cx + w * 0.5 + 7.0, c.cy - h * 0.25);
    drawTextLeft (c.posA, boldFont (8.5), ink, c.cx + w * 0.5 + 7.0, c.cy + h * 0.25);
}

- (void) drawReadout
{
    const NSRect box = kReadoutRect;
    NSBezierPath* p = [NSBezierPath bezierPathWithRoundedRect: box xRadius: 4 yRadius: 4];
    NSGradient* glow = [[[NSGradient alloc] initWithStartingColor: rgb (206, 255, 120)
                                                      endingColor: rgb (150, 232, 70)] autorelease];
    [glow drawInBezierPath: p relativeCenterPosition: NSMakePoint (0.0, 0.0)];
    [rgb (60, 62, 65) set];
    [p setLineWidth: 1.0];
    [p stroke];

    if (status[0])
    {
        drawText (status, boldFont (14.0), [NSColor blackColor], NSMidX (box), NSMidY (box));
    }
    else if (readoutParam >= 0 && host)
    {
        char disp[16] = { 0 };
        host->getParamDisplay (readoutParam, disp);
        const char* name = 0;
        for (int i = 0; i < kNumCtls; ++i)
            if (kCtls[i].param == readoutParam) { name = kCtls[i].label; break; }
        if (readoutParam == kVolume1) name = "VOLUME 1";
        if (readoutParam == kVolume2) name = "VOLUME 2";
        if (readoutParam == kEchoSw) name = "FOOTSW ECHO";
        if (readoutParam == kReverbSw) name = "FOOTSW REVERB";
        if (readoutParam == kChEcho1) name = "INPUT 1 ECHO";
        if (readoutParam == kChEcho2) name = "INPUT 2 ECHO";
        const char* unit = readoutParam == kEchoRate ? " MS" : (readoutParam == kOutput ? " DB" : "");
        char text[96];
        std::snprintf (text, sizeof (text), "%s   %s%s", name ? name : "", disp, unit);
        for (char* q = text; *q; ++q) if (*q >= 'a' && *q <= 'z') *q = (char) (*q - 'a' + 'A');
        drawText (text, boldFont (14.0), [NSColor blackColor], NSMidX (box), NSMidY (box));
    }
    else
    {
        drawText ("TB DIGITAL MULTI ECHO", boldFont (14.0), [NSColor blackColor], NSMidX (box), NSMidY (box));
    }
}

- (void) drawPresets
{
    NSColor* ink = [NSColor blackColor];
    drawText ("PRESETS", boldFont (9.5), ink, (NSMinX (kSaveRect) + NSMaxX (slotRect (kNumPresetSlots - 1))) * 0.5, NSMinY (kSaveRect) - 12.0);

    NSBezierPath* sp = [NSBezierPath bezierPathWithRoundedRect: kSaveRect xRadius: 3 yRadius: 3];
    if (saveMode)
    {
        [rgb (200, 30, 30) set];
        [sp fill];
    }
    else
    {
        NSGradient* g = [[[NSGradient alloc] initWithStartingColor: rgb (232, 234, 236) endingColor: rgb (160, 163, 167)] autorelease];
        [g drawInBezierPath: sp angle: -90.0];
    }
    [rgb (40, 41, 43) set];
    [sp setLineWidth: 1.0];
    [sp stroke];
    drawText ("SAVE", boldFont (11.0), saveMode ? [NSColor whiteColor] : ink, NSMidX (kSaveRect), NSMidY (kSaveRect));

    for (int k = 0; k < kNumPresetSlots; ++k)
    {
        const NSRect r = slotRect (k);
        NSBezierPath* bp = [NSBezierPath bezierPathWithRoundedRect: r xRadius: 3 yRadius: 3];
        NSColor* fill = (k == activePreset) ? rgb (255, 150, 40) : (presetUsed[k] ? rgb (46, 47, 50) : rgb (214, 216, 219));
        NSColor* text = (k == activePreset) ? ink : (presetUsed[k] ? [NSColor whiteColor] : rgb (120, 122, 126));
        [fill set];
        [bp fill];
        [rgb (40, 41, 43) set];
        [bp setLineWidth: 1.0];
        [bp stroke];
        char num[4];
        std::snprintf (num, sizeof (num), "%d", k + 1);
        drawText (num, boldFont (12.0), text, NSMidX (r), NSMidY (r));
    }
}

- (void) drawRect: (NSRect) dirty
{
    (void) dirty;
    NSColor* ink = [NSColor blackColor];
    [self drawBackground];
    for (int j = 0; j < 4; ++j) [self drawJackAt: kJackX[j] black: false];
    [self drawLamp];

    for (int i = 0; i < kNumCtls; ++i)
    {
        const Ctl& c = kCtls[i];
        switch (c.type)
        {
            case kCtlKnob:
            {
                [self drawScaleForCtl: c];
                [self drawKnobAt: NSMakePoint (c.cx, c.cy) radius: c.r value: shown[c.param]];
                const CGFloat top = c.cy - c.r - (c.r > 24.0 ? 40.0 : 35.0);
                drawText (c.label, boldFont (10.5), ink, c.cx, top);
                break;
            }
            case kCtlButton: [self drawButton: c]; break;
            case kCtlSlide:  [self drawSlide: c];  break;
            case kCtlRocker: [self drawRocker: c]; break;
            case kCtlSmallKnob:
            {
                [self drawKnobAt: NSMakePoint (c.cx, c.cy) radius: c.r value: shown[c.param]];
                drawText (c.label, boldFont (9.5), ink, c.cx, c.cy - c.r - 13.0);
                drawText (c.posA, plainFont (8.5), ink, c.cx - c.r - 16.0, c.cy + c.r - 2.0);
                drawText (c.posB, plainFont (8.5), ink, c.cx + c.r + 16.0, c.cy + c.r - 2.0);
                break;
            }
            default: break;
        }
    }
    drawText ("FOOTSWITCH", boldFont (8.5), ink, 340, 357);
    [self drawPresets];
    [self drawReadout];
}

//------------------------------------------------------------------------------
// Mouse

- (int) ctlAtPoint: (NSPoint) p
{
    for (int i = 0; i < kNumCtls; ++i)
    {
        const Ctl& c = kCtls[i];
        switch (c.type)
        {
            case kCtlRocker:
                if (std::fabs (p.x - c.cx) < 18 && std::fabs (p.y - c.cy) < 22) return i;
                break;
            case kCtlButton:
                if (std::fabs (p.x - c.cx) < kButtonW * 0.5 + 2 && std::fabs (p.y - c.cy) < kButtonH * 0.5 + 4) return i;
                break;
            case kCtlSlide:
                if (std::fabs (p.x - c.cx) < kSlideW * 0.5 + 14 && std::fabs (p.y - c.cy) < kSlideH * 0.5 + 6) return i;
                break;
            default:
            {
                const CGFloat dx = p.x - c.cx, dy = p.y - c.cy;
                if (dx * dx + dy * dy < (c.r + 8) * (c.r + 8)) return i;
                break;
            }
        }
    }
    return -1;
}

- (void) setParam: (int) param value: (float) v
{
    if (! host) return;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    host->guiSetParam (param, v);
    shown[param] = host->getParam (param);
    readoutParam = param;
    [self setNeedsDisplay: YES];
}

// SAVE button and preset slots. Returns YES if the click was handled here.
- (BOOL) presetClick: (NSPoint) p
{
    if (NSPointInRect (p, kSaveRect))
    {
        saveMode = ! saveMode;
        std::snprintf (status, sizeof (status), "%s", saveMode ? "SAVE: CLICK A SLOT 1-8" : "");
        [self setNeedsDisplay: YES];
        return YES;
    }
    for (int k = 0; k < kNumPresetSlots; ++k)
    {
        if (! NSPointInRect (p, slotRect (k))) continue;
        if (saveMode)
        {
            const bool ok = host->savePreset (k);
            saveMode = false;
            host->refreshPresets();
            for (int j = 0; j < kNumPresetSlots; ++j) presetUsed[j] = host->presetExists (j);
            if (ok) activePreset = k;
            std::snprintf (status, sizeof (status), ok ? "PRESET %d SAVED" : "COULD NOT SAVE PRESET %d", k + 1);
        }
        else
        {
            host->refreshPresets();
            for (int j = 0; j < kNumPresetSlots; ++j) presetUsed[j] = host->presetExists (j);
            if (presetUsed[k] && host->loadPreset (k))
            {
                for (int j = 0; j < kNumParams; ++j) shown[j] = host->getParam (j);
                activePreset = k;
                std::snprintf (status, sizeof (status), "PRESET %d LOADED", k + 1);
            }
            else
                std::snprintf (status, sizeof (status), "PRESET %d IS EMPTY", k + 1);
        }
        [self setNeedsDisplay: YES];
        return YES;
    }
    return NO;
}

- (void) mouseDown: (NSEvent*) e
{
    const NSPoint p = [self convertPoint: [e locationInWindow] fromView: nil];
    if (! host) return;
    if ([self presetClick: p]) return;
    const int i = [self ctlAtPoint: p];
    if (i < 0) return;
    const Ctl& c = kCtls[i];
    readoutParam = c.param;
    status[0] = 0;
    activePreset = -1;     // settings are being changed away from the preset

    switch (c.type)
    {
        case kCtlRocker:
            // Press the top half for ON, the bottom half for OFF
            host->guiBeginEdit (c.param);
            [self setParam: c.param value: (p.y < c.cy) ? 1.0f : 0.0f];
            host->guiEndEdit (c.param);
            return;
        case kCtlButton:
        case kCtlSlide:
            host->guiBeginEdit (c.param);
            [self setParam: c.param value: isOn (shown[c.param]) ? 0.0f : 1.0f];
            host->guiEndEdit (c.param);
            return;
        default:
            break;
    }

    host->guiBeginEdit (c.param);
    if ([e clickCount] == 2)
        [self setParam: c.param value: kDefaults[c.param]];   // double-click: back to default
    dragCtl = i;
    dragStartY = p.y;
    dragStartValue = shown[c.param];
    [self setNeedsDisplay: YES];
}

- (void) mouseDragged: (NSEvent*) e
{
    if (dragCtl < 0 || ! host) return;
    const Ctl& c = kCtls[dragCtl];
    const NSPoint p = [self convertPoint: [e locationInWindow] fromView: nil];
    const bool fine = ([e modifierFlags] & NSShiftKeyMask) != 0;
    const CGFloat perPixel = fine ? 1.0 / 1000.0 : 1.0 / 200.0;
    const CGFloat v = dragStartValue + (dragStartY - p.y) * perPixel;
    [self setParam: c.param value: (float) v];
}

- (void) mouseUp: (NSEvent*) e
{
    (void) e;
    if (dragCtl >= 0 && host) host->guiEndEdit (kCtls[dragCtl].param);
    dragCtl = -1;
}

- (void) scrollWheel: (NSEvent*) e
{
    const NSPoint p = [self convertPoint: [e locationInWindow] fromView: nil];
    const int i = [self ctlAtPoint: p];
    if (i < 0 || ! host) return;
    const Ctl& c = kCtls[i];
    if (c.type != kCtlKnob && c.type != kCtlSmallKnob) return;
    const CGFloat d = [e deltaY];
    if (d == 0.0) return;
    status[0] = 0;
    activePreset = -1;
    host->guiBeginEdit (c.param);
    [self setParam: c.param value: (float) (shown[c.param] + d * 0.01)];
    host->guiEndEdit (c.param);
}

@end

//==============================================================================
namespace tbme
{

// Both calls use their own autorelease pool, so nothing of this plugin's is
// left waiting in the host's pool when the editor closes or the plugin unloads.
void* editorOpen (void* parentView, EditorHost* host)
{
    NSView* parent = (NSView*) parentView;
    if (! parent || ! host) return 0;
    NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
    TBMultiEchoPanelView* v = [[TBMultiEchoPanelView alloc]
                                    initWithFrame: NSMakeRect (0, 0, kEditorWidth, kEditorHeight) host: host];
    if (v) [parent addSubview: v];
    [pool release];
    return (void*) v;    // retained until editorClose
}

void editorClose (void* handle)
{
    TBMultiEchoPanelView* v = (TBMultiEchoPanelView*) handle;
    if (! v) return;
    NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
    [v shutdown];
    [v removeFromSuperview];
    [v release];
    [pool release];
}

} // namespace tbme
