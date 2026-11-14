#define SDL_MAIN_HANDLED
#include "game/game.h"
#include "game/ime-services.h"
#include "platform/platform.h"
#include "render/replay.h"
#include "storage/save-transactions.h"
#include "storage/storage-core.h"
#import "storage/storage-ui.h"
#import <Metal/Metal.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#import <TargetConditionals.h>
#import <UIKit/UIKit.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#import <mach/mach.h>
#include <os/proc.h>
#include <memory>
#include <mutex>
#include <vector>

namespace {
struct FrameIntervals {
  static constexpr size_t capacity = 3600;
  std::array<double, capacity> values{};
  size_t count = 0, next = 0;
  double last = 0;
  void observe(double now) {
    if (last > 0 && now > last) {
      values[next] = (now - last) * 1000.0;
      next = (next + 1) % capacity;
      count = std::min(count + 1, capacity);
    }
    last = now;
  }
  NSDictionary *json() const {
    std::vector<double> sorted(values.begin(), values.begin() + count);
    std::sort(sorted.begin(), sorted.end());
    auto percentile = [&](double p) {
      if (sorted.empty())
        return 0.0;
      const double index = p * double(sorted.size() - 1);
      const size_t lo = size_t(index), hi = std::min(lo + 1, sorted.size() - 1);
      return sorted[lo] + (sorted[hi] - sorted[lo]) * (index - double(lo));
    };
    return @{
      @"samples" : @(count),
      @"capacity" : @(capacity),
      @"p50_ms" : @(percentile(0.5)),
      @"p95_ms" : @(percentile(0.95)),
      @"p99_ms" : @(percentile(0.99))
    };
  }
};
NSDictionary *memorySnapshot() {
  task_vm_info_data_t info{};
  mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
  const auto result = task_info(mach_task_self(), TASK_VM_INFO,
                                reinterpret_cast<task_info_t>(&info), &count);
  if (result != KERN_SUCCESS)
    return @{@"available" : @NO, @"mach_result" : @(result)};
  return @{
    @"available" : @YES,
    @"phys_footprint_bytes" : @(info.phys_footprint),
    @"resident_bytes" : @(info.resident_size),
    @"remaining_dirty_memory_limit_bytes" : @(os_proc_available_memory()),
    @"physical_memory_bytes" : @(NSProcessInfo.processInfo.physicalMemory)
  };
}
NSDictionary *guestSnapshot() {
  DcpGameStats stats{};
  dcp_game_snapshot(&stats);
  return @{
    @"running" : @(stats.running != 0),
    @"paused" : @(stats.paused != 0),
    @"stopping" : @(stats.stopping != 0),
    @"interactive" : @(stats.interactive != 0),
    @"steps" : @(stats.steps),
    @"vblanks" : @(stats.vblanks),
    @"workers" : @(stats.workers),
    @"workers_started" : @(stats.workers),
    @"submits" : @(stats.submits),
    @"flips" : @(stats.flips),
    @"label_writes" : @(stats.label_writes),
    @"libc_checks" : @(stats.libc_checks),
    @"libc_result" : @(stats.libc_result),
    @"entered_game" : @(stats.entered_game != 0),
    @"title_seen" : @(stats.title_seen != 0),
    @"checkpoint_complete" : @(stats.checkpoint_complete != 0),
    @"current_scene" : @(stats.current_scene),
    @"first_error" : @(stats.first_error),
    @"headless_checkpoint_expected" : @(stats.running && !stats.interactive)
  };
}
NSDictionary *audioSnapshot() {
  DcpPlatformAudioStats stats{};
  dcp_platform_audio_stats(&stats);
  return @{
    @"submitted_frames" : @(stats.submitted_frames),
    @"rendered_frames" : @(stats.rendered_frames),
    @"underrun_frames" : @(stats.underrun_frames),
    @"clipped_samples" : @(stats.clipped_samples),
    @"flushed_frames" : @(stats.flushed_frames),
    @"queued_frames" : @(stats.queued_frames),
    @"target_frames" : @(stats.target_frames),
    @"callback_frames" : @(stats.callback_frames),
    @"running" : @(stats.running != 0),
    @"paused" : @(stats.paused != 0),
    @"device_rate" : @(stats.device_sample_rate),
    @"buffer_seconds" : @(stats.device_buffer_seconds),
#if TARGET_OS_SIMULATOR
    @"simulator_muted" : @(std::getenv("DCP_SIM_MUTE_AUDIO") &&
        !std::strcmp(std::getenv("DCP_SIM_MUTE_AUDIO"), "1"))
#else
    @"simulator_muted" : @NO
#endif
  };
}
NSString *thermalName(NSProcessInfoThermalState state) {
  switch (state) {
  case NSProcessInfoThermalStateNominal:
    return @"nominal";
  case NSProcessInfoThermalStateFair:
    return @"fair";
  case NSProcessInfoThermalStateSerious:
    return @"serious";
  case NSProcessInfoThermalStateCritical:
    return @"critical";
  }
  return @"unknown";
}
} // namespace

