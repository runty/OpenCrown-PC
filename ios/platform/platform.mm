#include "core.h"
#import <AVFoundation/AVFoundation.h>
#import <GameController/GameController.h>
#import <UIKit/UIKit.h>
#import <TargetConditionals.h>
#include <cstdlib>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace dcp::mobile;
namespace {
InputState input;
std::atomic<bool> active{false};
DcpPlatformHooks hooks{};
std::atomic<bool> initialized{false};
bool manualPause = false, interrupted = false;
NSMutableArray<id> *observers;
NSMutableArray<GCController *> *controllers;
dispatch_queue_t audioQueue;
void *audioQueueKey = &audioQueueKey;
AVAudioEngine *engine;
AVAudioSourceNode *source;
struct AudioState {
  AudioRing ring;
  std::timed_mutex producer;
  std::mutex errorMutex;
  std::string error;
  std::atomic<bool> wanted{false}, running{false}, paused{true}, primed{false};
  std::atomic<unsigned> callbacks{0}, callbackFrames{256};
  std::atomic<uint64_t> submitted{0}, rendered{0}, underrun{0}, clipped{0},
      flushed{0};
  std::atomic<double> rate{0}, duration{0};
} audio;
void recordError(NSString *message) {
  std::lock_guard<std::mutex> lock(audio.errorMutex);
  audio.error = message.UTF8String ?: "Unknown platform error";
}
void onAudioQueue(dispatch_block_t block) {
  if (dispatch_get_specific(audioQueueKey))
    block();
  else
    dispatch_sync(audioQueue, block);
}
uint32_t queueTarget() {
  return std::min<uint32_t>(audio_capacity / 2,
                            std::max(1024u, audio.callbackFrames.load() * 3));
}
void stopAudio(bool deactivate) {
  // The render block never takes a lock; stop and drain it before clearing
  // indices.
  audio.paused.store(true);
  audio.running.store(false);
  [engine stop];
  while (audio.callbacks.load(std::memory_order_acquire))
    std::this_thread::yield();
  {
    std::lock_guard<std::timed_mutex> lock(audio.producer);
    audio.flushed.fetch_add(audio.ring.clear());
    audio.primed.store(false);
  }
  engine = nil;
  source = nil;
  if (deactivate) {
    NSError *error = nil;
    if (![[AVAudioSession sharedInstance] setActive:NO error:&error])
      recordError(error.localizedDescription);
  }
}
bool startAudio() {
  stopAudio(false);
  if (!audio.wanted.load() || !active.load())
    return false;
  NSError *error = nil;
  AVAudioSession *session = AVAudioSession.sharedInstance;
  if (![session setCategory:AVAudioSessionCategoryAmbient
                       mode:AVAudioSessionModeDefault
                    options:AVAudioSessionCategoryOptionMixWithOthers
                      error:&error] ||
      ![session setPreferredSampleRate:48000 error:&error] ||
      ![session setPreferredIOBufferDuration:256.0 / 48000.0 error:&error] ||
      ![session setActive:YES error:&error]) {
    recordError(error.localizedDescription);
    return false;
  }
  audio.rate.store(session.sampleRate);
  audio.duration.store(session.IOBufferDuration);
  // IOBufferDuration is in device-rate frames. The source always supplies 48
  // kHz.
  audio.callbackFrames.store(
      std::max(256u, unsigned(std::ceil(session.IOBufferDuration * 48000))));
  engine = [[AVAudioEngine alloc] init];
  AVAudioFormat *format =
      [[AVAudioFormat alloc] initStandardFormatWithSampleRate:48000 channels:2];
#if TARGET_OS_SIMULATOR
  const char *muteSetting = std::getenv("DCP_SIM_MUTE_AUDIO");
  const bool muted = muteSetting && !std::strcmp(muteSetting, "1");
#else
  constexpr bool muted = false;
#endif
  source = [[AVAudioSourceNode alloc]
      initWithFormat:format
         renderBlock:^OSStatus(BOOL *silence, const AudioTimeStamp *,
                               AVAudioFrameCount count,
                               AudioBufferList *buffers) {
           audio.callbacks.fetch_add(1, std::memory_order_acq_rel);
           for (UInt32 i = 0; i < buffers->mNumberBuffers; ++i)
             std::memset(buffers->mBuffers[i].mData, 0,
                         buffers->mBuffers[i].mDataByteSize);
           size_t got = 0;
           if (!audio.paused.load(std::memory_order_acquire) &&
               buffers->mNumberBuffers == 2 &&
               buffers->mBuffers[0].mDataByteSize >= count * sizeof(float) &&
               buffers->mBuffers[1].mDataByteSize >= count * sizeof(float)) {
             audio.callbackFrames.store(count, std::memory_order_relaxed);
             if (!audio.primed.load() && audio.ring.queued() >= count)
               audio.primed.store(true);
             if (audio.primed.load()) {
               got = audio.ring.pop(
                   static_cast<float *>(buffers->mBuffers[0].mData),
                   static_cast<float *>(buffers->mBuffers[1].mData), count);
               audio.underrun.fetch_add(count - got, std::memory_order_relaxed);
             }
             audio.rendered.fetch_add(got, std::memory_order_relaxed);
           }
           // Quiet simulator verification still consumes the real stream and
           // retains callback/underrun accounting. Device output is unchanged.
           if (muted)
             for (UInt32 i = 0; i < buffers->mNumberBuffers; ++i)
               std::memset(buffers->mBuffers[i].mData, 0,
                           buffers->mBuffers[i].mDataByteSize);
           *silence = muted || got == 0;
           audio.callbacks.fetch_sub(1, std::memory_order_release);
           return noErr;
         }];
  [engine attachNode:source];
  [engine connect:source to:engine.mainMixerNode format:format];
  [engine prepare];
  audio.paused.store(false);
  if (![engine startAndReturnError:&error]) {
    recordError(error.localizedDescription);
    stopAudio(false);
    return false;
  }
  audio.running.store(true);
  return true;
}
void refreshControllers();
void updateActive();
} // namespace

