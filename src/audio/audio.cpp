// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/audio.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#include "base/rng.hpp"

#ifdef CB_HAVE_SDL_AUDIO
#include <SDL3/SDL.h>
#endif

namespace cb::audio {
namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 2.0f * kPi;
/// Full scale for a signed 16-bit sample. Buffers are normalised well below it.
constexpr float kFullScale = 32767.0f;

/// A quick attack then an exponential decay: the shape that reads as a percussive
/// "something happened" without needing a sustain of any length. `attack` is kept away
/// from zero by the callers so t = 0 is silent rather than a click of its own.
float envelope(float t, float attack, float decay) noexcept {
  const float a = attack > 0.0f ? std::min(1.0f, t / attack) : 1.0f;
  return a * std::exp(-t / decay);
}

/// Uniform noise in [-1, 1) from the deterministic generator. The seed is fixed per
/// sound, so the "noise" is the same bytes on every machine and every run.
float noise(cb::Rng& rng) noexcept {
  const auto r = static_cast<std::uint32_t>(rng.next() >> 32);
  return static_cast<float>(r) * (2.0f / 4294967295.0f) - 1.0f;
}

/// The peak each buffer is normalised to. Normalising rather than summing fixed
/// amplitudes is what makes "does not clip" true by construction whatever the formula
/// does - no component can push the result past this.
float peakFor(Sound s) noexcept {
  switch (s) {
    case Sound::Click:
      return 0.28f;
    case Sound::Move:
      return 0.55f;
    case Sound::Capture:
      return 0.70f;
    case Sound::Check:
      return 0.55f;
  }
  return 0.5f;
}

std::uint64_t seedFor(Sound s) noexcept {
  switch (s) {
    case Sound::Click:
      return 0x9E3779B97F4A7C15ull;
    case Sound::Move:
      return 0xBF58476D1CE4E5B9ull;
    case Sound::Capture:
      return 0x94D049BB133111EBull;
    case Sound::Check:
      return 0xD1B54A32D192ED03ull;
  }
  return 0x1234567890ABCDEFull;
}

}  // namespace

float duration(Sound s) noexcept {
  switch (s) {
    case Sound::Click:
      return 0.04f;
    case Sound::Move:
      return 0.11f;
    case Sound::Capture:
      return 0.17f;
    case Sound::Check:
      return 0.19f;
  }
  return 0.10f;
}

int sampleCount(Sound s) noexcept {
  return static_cast<int>(static_cast<float>(kSampleRate) * duration(s));
}