@interface GameController : UIViewController {
  SDL_Window *_gameWindow;
  std::unique_ptr<dcp::mobile::Replay> _replay;
  std::unique_ptr<dcp::storage::SaveTransactions> _saves;
  uint64_t _presentedFrames;
  uint64_t _replayedFrames;
  uint64_t _replayedDraws;
  uint64_t _peakFootprint, _peakResident;
  BOOL _running;
  BOOL _autoStarted;
  NSString *_hostError;
  NSString *_saveError;
  BOOL _storageBusy;
  BOOL _sceneAvailable;
  BOOL _restoring;
  BOOL _snapshotHasContent;
  dispatch_queue_t _diagnosticQueue;
  std::mutex _diagnosticStateLock;
  NSString *_sessionID;
  NSString *_diagnosticPhase;
  NSDictionary *_metalInfo;
  NSDictionary *_lastGuest;
  NSTimeInterval _sessionStarted;
  CFTimeInterval _lastHeartbeat;
  FrameIntervals _gameIntervals, _drawableIntervals;
}
@property(nonatomic, strong) UILabel *status;
@property(nonatomic, strong) UIButton *play;
@property(nonatomic, strong) UIButton *importButton;
@property(nonatomic, strong) UIButton *restoreButton;
@property(nonatomic, strong) DCPStorageController *storage;
@property(nonatomic, strong) CADisplayLink *displayLink;
@property(nonatomic, strong) UIWindow *launcherWindow;
@property(nonatomic, strong) UIButton *pauseButton;
@property(nonatomic, strong) UIAlertController *imeAlert;
@property(nonatomic, strong) NSArray<NSLayoutConstraint *> *pauseLayout;
- (void)activeChanged:(BOOL)active;
- (void)unloadRenderer;
- (BOOL)saveTransaction:(BOOL)begin;
- (void)attachGameControls:(UIView *)content;
- (void)sceneAttached;
- (void)recordStartupFailure:(NSString *)error stage:(NSString *)stage;
- (void)sceneDetached;
- (NSDictionary *)diagnosticSnapshot;
- (void)emitLiveDiagnostics:(BOOL)force;
- (void)queueDiagnostic:(NSDictionary *)report filename:(NSString *)filename;
- (NSString *)currentSaveError;
@end

namespace {
NSString *documents() {
  return NSSearchPathForDirectoriesInDomains(NSDocumentDirectory,
                                             NSUserDomainMask, YES)
      .firstObject;
}
#if TARGET_OS_SIMULATOR
void saveUISnapshot(UIWindow *window, NSString *name) {
  if (!window || !std::getenv("DCP_SIM_UI_SNAPSHOT"))
    return;
  [window layoutIfNeeded];
  UIGraphicsImageRendererFormat *format =
      [UIGraphicsImageRendererFormat defaultFormat];
  format.scale = 1;
  UIGraphicsImageRenderer *renderer =
      [[UIGraphicsImageRenderer alloc] initWithSize:window.bounds.size
                                             format:format];
  UIImage *image =
      [renderer imageWithActions:^(UIGraphicsImageRendererContext *context) {
        [window.layer renderInContext:context.CGContext];
      }];
  [UIImagePNGRepresentation(image)
      writeToFile:[documents() stringByAppendingPathComponent:name]
       atomically:YES];
}
#endif
void activeChanged(void *context, int active) {
  [(__bridge GameController *)context activeChanged:active != 0];
}
void willUnload(void *context) {
  GameController *controller = (__bridge GameController *)context;
  if (NSThread.isMainThread)
    [controller unloadRenderer];
  else
    dispatch_sync(dispatch_get_main_queue(), ^{
      [controller unloadRenderer];
    });
}
int saveTransaction(void *context, int begin) {
  return [(__bridge GameController *)context saveTransaction:begin != 0];
}
UIButton *button(NSString *title, id target, SEL action) {
  UIButton *value = [UIButton buttonWithType:UIButtonTypeSystem];
  value.configuration = [UIButtonConfiguration filledButtonConfiguration];
  [value setTitle:title forState:UIControlStateNormal];
  [value addTarget:target
                action:action
      forControlEvents:UIControlEventTouchUpInside];
  return value;
}
} // namespace