// A single overlay keeps per-touch captures so movement, attacks and menus
// combine.
@interface DcpTouchOverlay : UIView
- (void)clear;
@end
struct TouchCapture {
  __unsafe_unretained UITouch *touch;
  NSInteger control;
  uint8_t id;
};
@implementation DcpTouchOverlay {
  std::vector<TouchCapture> _captures;
  uint8_t _nextID;
}
- (instancetype)initWithFrame:(CGRect)frame {
  if ((self = [super initWithFrame:frame])) {
    self.multipleTouchEnabled = YES;
    self.opaque = NO;
    self.backgroundColor = UIColor.clearColor;
    self.autoresizingMask =
        UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    self.accessibilityLabel = @"Game touch controls";
  }
  return self;
}
- (CGFloat)unit {
  const UIEdgeInsets safe = self.safeAreaInsets;
  const CGFloat width = self.bounds.size.width - safe.left - safe.right;
  const CGFloat height = self.bounds.size.height - safe.top - safe.bottom;
  return std::clamp<CGFloat>(
      std::min(width / 14, height / 9), 18, 58);
}
- (CGPoint)centre:(NSInteger)control {
  CGFloat u = self.unit;
  UIEdgeInsets safe = self.safeAreaInsets;
  CGFloat left = safe.left + 2 * u,
          right = self.bounds.size.width - safe.right - 2 * u;
  CGFloat bottom = self.bounds.size.height - safe.bottom - 1.8 * u;
  const bool compact =
      (self.bounds.size.width - safe.left - safe.right) / u < 16;
  const CGFloat menuY = compact ? bottom - 4 * u
                               : self.bounds.size.height - safe.bottom - .7 * u;
  switch (control) {
  case 0:
    return CGPointMake(left, bottom);
  case 1:
    return CGPointMake(right - 2.8 * u, bottom);
  case 2:
    return CGPointMake(right, bottom + .85 * u); // Cross
  case 3:
    return CGPointMake(right + .85 * u, bottom); // Circle
  case 4:
    return CGPointMake(right, bottom - .85 * u); // Triangle
  case 5:
    return CGPointMake(right - .85 * u, bottom); // Square
  case 6:
    return CGPointMake(left, bottom - 2 * u);
  case 7:
    return CGPointMake(right, bottom - 2 * u);
  case 8:
    return CGPointMake(self.bounds.size.width / 2 - u, menuY);
  case 9:
    return CGPointMake(self.bounds.size.width / 2 + u, menuY);
  case 10:
    return CGPointMake(left + 2.2 * u, bottom - 1.6 * u); // D-pad
  case 11:
    return CGPointMake(left + 1.3 * u, bottom - 2.8 * u);
  case 12:
    return CGPointMake(right - 1.3 * u, bottom - 2.8 * u);
  case 13:
    return CGPointMake(left - 1.4 * u, bottom + 1.25 * u);
  default:
    return CGPointMake(right - 1.4 * u, bottom + 1.2 * u);
  }
}
- (void)drawRect:(CGRect)rect {
  (void)rect;
  NSArray<NSString *> *labels = @[
    @"MOVE", @"LOOK", @"×", @"○", @"△", @"□", @"L1", @"R1", @"MENU", @"PAD",
    @"D-PAD", @"L2", @"R2", @"L3", @"R3"
  ];
  CGFloat u = self.unit;
  for (NSInteger c = 0; c < 15; ++c) {
    CGPoint p = [self centre:c];
    CGFloat radius = c < 2 ? u : (c == 10 ? .7 * u : .5 * u);
    UIBezierPath *path = [UIBezierPath
        bezierPathWithOvalInRect:CGRectMake(p.x - radius, p.y - radius,
                                            2 * radius, 2 * radius)];
    [[UIColor colorWithWhite:.12 alpha:.22] setFill];
    [path fill];
    [[UIColor colorWithWhite:1 alpha:.45] setStroke];
    [path stroke];
    NSDictionary *attributes = @{
      NSFontAttributeName : [UIFont systemFontOfSize:c < 2 || c > 7 ? 10 : 19
                                              weight:UIFontWeightMedium],
      NSForegroundColorAttributeName : [UIColor colorWithWhite:1 alpha:.8]
    };
    CGSize size = [labels[c] sizeWithAttributes:attributes];
    [labels[c]
           drawAtPoint:CGPointMake(p.x - size.width / 2, p.y - size.height / 2)
        withAttributes:attributes];
  }
}
- (void)layoutSubviews {
  [super layoutSubviews];
  [self setNeedsDisplay];
}
- (void)publish {
  auto pad = neutral();
  if (self.bounds.size.width <= 0 || self.bounds.size.height <= 0) {
    input.touch(pad);
    return;
  }
  const uint32_t masks[] = {0,      0,     0x4000, 0x2000, 0x1000,
                            0x8000, 0x400, 0x800,  0x8,    0x100000,
                            0,      0x100, 0x200,  0x2,    0x4};
  CGFloat u = self.unit;
  CGRect content = self.bounds;
  if (content.size.width / content.size.height > 16.0 / 9) {
    CGFloat width = content.size.height * 16 / 9;
    content.origin.x = (content.size.width - width) / 2;
    content.size.width = width;
  } else {
    CGFloat height = content.size.width * 9 / 16;
    content.origin.y = (content.size.height - height) / 2;
    content.size.height = height;
  }
  for (const auto &capture : _captures) {
    CGPoint p = [capture.touch locationInView:self];
    if (capture.control < 0) {
      touch_point(pad, (p.x - content.origin.x) / content.size.width,
                  (p.y - content.origin.y) / content.size.height, capture.id);
    } else if (capture.control < 2) {
      CGPoint centre = [self centre:capture.control];
      float x = (p.x - centre.x) / u, y = (p.y - centre.y) / u;
      float length = std::hypot(x, y);
      if (length > 1) {
        x /= length;
        y /= length;
      }
      if (capture.control == 0) {
        pad.lx = axis(x);
        pad.ly = axis(y);
      } else {
        pad.rx = axis(x);
        pad.ry = axis(y);
      }
    } else if (capture.control == 10) {
      CGPoint centre = [self centre:10];
      float x = (p.x - centre.x) / u, y = (p.y - centre.y) / u;
      if (x < -.18f)
        pad.buttons |= 0x80;
      if (x > .18f)
        pad.buttons |= 0x20;
      if (y < -.18f)
        pad.buttons |= 0x10;
      if (y > .18f)
        pad.buttons |= 0x40;
    } else {
      pad.buttons |= masks[capture.control];
      if (capture.control == 11)
        pad.l2 = 255;
      if (capture.control == 12)
        pad.r2 = 255;
    }
  }
  input.touch(pad);
}
- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
  (void)event;
  if (!active.load())
    return;
  for (UITouch *touch in touches) {
    CGPoint p = [touch locationInView:self];
    NSInteger selected = -1;
    CGFloat nearest = CGFLOAT_MAX;
    for (NSInteger c = 0; c < 15; ++c) {
      CGPoint centre = [self centre:c];
      CGFloat distance = std::hypot(p.x - centre.x, p.y - centre.y);
      CGFloat radius = self.unit * (c < 2 ? 1.15 : (c == 10 ? .8 : .63));
      if (distance < radius && distance < nearest) {
        selected = c;
        nearest = distance;
      }
    }
    bool occupied = false;
    for (const auto &capture : _captures)
      if (selected >= 0 && capture.control == selected)
        occupied = true;
    if (!occupied)
      _captures.push_back({touch, selected, uint8_t(_nextID++ & 127)});
  }
  [self publish];
}
- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
  (void)touches;
  (void)event;
  [self publish];
}
- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
  (void)event;
  _captures.erase(std::remove_if(_captures.begin(), _captures.end(),
                                 [&](const TouchCapture &c) {
                                   return [touches containsObject:c.touch];
                                 }),
                  _captures.end());
  [self publish];
}
- (void)touchesCancelled:(NSSet<UITouch *> *)touches
               withEvent:(UIEvent *)event {
  [self touchesEnded:touches withEvent:event];
}
- (void)clear {
  _captures.clear();
  input.touch(neutral());
}
@end