std::vector<std::int16_t> synthesize(Sound s) {
  const int n = std::max(0, sampleCount(s));
  std::vector<float> mix(static_cast<std::size_t>(n), 0.0f);
  cb::Rng rng(seedFor(s));
  const float rate = static_cast<float>(kSampleRate);
  for (int i = 0; i < n; ++i) {
    const float t = static_cast<float>(i) / rate;
    float v = 0.0f;
    switch (s) {
      case Sound::Click:
        // A soft, brief blip: a fundamental and its octave, gone almost at once.
        v = 0.75f * std::sin(kTwoPi * 1400.0f * t) +
            0.25f * std::sin(kTwoPi * 2800.0f * t);
        v *= envelope(t, 0.0008f, 0.010f);
        break;
      case Sound::Move:
        // A wooden landing: a low body tone with a short noise knock on the front.
        v = 0.72f * std::sin(kTwoPi * 300.0f * t) + 0.35f * noise(rng);
        v *= envelope(t, 0.0015f, 0.045f);
        break;
      case Sound::Capture:
        // Lower and sharper than a landing, with more noise: this one is an event.
        v = 0.85f * std::sin(kTwoPi * 170.0f * t) + 0.55f * noise(rng);
        v *= envelope(t, 0.0008f, 0.070f);
        break;
      case Sound::Check: {
        // Two notes, rising: unmistakably not the landing sound and not the capture.
        const float split = 0.075f;
        if (t < split) {
          v = std::sin(kTwoPi * 700.0f * t) * envelope(t, 0.0020f, 0.025f);
        } else {
          const float u = t - split;
          v = std::sin(kTwoPi * 1050.0f * u) * envelope(u, 0.0020f, 0.045f);
        }
        break;
      }
    }
    mix[static_cast<std::size_t>(i)] = v;
  }

  float peak = 0.0f;
  for (const float v : mix) peak = std::max(peak, std::abs(v));
  const float scale = peak > 1e-6f ? peakFor(s) / peak : 0.0f;

  std::vector<std::int16_t> out(static_cast<std::size_t>(n), 0);
  for (int i = 0; i < n; ++i) {
    const float scaled = mix[static_cast<std::size_t>(i)] * scale * kFullScale;
    out[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(std::lround(scaled));
  }
  return out;
}

const std::vector<std::int16_t>& samples(Sound s) {
  static const std::array<std::vector<std::int16_t>, 4> kCache = {
      synthesize(Sound::Click), synthesize(Sound::Move), synthesize(Sound::Capture),
      synthesize(Sound::Check)};
  return kCache[static_cast<std::size_t>(s)];
}

std::vector<std::int16_t> applyGain(const std::vector<std::int16_t>& pcm, float gain) {
  std::vector<std::int16_t> out(pcm.size(), 0);
  if (gain <= 0.0f) return out;  // off is off, not merely quieter
  const float g = std::min(gain, 1.0f);
  for (std::size_t i = 0; i < pcm.size(); ++i) {
    out[i] = static_cast<std::int16_t>(std::lround(static_cast<float>(pcm[i]) * g));
  }
  return out;
}

float effectsGain(float master, float effects) noexcept {
  const float m = std::clamp(master, 0.0f, 1.0f);
  const float e = std::clamp(effects, 0.0f, 1.0f);
  return m * e;
}

namespace {
Sink*& sinkSlot() noexcept {
  static Sink* installed = nullptr;
  return installed;
}
}  // namespace

void setSink(Sink* s) noexcept {
  sinkSlot() = s;
}

Sink* sink() noexcept {
  return sinkSlot();
}

void play(Sound s) noexcept {
  if (Sink* out = sinkSlot(); out != nullptr) out->play(s);
}

#ifdef CB_HAVE_SDL_AUDIO
struct Mixer::Impl {
  SDL_AudioStream* stream{nullptr};
};
#else
struct Mixer::Impl {};
#endif

Mixer::Mixer() : impl_(std::make_unique<Impl>()) {}

Mixer::~Mixer() {
#ifdef CB_HAVE_SDL_AUDIO
  if (impl_ != nullptr && impl_->stream != nullptr) {
    // Destroying a stream created by `SDL_OpenAudioDeviceStream` also closes its device.
    SDL_DestroyAudioStream(impl_->stream);
    impl_->stream = nullptr;
  }
#endif
}

std::unique_ptr<Mixer> Mixer::create() {
#ifdef CB_HAVE_SDL_AUDIO
  if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    std::fprintf(stderr, "endspielzero: audio unavailable (%s); continuing silent\n",
                 SDL_GetError());
    return nullptr;
  }
  SDL_AudioSpec spec{};
  spec.format = SDL_AUDIO_S16;
  spec.channels = 1;
  spec.freq = kSampleRate;
  // A null callback leaves the stream in "put" mode: SDL pulls from the queue we fill.
  SDL_AudioStream* stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                                      &spec, nullptr, nullptr);
  if (stream == nullptr) {
    std::fprintf(stderr, "endspielzero: no audio device (%s); continuing silent\n",
                 SDL_GetError());
    return nullptr;
  }
  // `SDL_OpenAudioDeviceStream` opens paused.
  if (!SDL_ResumeAudioStreamDevice(stream)) {
    std::fprintf(stderr, "endspielzero: cannot start audio (%s); continuing silent\n",
                 SDL_GetError());
    SDL_DestroyAudioStream(stream);
    return nullptr;
  }
  auto mixer = std::unique_ptr<Mixer>(new Mixer());
  mixer->impl_->stream = stream;
  return mixer;
#else
  return nullptr;
#endif
}

void Mixer::setVolumes(float master, float effects) noexcept {
  gain_ = effectsGain(master, effects);
}

void Mixer::play(Sound s) {
#ifdef CB_HAVE_SDL_AUDIO
  if (gain_ <= 0.0f || impl_ == nullptr || impl_->stream == nullptr) return;
  const std::vector<std::int16_t>& pcm = samples(s);
  if (pcm.empty()) return;
  constexpr int kBytesPerSample = static_cast<int>(sizeof(std::int16_t));
  // A rapid run of UI clicks must not build an unbounded backlog and lag behind the
  // interface: once more than about a quarter second is queued, drop it and start this
  // sound now. The sounds are short, so a dropped tail is a fraction of one effect.
  if (SDL_GetAudioStreamQueued(impl_->stream) > kSampleRate / 4 * kBytesPerSample) {
    (void)SDL_ClearAudioStream(impl_->stream);
  }
  if (gain_ >= 1.0f) {
    (void)SDL_PutAudioStreamData(impl_->stream, pcm.data(),
                                 static_cast<int>(pcm.size()) * kBytesPerSample);
  } else {
    const std::vector<std::int16_t> scaled = applyGain(pcm, gain_);
    (void)SDL_PutAudioStreamData(impl_->stream, scaled.data(),
                                 static_cast<int>(scaled.size()) * kBytesPerSample);
  }
#else
  (void)s;
#endif
}

}  // namespace cb::audio