@implementation GameController
- (void)viewDidLoad {
  [super viewDidLoad];
  _diagnosticQueue = dispatch_queue_create("com.wadscorp.opencrown.diagnostics",
                                           DISPATCH_QUEUE_SERIAL);
  _sessionID = NSUUID.UUID.UUIDString;
  _sessionStarted = [NSDate.date timeIntervalSince1970];
  _diagnosticPhase = @"launcher";
  id<MTLDevice> gpu = MTLCreateSystemDefaultDevice();
  _metalInfo = @{
    @"name" : gpu.name ?: @"unavailable",
    @"bc_texture_compression" : @(gpu.supportsBCTextureCompression),
    @"apple1" : @([gpu supportsFamily:MTLGPUFamilyApple1]),
    @"apple3" : @([gpu supportsFamily:MTLGPUFamilyApple3]),
    @"mac2" : @([gpu supportsFamily:MTLGPUFamilyMac2]),
    @"common1" : @([gpu supportsFamily:MTLGPUFamilyCommon1]),
    @"common2" : @([gpu supportsFamily:MTLGPUFamilyCommon2]),
    @"common3" : @([gpu supportsFamily:MTLGPUFamilyCommon3])
  };
  self.title = @"OpenCrown";
  self.view.backgroundColor = UIColor.systemBackgroundColor;
  self.status = [[UILabel alloc] init];
  self.status.numberOfLines = 0;
  self.status.textAlignment = NSTextAlignmentCenter;
  self.status.text = @"Import your Dragon’s Crown Pro game folder to begin.";
  self.play = button(@"Play", self, @selector(startGame));
  self.importButton =
      button(@"Import Game Folder", self, @selector(importGame));
  UIButton *restore =
      button(@"Restore Save Checkpoint", self, @selector(restoreSave));
  self.restoreButton = restore;
  UIButton *share =
      button(@"Share Diagnostics", self, @selector(shareDiagnostics));
  UIStackView *stack = [[UIStackView alloc] initWithArrangedSubviews:@[
    self.status, self.play, self.importButton, restore, share
  ]];
  stack.axis = UILayoutConstraintAxisVertical;
  stack.spacing = 20;
  stack.translatesAutoresizingMaskIntoConstraints = NO;
  [self.view addSubview:stack];
  [NSLayoutConstraint activateConstraints:@[
    [stack.centerXAnchor
        constraintEqualToAnchor:self.view.safeAreaLayoutGuide.centerXAnchor],
    [stack.centerYAnchor
        constraintEqualToAnchor:self.view.safeAreaLayoutGuide.centerYAnchor],
    [stack.widthAnchor constraintLessThanOrEqualToConstant:460],
    [stack.leadingAnchor
        constraintGreaterThanOrEqualToAnchor:self.view.safeAreaLayoutGuide
                                                 .leadingAnchor
                                    constant:24],
    [stack.trailingAnchor
        constraintLessThanOrEqualToAnchor:self.view.safeAreaLayoutGuide
                                              .trailingAnchor
                                 constant:-24]
  ]];
  self.storage = [[DCPStorageController alloc]
      initWithDocumentsURL:[NSURL fileURLWithPath:documents() isDirectory:YES]];
  __weak GameController *weakSelf = self;
  self.storage.completionHandler = ^(NSError *error) {
    GameController *controller = weakSelf;
    if (!controller)
      return;
    controller->_storageBusy = NO;
    controller.status.text =
        error ? error.localizedDescription : @"Game imported. Ready to play.";
    [controller updateButtons];
  };
  NSError *error = nil;
  if (![self.storage recoverLocalStorageWithError:&error])
    self.status.text = error.localizedDescription;
  else if ([NSFileManager.defaultManager
               fileExistsAtPath:[documents() stringByAppendingPathComponent:
                                                 @"Game/eboot.bin"]])
    self.status.text = @"Ready to play.";
  [self updateButtons];
  dcp_game_set_paused(1);
  dcp_platform_initialize({(__bridge void *)self, activeChanged});
  [self emitLiveDiagnostics:YES];
}
- (void)viewDidAppear:(BOOL)animated {
  [super viewDidAppear:animated];
#if TARGET_OS_SIMULATOR
  saveUISnapshot(self.view.window, @"launcher-ui.png");
  if (!_autoStarted && std::getenv("DCP_SIM_AUTOPLAY")) {
    _autoStarted = YES;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC / 2),
                   dispatch_get_main_queue(), ^{
                     [self startGame];
                   });
  }