namespace {
__weak DcpTouchOverlay *overlay;
void publishController(GCController *controller) {
  NSUInteger slot = [controllers indexOfObjectIdenticalTo:controller];
  if (slot == NSNotFound || slot >= 4)
    return;
  auto p = neutral();
  GCExtendedGamepad *g = controller.extendedGamepad;
  if (!g) {
    input.physical(unsigned(slot), neutral(false));
    return;
  }
  auto button = [&](GCControllerButtonInput *b, uint32_t mask) {
    if (b.isPressed)
      p.buttons |= mask;
  };
  button(g.buttonA, 0x4000);
  button(g.buttonB, 0x2000);
  button(g.buttonX, 0x8000);
  button(g.buttonY, 0x1000);
  button(g.leftShoulder, 0x400);
  button(g.rightShoulder, 0x800);
  button(g.leftTrigger, 0x100);
  button(g.rightTrigger, 0x200);
  button(g.leftThumbstickButton, 0x2);
  button(g.rightThumbstickButton, 0x4);
  button(g.buttonMenu, 0x8);
  button(g.buttonOptions, 0x100000);
  button(g.dpad.up, 0x10);
  button(g.dpad.right, 0x20);
  button(g.dpad.down, 0x40);
  button(g.dpad.left, 0x80);
  p.lx = axis(g.leftThumbstick.xAxis.value);
  p.ly = axis(g.leftThumbstick.yAxis.value, true);
  p.rx = axis(g.rightThumbstick.xAxis.value);
  p.ry = axis(g.rightThumbstick.yAxis.value, true);
  p.l2 = trigger(g.leftTrigger.value);
  p.r2 = trigger(g.rightTrigger.value);
  GCControllerDirectionPad *primary = nil, *secondary = nil;
  GCControllerButtonInput *touchButton = nil;
  if ([g isKindOfClass:GCDualSenseGamepad.class]) {
    auto *ds = (GCDualSenseGamepad *)g;
    primary = ds.touchpadPrimary;
    secondary = ds.touchpadSecondary;
    touchButton = ds.touchpadButton;
  } else if ([g isKindOfClass:GCDualShockGamepad.class]) {
    auto *ds = (GCDualShockGamepad *)g;
    primary = ds.touchpadPrimary;
    secondary = ds.touchpadSecondary;
    touchButton = ds.touchpadButton;
  }
  button(touchButton, 0x100000);
  // GC reports (0,0) when not touching, without a separate touch-presence API.
  if (primary.xAxis.value != 0 || primary.yAxis.value != 0)
    touch_point(p, (primary.xAxis.value + 1) / 2, (1 - primary.yAxis.value) / 2,
                0);
  if (secondary.xAxis.value != 0 || secondary.yAxis.value != 0)
    touch_point(p, (secondary.xAxis.value + 1) / 2,
                (1 - secondary.yAxis.value) / 2, 1);
  input.physical(unsigned(slot), p);
}
void refreshControllers() {
  NSArray<GCController *> *available = GCController.controllers;
  for (GCController *old in [controllers copy]) {
    if (![available containsObject:old]) {
      old.extendedGamepad.valueChangedHandler = nil;
      [controllers removeObjectIdenticalTo:old];
    }
  }
  for (GCController *candidate in available) {
    if (controllers.count == 4)
      break;
    if (candidate.extendedGamepad && ![controllers containsObject:candidate])
      [controllers addObject:candidate];
  }
  for (unsigned slot = 0; slot < 4; ++slot)
    input.physical(slot, neutral(false));
  for (GCController *controller in controllers) {
    controller.handlerQueue = dispatch_get_main_queue();
    __weak GCController *weakController = controller;
    controller.extendedGamepad.valueChangedHandler =
        ^(GCExtendedGamepad *, GCControllerElement *) {
          GCController *strongController = weakController;
          if (strongController)
            publishController(strongController);
        };
    publishController(controller);
  }
}
void updateActive() {
  bool next = initialized && !manualPause && !interrupted &&
              UIApplication.sharedApplication.applicationState ==
                  UIApplicationStateActive;
  if (next == active.exchange(next))
    return;
  if (!next)
    [overlay clear];
  input.active(next);
  if (next)
    refreshControllers();
  onAudioQueue(^{
    if (next)
      startAudio();
    else
      stopAudio(true);
  });
  if (hooks.set_active)
    hooks.set_active(hooks.context, next);
}
void observe(NSNotificationName name, void (^block)(NSNotification *)) {
  [observers addObject:[NSNotificationCenter.defaultCenter
                           addObserverForName:name
                                       object:nil
                                        queue:NSOperationQueue.mainQueue
                                   usingBlock:block]];
}
void refreshAudio() {
  if (active.load() && audio.wanted.load())
    dispatch_async(audioQueue, ^{
      startAudio();
    });
}
} // namespace