#endif
}
- (void)updateButtons {
  const BOOL busy = _running || _storageBusy || self.storage.importing ||
                    dcp_game_is_running();
  self.play.enabled =
      !busy && [NSFileManager.defaultManager
                   fileExistsAtPath:[documents() stringByAppendingPathComponent:
                                                     @"Game/eboot.bin"]];
  self.importButton.enabled = self.restoreButton.enabled = !busy;
}
- (void)importGame {
  if (_running || _storageBusy || dcp_game_is_running())
    return;
  _storageBusy = YES;
  [self.storage presentGameFolderPickerFrom:self];
  [self updateButtons];
}
- (void)startGame {
  if (_running || _storageBusy || self.storage.importing ||
      dcp_game_is_running())
    return;
  _running = YES;
  _sessionID = NSUUID.UUID.UUIDString;
  _sessionStarted = [NSDate.date timeIntervalSince1970];
  _diagnosticPhase = @"validation";
  _lastGuest = nil;
  _lastHeartbeat = 0;
  _presentedFrames = _replayedFrames = _replayedDraws = 0;
  _peakFootprint = _peakResident = 0;
  _snapshotHasContent = NO;
  _gameIntervals = FrameIntervals{};
  _drawableIntervals = FrameIntervals{};
  _hostError = nil;
  {
    std::lock_guard<std::mutex> lock(_diagnosticStateLock);
    _saveError = nil;
  }
  self.status.text = @"Checking game files and recovering local saves…";
  [self emitLiveDiagnostics:YES];
  [self updateButtons];
  dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
    NSString *failure = nil;
    try {
      const std::filesystem::path root(documents().fileSystemRepresentation);
      dcp::storage::validate_game(root / "Game");
      self->_saves = std::make_unique<dcp::storage::SaveTransactions>(
          root / "ProbeSaves", root / "SaveCheckpoints");
      if (!self->_saves->recover())
        throw std::runtime_error(self->_saves->error());
    } catch (const std::exception &exception) {
      failure = @(exception.what());
    }
    dispatch_async(dispatch_get_main_queue(), ^{
      if (failure) {
        self->_running = NO;
        self.status.text = failure;
        [self recordStartupFailure:failure stage:@"validation"];
        [self updateButtons];
      } else
        [self launchGame];
    });
  });
}
- (void)launchGame {
  if (!_sceneAvailable) {
    _running = NO;
    self.status.text = @"Game startup cancelled when the window closed.";
    [self recordStartupFailure:self.status.text stage:@"scene_closed"];
    [self updateButtons];
    return;
  }
  _hostError = nil;
  {
    std::lock_guard<std::mutex> lock(_diagnosticStateLock);
    _saveError = nil;
  }
  std::string failure;
  SDL_SetMainReady();
  SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
  if (!SDL_Init(SDL_INIT_VIDEO))
    failure = SDL_GetError();
  if (failure.empty()) {
    SDL_PropertiesID properties = SDL_CreateProperties();
    SDL_SetStringProperty(properties, SDL_PROP_WINDOW_CREATE_TITLE_STRING,
                          "OpenCrown");
    SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, 960);
    SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER,
                          540);
    SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_METAL_BOOLEAN,
                           true);
    SDL_SetBooleanProperty(
        properties, SDL_PROP_WINDOW_CREATE_HIGH_PIXEL_DENSITY_BOOLEAN, true);
    SDL_SetPointerProperty(properties,
                           SDL_PROP_WINDOW_CREATE_WINDOWSCENE_POINTER,
                           (__bridge void *)self.view.window.windowScene);
    _gameWindow = SDL_CreateWindowWithProperties(properties);
    SDL_DestroyProperties(properties);
    if (!_gameWindow)
      failure = SDL_GetError();
  }
  if (!failure.empty()) {
    [self unloadRenderer];
    _running = NO;
    self.status.text = @(failure.c_str());
    [self recordStartupFailure:self.status.text stage:@"SDL_startup"];
    [self updateButtons];
    return;
  }
  self.launcherWindow = self.view.window;
  UIWindow *window = (__bridge UIWindow *)SDL_GetPointerProperty(
      SDL_GetWindowProperties(_gameWindow),
      SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, nullptr);
  UIView *content = window.rootViewController.view;
  [self attachGameControls:content];
  setenv("DCP_GAME_ROOT",
         [documents() stringByAppendingPathComponent:@"Game"]
             .fileSystemRepresentation,
         1);
  setenv("DCP_SAVE_ROOT",
         [documents() stringByAppendingPathComponent:@"ProbeSaves"]
             .fileSystemRepresentation,
         1);
  setenv("DCP_GAME_MODE", "play", 1);
  if (!dcp_game_configure(
          {(__bridge void *)self, willUnload, saveTransaction})) {
    [self unloadRenderer];
    _running = NO;
    self.status.text = @"A previous game session is still stopping.";
    [self recordStartupFailure:self.status.text stage:@"runtime_busy"];
    [self updateButtons];
    return;
  }
  _diagnosticPhase = @"running";
  [self emitLiveDiagnostics:YES];
  self.displayLink =
      [CADisplayLink displayLinkWithTarget:self
                                  selector:@selector(renderFrame:)];
  self.displayLink.preferredFrameRateRange = CAFrameRateRangeMake(30, 60, 60);
  [self.displayLink addToRunLoop:NSRunLoop.mainRunLoop
                         forMode:NSRunLoopCommonModes];
  dcp_platform_set_paused(0);
  dcp_game_set_paused(!dcp_platform_active());
#if TARGET_OS_SIMULATOR
  if (const char *duration = std::getenv("DCP_SIM_STOP_SECONDS")) {
    double seconds = std::max(0.1, std::atof(duration));
    dispatch_after(
        dispatch_time(DISPATCH_TIME_NOW, int64_t(seconds * NSEC_PER_SEC)),
        dispatch_get_main_queue(), ^{
          if (self->_running)
            dcp_game_request_stop();
        });
  }
#endif
  dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
    const char *json = dcp_cpu_report();
    NSData *data = [NSData dataWithBytes:json length:strlen(json)];
    NSDictionary *guest = [NSJSONSerialization JSONObjectWithData:data
                                                          options:0
                                                            error:nil]
                              ?: @{};
    // Renderer/counter/UI ownership stays on main. Only immutable reports reach
    // the serial writer; the main thread never waits for guest completion.
    dispatch_async(dispatch_get_main_queue(), ^{
      [self unloadRenderer];
      dcp_platform_audio_stop();
      dcp_platform_set_paused(1);
      self->_running = NO;
      self->_diagnosticPhase = @"stopped";
      self->_lastGuest = guest;
      NSString *saveError = [self currentSaveError];
      NSString *error = self->_hostError.length
                            ? self->_hostError
                            : (saveError.length ? saveError : guest[@"error"]);
      char platformError[512]{};
      dcp_platform_copy_error(platformError, sizeof(platformError));
      self.status.text =
          error.length
              ? [@"Game stopped: " stringByAppendingString:error]
              : (platformError[0]
                     ? @"Game stopped. Diagnostics include the last "
                       @"audio/platform error."
                     : @"Game stopped. Local save checkpoints are available.");
      [self updateButtons];
      [self queueDiagnostic:[self diagnosticSnapshot] filename:@"diagnostics.json"];
      [self emitLiveDiagnostics:YES];
    });
  });
}

- (void)showPendingNameEntry {
  if (!_gameWindow || self.imeAlert || !dcp_platform_active())
    return;
  DcpImeRequest request;
  if (!dcp_ime_pending(&request))
    return;
  UIWindow *window = (__bridge UIWindow *)SDL_GetPointerProperty(
      SDL_GetWindowProperties(_gameWindow),
      SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, nullptr);
  UIViewController *presenter = window.rootViewController;
  if (!presenter || presenter.presentedViewController)
    return;
  auto string = [](const std::u16string &text) -> NSString * {
    return [[NSString alloc] initWithCharacters:
        reinterpret_cast<const unichar *>(text.data()) length:text.size()];
  };
  UIAlertController *alert = [UIAlertController
      alertControllerWithTitle:request.title.empty() ? @"Enter Name" : string(request.title)
                       message:nil preferredStyle:UIAlertControllerStyleAlert];
  [alert addTextFieldWithConfigurationHandler:^(UITextField *field) {
    field.text = string(request.text);
    field.autocorrectionType = UITextAutocorrectionTypeNo;
    field.spellCheckingType = UITextSpellCheckingTypeNo;
    field.clearButtonMode = UITextFieldViewModeWhileEditing;
  }];
  const uint64_t requestID = request.id;
  __weak GameController *weakSelf = self;
  [alert addAction:[UIAlertAction actionWithTitle:@"Cancel"
      style:UIAlertActionStyleCancel handler:^(UIAlertAction *) {
    dcp_ime_finish(requestID, nullptr);
    GameController *controller = weakSelf;
    controller.imeAlert = nil;
    if (controller && controller->_running)
      dcp_platform_set_paused(0);
  }]];
  __weak UIAlertController *weakAlert = alert;
  [alert addAction:[UIAlertAction actionWithTitle:@"Done"
      style:UIAlertActionStyleDefault handler:^(UIAlertAction *) {
    NSString *value = weakAlert.textFields.firstObject.text ?: @"";
    std::u16string text(value.length, 0);
    if (value.length)
      [value getCharacters:reinterpret_cast<unichar *>(text.data())
                     range:NSMakeRange(0, value.length)];
    dcp_ime_finish(requestID, &text);
    GameController *controller = weakSelf;
    controller.imeAlert = nil;
    if (controller && controller->_running)
      dcp_platform_set_paused(0);
  }]];
  self.imeAlert = alert;
  dcp_platform_set_paused(1);
  [presenter presentViewController:alert animated:YES completion:nil];
}
- (void)renderFrame:(CADisplayLink *)sender {
  [self emitLiveDiagnostics:NO];
  SDL_Event event;
  while (SDL_PollEvent(&event))
    if (event.type == SDL_EVENT_QUIT) {
      _hostError = @"SDL requested game shutdown.";
      NSLog(@"OpenCrown %@", _hostError);
      dcp_game_request_stop();
    }
  if (!_gameWindow || !dcp_platform_active())
    return;
  [self showPendingNameEntry];
  if (!dcp_platform_active())
    return;
  if (!_replay) {
    if (!dcp::render::capture_enabled())
      return;
    std::string setupError;
    _replay = std::make_unique<dcp::mobile::Replay>();
    if (!_replay->initialize(_gameWindow, false, &setupError)) {
      _replay.reset();
      self.status.text = _hostError = @(setupError.c_str());
      NSLog(@"OpenCrown Metal initialization failed: %@", _hostError);
      dcp_game_request_stop();
      return;
    }
    UIWindow *window = (__bridge UIWindow *)SDL_GetPointerProperty(
        SDL_GetWindowProperties(_gameWindow),
        SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, nullptr);
    [self attachGameControls:window.rootViewController.view];
#if TARGET_OS_SIMULATOR
    saveUISnapshot(window, @"controls-ui.png");
#endif
  }
  dcp::mobile::ReplayFrame frame;
  std::string error;
  auto result = _replay->pump(0, &frame, &error);
  if (result == dcp::mobile::PumpResult::Error) {
    self.status.text = _hostError = @(error.c_str());
    NSLog(@"OpenCrown render failed: %@", _hostError);
    dcp_game_request_stop();
    return;
  }
  if (result == dcp::mobile::PumpResult::Frame) {
    ++_replayedFrames;
    _gameIntervals.observe(sender.timestamp);
    _replayedDraws += frame.draws;
#if TARGET_OS_SIMULATOR
    if (frame.draws && !_snapshotHasContent &&
        std::getenv("DCP_SIM_UI_SNAPSHOT")) {
      bool varied = false;
      if (_replay->write_png(
          [documents() stringByAppendingPathComponent:@"first-game-frame.png"]
              .fileSystemRepresentation,
          &error, &varied))
        _snapshotHasContent = varied;
    }
#endif
  }
  int width = 0, height = 0;
  SDL_GetWindowSizeInPixels(_gameWindow, &width, &height);
  if (_replay->present(width, height)) {
    ++_presentedFrames;
    _drawableIntervals.observe(sender.timestamp);
  }
}
- (void)attachGameControls:(UIView *)content {
  dcp_platform_attach_touch_overlay((__bridge void *)content);
  if (!self.pauseButton) {
    self.pauseButton = button(@"Pause", self, @selector(pauseGame));
    self.pauseButton.translatesAutoresizingMaskIntoConstraints = NO;
  }
  if (self.pauseButton.superview != content) {
    if (self.pauseLayout)
      [NSLayoutConstraint deactivateConstraints:self.pauseLayout];
    [self.pauseButton removeFromSuperview];
    [content addSubview:self.pauseButton];
    self.pauseLayout = @[
      [self.pauseButton.topAnchor
          constraintEqualToAnchor:content.safeAreaLayoutGuide.topAnchor
                         constant:8],
      [self.pauseButton.trailingAnchor
          constraintEqualToAnchor:content.safeAreaLayoutGuide.trailingAnchor
                         constant:-12]
    ];
    [NSLayoutConstraint activateConstraints:self.pauseLayout];
  }
  [content bringSubviewToFront:self.pauseButton];
}
- (void)activeChanged:(BOOL)active {
  _gameIntervals.last = _drawableIntervals.last = 0;
  self.displayLink.paused = !active;
  if (!active && _replay && !_replay->suspend(true)) {
    _hostError = @"Metal could not drain during suspension.";
    dcp_game_request_stop();
  }
  dcp_game_set_paused(!active);
  if (active && _replay && !_replay->suspend(false)) {
    _hostError = @"Metal could not resume.";
    dcp_game_request_stop();
  }
  [self emitLiveDiagnostics:YES];
}
- (void)pauseGame {
  dcp_platform_set_paused(1);
  UIWindow *window = (__bridge UIWindow *)SDL_GetPointerProperty(
      SDL_GetWindowProperties(_gameWindow),
      SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, nullptr);
  UIAlertController *sheet = [UIAlertController
      alertControllerWithTitle:@"Paused"
                       message:@"Return to town to save your progress before "
                               @"stopping."
                preferredStyle:UIAlertControllerStyleAlert];
  [sheet addAction:[UIAlertAction actionWithTitle:@"Resume"
                                            style:UIAlertActionStyleCancel
                                          handler:^(UIAlertAction *) {
                                            dcp_platform_set_paused(0);
                                          }]];
  [sheet addAction:[UIAlertAction actionWithTitle:@"Stop Game"
                                            style:UIAlertActionStyleDestructive
                                          handler:^(UIAlertAction *) {
                                            dcp_game_request_stop();
                                          }]];
  [window.rootViewController presentViewController:sheet
                                          animated:YES
                                        completion:nil];
}
- (void)unloadRenderer {
  [self.imeAlert dismissViewControllerAnimated:NO completion:nil];
  self.imeAlert = nil;
  [self.displayLink invalidate];
  self.displayLink = nil;
  dcp_platform_attach_touch_overlay(nullptr);
  if (self.pauseLayout)
    [NSLayoutConstraint deactivateConstraints:self.pauseLayout];
  self.pauseLayout = nil;
  [self.pauseButton removeFromSuperview];
  dcp_platform_audio_stop();
#if TARGET_OS_SIMULATOR
  if (_replay && _replayedFrames && std::getenv("DCP_SIM_UI_SNAPSHOT")) {
    std::string imageError;
    bool varied = false;
    NSString *path = [documents() stringByAppendingPathComponent:@"last-game-frame.png"];
    if (_replay->write_png(path.fileSystemRepresentation, &imageError, &varied))
      _snapshotHasContent = _snapshotHasContent || varied;
    else
      NSLog(@"OpenCrown final readback: %s", imageError.c_str());
  }
#endif
  if (_replay && !_replay->suspend(true))
    _hostError = @"Metal could not drain during shutdown.";
  _replay.reset();
  if (_gameWindow) {
    SDL_DestroyWindow(_gameWindow);
    _gameWindow = nullptr;
  }
  if (self.launcherWindow.windowScene.activationState !=
      UISceneActivationStateUnattached)
    [self.launcherWindow makeKeyAndVisible];
  self.launcherWindow = nil;
  self.pauseButton = nil;
}
- (BOOL)saveTransaction:(BOOL)begin {
  BOOL success = _saves && (begin ? _saves->begin() : _saves->commit());
  if (!success) {
    std::lock_guard<std::mutex> lock(_diagnosticStateLock);
    _saveError = _saves ? @(_saves->error().c_str())
                        : @"Save transaction storage is unavailable.";
  }
  return success;
}
- (void)sceneAttached {
  _sceneAvailable = YES;
}
- (void)sceneDetached {
  _sceneAvailable = NO;
  self.launcherWindow = nil;
  dcp_platform_set_paused(1);
  dcp_game_request_stop();
  [self.storage cancelImport];
  if (!_restoring && !self.storage.importing)
    _storageBusy = NO;
  [self dismissViewControllerAnimated:NO completion:nil];
}
- (void)restoreSave {
  if (_running || _storageBusy || self.storage.importing ||
      dcp_game_is_running())
    return;
  NSString *root =
      [documents() stringByAppendingPathComponent:@"SaveCheckpoints"];
  NSArray<NSString *> *entries =
      [NSFileManager.defaultManager contentsOfDirectoryAtPath:root error:nil]
          ?: @[];
  NSMutableArray<NSString *> *ids = [NSMutableArray array];
  NSCharacterSet *invalid = [[NSCharacterSet
      characterSetWithCharactersInString:@"0123456789abcdef"] invertedSet];
  for (NSString *name in entries)
    if (name.length == 32 &&
        [name rangeOfCharacterFromSet:invalid].location == NSNotFound)
      [ids addObject:name];
  [ids sortUsingComparator:^NSComparisonResult(NSString *a, NSString *b) {
    NSDictionary *left = [NSFileManager.defaultManager
        attributesOfItemAtPath:[root stringByAppendingPathComponent:a]
                         error:nil];
    NSDictionary *right = [NSFileManager.defaultManager
        attributesOfItemAtPath:[root stringByAppendingPathComponent:b]
                         error:nil];
    return [right[NSFileModificationDate] compare:left[NSFileModificationDate]];
  }];
  if (!ids.count) {
    self.status.text = @"No completed local save checkpoints are available.";
    return;
  }
  _storageBusy = YES;
  [self updateButtons];
  UIAlertController *sheet = [UIAlertController
      alertControllerWithTitle:@"Restore Save"
                       message:@"Choose a local checkpoint. The current save "
                               @"is retained as a previous copy."
                preferredStyle:UIAlertControllerStyleActionSheet];
  for (NSString *name in
       [ids subarrayWithRange:NSMakeRange(0, MIN(ids.count, 10))]) {
    NSString *path = [root stringByAppendingPathComponent:name];
    NSDate *date = [NSFileManager.defaultManager
        attributesOfItemAtPath:path
                         error:nil][NSFileModificationDate];
    NSString *title =
        date ? [NSDateFormatter
                   localizedStringFromDate:date
                                 dateStyle:NSDateFormatterMediumStyle
                                 timeStyle:NSDateFormatterMediumStyle]
             : name;
    [sheet addAction:
               [UIAlertAction
                   actionWithTitle:title
                             style:UIAlertActionStyleDefault
                           handler:^(UIAlertAction *) {
                             if (self->_running || dcp_game_is_running() ||
                                 self.storage.importing) {
                               self->_storageBusy = NO;
                               [self updateButtons];
                               return;
                             }
                             self->_restoring = YES;
                             self->_storageBusy = YES;
                             [self updateButtons];
                             self.status.text =
                                 @"Validating and restoring save checkpoint…";
                             dispatch_async(
                                 dispatch_get_global_queue(
                                     QOS_CLASS_USER_INITIATED, 0),
                                 ^{
                                   NSString *failure = nil;
                                   try {
                                     dcp::storage::restore_checkpoint(
                                         path.fileSystemRepresentation,
                                         [documents()
                                             stringByAppendingPathComponent:
                                                 @"ProbeSaves"]
                                             .fileSystemRepresentation);
                                   } catch (const std::exception &exception) {
                                     failure = @(exception.what());
                                   }
                                   dispatch_async(dispatch_get_main_queue(), ^{
                                     self->_storageBusy = self->_restoring = NO;
                                     self.status.text =
                                         failure
                                             ?: @"Save checkpoint restored.";
                                     [self updateButtons];
                                   });
                                 });
                           }]];
  }
  [sheet addAction:[UIAlertAction actionWithTitle:@"Cancel"
                                            style:UIAlertActionStyleCancel
                                          handler:^(UIAlertAction *) {
                                            self->_storageBusy = NO;
                                            [self updateButtons];
                                          }]];
  sheet.popoverPresentationController.sourceView = self.view;
  sheet.popoverPresentationController.sourceRect = CGRectMake(
      CGRectGetMidX(self.view.bounds), CGRectGetMidY(self.view.bounds), 1, 1);
  [self presentViewController:sheet animated:YES completion:nil];
}
- (NSString *)currentSaveError {
  std::lock_guard<std::mutex> lock(_diagnosticStateLock);
  return _saveError ?: @"";
}
- (NSDictionary *)diagnosticSnapshot {
  NSAssert(NSThread.isMainThread,
           @"Diagnostics snapshots own frontend state on main");
  char platformError[512]{};
  dcp_platform_copy_error(platformError, sizeof(platformError));
  const auto thermal = NSProcessInfo.processInfo.thermalState;
  static NSDictionary *gameArtifact;
  static dispatch_once_t artifactOnce;
  dispatch_once(&artifactOnce, ^{
    NSURL *url = [NSBundle.mainBundle URLForResource:@"build-inputs" withExtension:@"json"];
    NSData *data = url ? [NSData dataWithContentsOfURL:url] : nil;
    id value = data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:nil] : nil;
    gameArtifact = [value isKindOfClass:NSDictionary.class] ? value : @{};
  });
  NSMutableDictionary *memory = [memorySnapshot() mutableCopy];
  if ([memory[@"available"] boolValue]) {
    _peakFootprint = std::max(
        _peakFootprint, [memory[@"phys_footprint_bytes"] unsignedLongLongValue]);
    _peakResident = std::max(
        _peakResident, [memory[@"resident_bytes"] unsignedLongLongValue]);
  }
  memory[@"sampled_peak_phys_footprint_bytes"] = @(_peakFootprint);
  memory[@"sampled_peak_resident_bytes"] = @(_peakResident);
  memory[@"peak_measurement"] =
      @"Maximum session heartbeat samples; not OS high-water marks";
  return @{
    @"build_id" : NSBundle.mainBundle.infoDictionary[@"DCPBuildID"]
        ?: @"unknown",
    @"app_version" : NSBundle.mainBundle.infoDictionary[@"CFBundleShortVersionString"] ?: @"unknown",
    @"app_build" : NSBundle.mainBundle.infoDictionary[@"CFBundleVersion"] ?: @"unknown",
    @"extended_memory_requested" : @([NSBundle.mainBundle.infoDictionary[@"DCPExtendedMemoryRequested"] boolValue]),
    @"game_artifact" : gameArtifact,
    @"nonuniform_snapshot_saved" : @(_snapshotHasContent),
    @"session_id" : _sessionID ?: @"unknown",
    @"session_started_unix" : @(_sessionStarted),
    @"timestamp" : @([NSDate.date timeIntervalSince1970]),
    @"phase" : _diagnosticPhase ?: @"launcher",
    @"os" : NSProcessInfo.processInfo.operatingSystemVersionString,
    @"guest" : _lastGuest ?: guestSnapshot(),
    @"captured_game_frames" : @(_replayedFrames),
    @"drawable_presents" : @(_presentedFrames),
    @"replayed_game_frames" : @(_replayedFrames),
    @"presented_frames" : @(_presentedFrames),
    @"replayed_draws" : @(_replayedDraws),
    @"captured_frame_intervals" : _gameIntervals.json(),
    @"drawable_present_intervals" : _drawableIntervals.json(),
    @"interval_measurement" :
        @"Main-thread captured-frame arrivals and successful drawable-present "
        @"calls; paused gaps excluded",
    @"frontend_error" : _hostError ?: @"",
    @"save_error" : [self currentSaveError],
    @"last_platform_error" : @(platformError),
    @"audio" : audioSnapshot(),
    @"metal" : _metalInfo ?: @{},
    @"thermal_state" : thermalName(thermal),
    @"thermal_state_raw" : @(thermal),
    @"memory" : memory,
    @"launcher_status" : self.status.text ?: @""
  };
}
- (void)queueDiagnostic:(NSDictionary *)report filename:(NSString *)filename {
  if (!_diagnosticQueue)
    return;
  NSError *error = nil;
  NSData *data = [NSJSONSerialization
      dataWithJSONObject:report
                 options:NSJSONWritingPrettyPrinted | NSJSONWritingSortedKeys
                   error:&error];
  if (!data) {
    NSLog(@"OpenCrown diagnostics serialization failed: %@",
          error.localizedDescription);
    return;
  }
  NSData *immutable = [data copy];
  NSString *path = [documents() stringByAppendingPathComponent:filename];
  dispatch_async(_diagnosticQueue, ^{
    NSError *writeError = nil;
    if (![immutable writeToFile:path
                        options:NSDataWritingAtomic
                          error:&writeError])
      NSLog(@"OpenCrown diagnostics write failed: %@",
            writeError.localizedDescription);
  });
}
- (void)emitLiveDiagnostics:(BOOL)force {
  if (!_diagnosticQueue)
    return;
  const double now = CACurrentMediaTime();
  if (!force && now - _lastHeartbeat < 1.0)
    return;
  _lastHeartbeat = now;
  [self queueDiagnostic:[self diagnosticSnapshot]
               filename:@"diagnostics-live.json"];
}
- (void)recordStartupFailure:(NSString *)error stage:(NSString *)stage {
  _diagnosticPhase = stage;
  _hostError = error ?: @"unknown startup error";
  _lastGuest = @{
    @"booted" : @NO,
    @"game_code_executed" : @NO,
    @"stage" : stage,
    @"error" : error ?: @"unknown startup error"
  };
  [self queueDiagnostic:[self diagnosticSnapshot] filename:@"diagnostics.json"];
  [self emitLiveDiagnostics:YES];
}
- (void)shareDiagnostics {
  [self emitLiveDiagnostics:YES];
  dispatch_async(_diagnosticQueue, ^{
    NSMutableArray<NSURL *> *files = [NSMutableArray array];
    for (NSString *name in @[ @"diagnostics-live.json", @"diagnostics.json" ]) {
      NSURL *url = [NSURL
          fileURLWithPath:[documents() stringByAppendingPathComponent:name]];
      if ([NSFileManager.defaultManager fileExistsAtPath:url.path])
        [files addObject:url];
    }
    NSArray<NSURL *> *immutable = [files copy];
    dispatch_async(dispatch_get_main_queue(), ^{
      if (!immutable.count) {
        self.status.text =
            @"Diagnostics could not be written. Check local storage.";
        return;
      }
      UIActivityViewController *sheet =
          [[UIActivityViewController alloc] initWithActivityItems:immutable
                                            applicationActivities:nil];
      sheet.popoverPresentationController.sourceView = self.view;
      sheet.popoverPresentationController.sourceRect =
          CGRectMake(CGRectGetMidX(self.view.bounds),
                     CGRectGetMidY(self.view.bounds), 1, 1);
      [self presentViewController:sheet animated:YES completion:nil];
    });
  });
}