extern "C" int dcp_platform_initialize(DcpPlatformHooks value) {
  if (!NSThread.isMainThread)
    return 0;
  if (initialized)
    return 1;
  hooks = value;
  observers = [NSMutableArray array];
  controllers = [NSMutableArray array];
  audioQueue = dispatch_queue_create("org.opencrown.platform.audio",
                                     DISPATCH_QUEUE_SERIAL);
  dispatch_queue_set_specific(audioQueue, audioQueueKey, audioQueueKey,
                              nullptr);
  initialized = true;
  manualPause = false;
  interrupted = false;
  input.touch(neutral());
  input.active(false);
  observe(UIApplicationWillResignActiveNotification, ^(NSNotification *) {
    if (active.exchange(false)) {
      [overlay clear];
      input.active(false);
      onAudioQueue(^{
        stopAudio(true);
      });
      if (hooks.set_active)
        hooks.set_active(hooks.context, 0);
    }
  });
  observe(UIApplicationDidEnterBackgroundNotification, ^(NSNotification *) {
    updateActive();
  });
  observe(UIApplicationDidBecomeActiveNotification, ^(NSNotification *) {
    updateActive();
  });
  observe(GCControllerDidConnectNotification, ^(NSNotification *) {
    refreshControllers();
  });
  observe(GCControllerDidDisconnectNotification, ^(NSNotification *) {
    refreshControllers();
  });
  observe(AVAudioSessionRouteChangeNotification, ^(NSNotification *note) {
    // Our own setCategory must not recursively rebuild the engine.
    if ([note.userInfo[AVAudioSessionRouteChangeReasonKey]
            unsignedIntegerValue] !=
        AVAudioSessionRouteChangeReasonCategoryChange)
      refreshAudio();
  });
  observe(AVAudioSessionMediaServicesWereLostNotification, ^(NSNotification *) {
    onAudioQueue(^{
      stopAudio(false);
    });
    recordError(@"Audio media services unavailable");
  });
  observe(AVAudioSessionMediaServicesWereResetNotification,
          ^(NSNotification *) {
            refreshAudio();
          });
  // AVAudioEngine can stop after a route-format change. Ignore startup
  // notifications from old engines and already-running configurations.
  observe(AVAudioEngineConfigurationChangeNotification,
          ^(NSNotification *note) {
            onAudioQueue(^{
              if (note.object == engine && engine && !engine.running &&
                  audio.wanted.load() && active.load())
                startAudio();
            });
          });
  if (@available(iOS 27.0, *)) {
    observe(AVAudioSessionDidBecomeInactiveNotification,
            ^(NSNotification *note) {
              AVAudioSessionDeactivationContext *context =
                  note.userInfo[AVAudioSessionDeactivationContextKey];
              if (context.source == AVAudioSessionDeactivationSourceSystem) {
                interrupted = true;
                updateActive();
              }
            });
    observe(AVAudioSessionResumptionRecommendationNotification,
            ^(NSNotification *note) {
              AVAudioSessionResumptionContext *context =
                  note.userInfo[AVAudioSessionResumptionContextKey];
              if (context.recommendation ==
                  AVAudioSessionResumptionRecommendationShouldResume) {
                interrupted = false;
                updateActive();
              }
            });
  } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    observe(AVAudioSessionInterruptionNotification, ^(NSNotification *note) {
      if ([note.userInfo[AVAudioSessionInterruptionTypeKey]
              unsignedIntegerValue] == AVAudioSessionInterruptionTypeBegan)
        interrupted = true;
      else if ([note.userInfo[AVAudioSessionInterruptionOptionKey]
                   unsignedIntegerValue] &
               AVAudioSessionInterruptionOptionShouldResume)
        interrupted = false;
      updateActive();
    });
#pragma clang diagnostic pop
  }
  refreshControllers();
  updateActive();
  return 1;
}
extern "C" void dcp_platform_shutdown() {
  if (!NSThread.isMainThread || !initialized)
    return;
  initialized = false;
  for (id observer in observers)
    [NSNotificationCenter.defaultCenter removeObserver:observer];
  [observers removeAllObjects];
  for (GCController *controller in controllers)
    controller.extendedGamepad.valueChangedHandler = nil;
  [controllers removeAllObjects];
  [overlay removeFromSuperview];
  overlay = nil;
  audio.wanted.store(false);
  onAudioQueue(^{
    stopAudio(true);
  });
  if (active.exchange(false) && hooks.set_active)
    hooks.set_active(hooks.context, 0);
  input.reset();
  hooks = {};
}
extern "C" void dcp_platform_set_paused(int paused) {
  if (!NSThread.isMainThread || !initialized)
    return;
  manualPause = paused;
  if (!paused)
    interrupted = false;
  updateActive();
}
extern "C" int dcp_platform_active() { return active.load(); }
extern "C" void dcp_platform_attach_touch_overlay(void *view) {
  if (!NSThread.isMainThread || !initialized)
    return;
  [overlay clear];
  [overlay removeFromSuperview];
  overlay = nil;
  if (view) {
    UIView *parent = (__bridge UIView *)view;
    DcpTouchOverlay *next =
        [[DcpTouchOverlay alloc] initWithFrame:parent.bounds];
    [parent addSubview:next];
    overlay = next;
  }
}
extern "C" void dcp_platform_input_set_touch(const DcpPlatformPad *p) {
  input.touch(p ? *p : neutral());
}
extern "C" void dcp_platform_input_read(unsigned slot, DcpPlatformPad *p) {
  if (p)
    *p = input.read(slot);
}
extern "C" int dcp_platform_audio_start() {
  if (!initialized)
    return 0;
  audio.wanted.store(true);
  __block bool result = false;
  onAudioQueue(^{
    result = startAudio();
  });
  return result;
}
extern "C" void dcp_platform_audio_stop() {
  if (!initialized)
    return;
  audio.wanted.store(false);
  onAudioQueue(^{
    stopAudio(true);
  });
}
extern "C" size_t dcp_platform_audio_submit(const float *data, size_t frames,
                                            uint32_t timeout) {
  if (!data || !frames || !audio.running.load())
    return 0;
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
  std::unique_lock<std::timed_mutex> lock(audio.producer, std::defer_lock);
  if (timeout ? !lock.try_lock_until(deadline) : !lock.try_lock())
    return 0;
  size_t accepted = 0;
  while (accepted < frames && audio.running.load() && !audio.paused.load()) {
    size_t target = queueTarget();
    size_t queued = audio.ring.queued();
    if (queued < target) {
      size_t n = std::min<size_t>({256, frames - accepted, target - queued});
      uint64_t clipped = 0;
      n = audio.ring.push(data + accepted * 2, n, clipped);
      accepted += n;
      audio.clipped.fetch_add(clipped);
      audio.submitted.fetch_add(n);
      continue;
    }
    if (!timeout || std::chrono::steady_clock::now() >= deadline)
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return accepted;
}
extern "C" void dcp_platform_audio_stats(DcpPlatformAudioStats *s) {
  if (!s)
    return;
  *s = {audio.submitted.load(), audio.rendered.load(),
        audio.underrun.load(),  audio.clipped.load(),
        audio.flushed.load(),   uint32_t(audio.ring.queued()),
        queueTarget(),          audio.callbackFrames.load(),
        audio.running.load(),   audio.paused.load(),
        audio.rate.load(),      audio.duration.load()};
}
extern "C" void dcp_platform_copy_error(char *destination, size_t capacity) {
  if (!destination || !capacity)
    return;
  std::lock_guard<std::mutex> lock(audio.errorMutex);
  size_t n = std::min(capacity - 1, audio.error.size());
  std::memcpy(destination, audio.error.data(), n);
  destination[n] = 0;
}