@end

static GameController *applicationController;

@interface SceneDelegate : UIResponder <UIWindowSceneDelegate>
@property(nonatomic, strong) UIWindow *window;
@end
@implementation SceneDelegate
- (void)scene:(UIScene *)scene
    willConnectToSession:(UISceneSession *)session
                 options:(UISceneConnectionOptions *)options {
  (void)session;
  (void)options;
  self.window = [[UIWindow alloc] initWithWindowScene:(UIWindowScene *)scene];
  // One app-lifetime owner keeps platform callback context alive across scene
  // discard.
  if (!applicationController)
    applicationController = [[GameController alloc] init];
  if ([applicationController.parentViewController
          isKindOfClass:UINavigationController.class])
    [(UINavigationController *)applicationController.parentViewController
        setViewControllers:@[]
                  animated:NO];
  self.window.rootViewController = [[UINavigationController alloc]
      initWithRootViewController:applicationController];
  [applicationController sceneAttached];
  [self.window makeKeyAndVisible];
}
- (void)sceneDidDisconnect:(UIScene *)scene {
  (void)scene;
  [applicationController sceneDetached];
}
@end
@interface AppDelegate : UIResponder <UIApplicationDelegate>
@end
@implementation AppDelegate
- (UISceneConfiguration *)application:(UIApplication *)app
    configurationForConnectingSceneSession:(UISceneSession *)session
                                   options:(UISceneConnectionOptions *)options {
  (void)app;
  (void)options;
  UISceneConfiguration *config =
      [[UISceneConfiguration alloc] initWithName:@"OpenCrown"
                                     sessionRole:session.role];
  config.delegateClass = SceneDelegate.class;
  return config;
}
@end
int main(int argc, char **argv) {
  @autoreleasepool {
    return UIApplicationMain(argc, argv, nil,
                             NSStringFromClass(AppDelegate.class));
  }
}
