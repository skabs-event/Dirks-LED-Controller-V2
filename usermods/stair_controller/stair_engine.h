#pragma once
#include <stdint.h>
#include <math.h>

// ─────────────────────────────────────────────────────────
// StairEngine — Wave Animation (C++ port of JS wave logic)
// Triggered by PIR: rest colour → neutral wave → fade back
// ─────────────────────────────────────────────────────────

#define SC_BASE_PX   14   // addressable segments per metre (WS2814 FCOB)
#define SC_MAX_STEPS 18

struct RGBW_t { uint8_t r, g, b, w; };

struct WaveParams {
  // Rest colours: left side (rosa) + right side (warm white)
  uint8_t leftR,  leftG,  leftB,  leftW;
  uint8_t rightR, rightG, rightB, rightW;
  // Trigger colour (neutral white)
  uint8_t neutralR, neutralG, neutralB, neutralW;
  // Edge accent colour (baby blue)
  uint8_t edgeR, edgeG, edgeB, edgeW;

  uint8_t  bri;          // trigger brightness 0-255
  uint8_t  restBri;      // rest brightness 0-255
  uint16_t stepDelay;    // ms delay per step in wave
  uint16_t sideDelay;    // ms delay per side-pixel in wave
  uint16_t fade;         // ms fade-in time
  uint16_t hold;         // seconds to hold trigger colour
  uint16_t retDelay;     // ms per step in return wave
  uint8_t  retSpread;    // steps of spread in return fade
  uint8_t  split;        // colour split midpoint % (0-100)
  uint8_t  blend;        // colour blend width % (0-100)

  uint8_t  numSteps;
  uint8_t  stepCounts[SC_MAX_STEPS]; // pixels per step
  bool     serpentine;
};

class StairEngine {
private:
  WaveParams p;
  bool       running = false;
  uint32_t   startMs = 0;

  // ── Math helpers ──────────────────────────────────────
  static float ss(float e0, float e1, float x) {
    float t = (x - e0) / (e1 - e0 + 1e-6f);
    t = t < 0.f ? 0.f : t > 1.f ? 1.f : t;
    return t * t * (3.f - 2.f * t);
  }
  static float cf(float v) { return v < 0.f ? 0.f : v > 1.f ? 1.f : v; }

  // ── Geometry ──────────────────────────────────────────
  int ledIndex(int s, int px) const {
    int base = 0;
    for (int i = 0; i < s; i++) base += p.stepCounts[i];
    int cnt = p.stepCounts[s];
    return (p.serpentine && (s & 1)) ? base + (cnt - 1 - px) : base + px;
  }

  // Physical coordinate relative to standard 14-pixel row
  int physCoord(int s, int px) const {
    int cnt = p.stepCounts[s];
    int extraL = (cnt > SC_BASE_PX) ? (cnt - SC_BASE_PX + 1) / 2 : 0;
    return px - extraL;
  }

  // Distance from centre (centre = between px 6 and 7 of 14)
  int sideDist(int s, int px) const {
    int c = physCoord(s, px);
    return (c <= 6) ? (6 - c) : (c - 7);
  }

  int maxSideDist() const {
    int m = 0;
    for (int s = 0; s < p.numSteps; s++)
      for (int px = 0; px < p.stepCounts[s]; px++) {
        int d = sideDist(s, px);
        if (d > m) m = d;
      }
    return m;
  }

  uint32_t waveStartMs(int s, int px) const {
    return (uint32_t)s * p.stepDelay + sideDist(s, px) * p.sideDelay;
  }

  uint32_t fullWaveMs() const {
    return (uint32_t)(p.numSteps - 1) * p.stepDelay
         + maxSideDist() * p.sideDelay
         + p.fade;
  }

  uint32_t retFadeMs() const {
    uint32_t rf = (uint32_t)p.retDelay * p.retSpread;
    return rf > p.fade ? rf : p.fade;
  }

  uint32_t totalSeqMs() const {
    return fullWaveMs()
         + (uint32_t)p.hold * 1000UL
         + (uint32_t)(p.numSteps - 1) * p.retDelay
         + retFadeMs();
  }

  // How much trigger colour (0-1) at elapsed time el
  float neutralAmount(int s, int px, uint32_t el) const {
    uint32_t wd  = fullWaveMs();
    uint32_t rs  = wd + (uint32_t)p.hold * 1000UL;
    uint32_t st  = waveStartMs(s, px);
    float    up  = ss((float)st, (float)(st + p.fade), (float)el);
    if (el < rs) return up;
    uint32_t bs  = rs + (uint32_t)s * p.retDelay;
    uint32_t rf  = retFadeMs();
    return cf(up * (1.f - ss((float)bs, (float)(bs + rf), (float)el)));
  }

  // ── Colour helpers ────────────────────────────────────
  RGBW_t restColour(int s, int px) const {
    int   cnt = p.stepCounts[s];
    float x   = (cnt <= 1) ? 0.f : (float)px / (float)(cnt - 1);
    float sp  = p.split / 100.f;
    float bw  = p.blend / 100.f;
    float t   = ss(sp - bw / 2.f, sp + bw / 2.f, x);
    return {
      (uint8_t)(p.leftR + (int)(p.rightR - p.leftR) * t),
      (uint8_t)(p.leftG + (int)(p.rightG - p.leftG) * t),
      (uint8_t)(p.leftB + (int)(p.rightB - p.leftB) * t),
      (uint8_t)(p.leftW + (int)(p.rightW - p.leftW) * t)
    };
  }

  RGBW_t neutralColour(int s, int px) const {
    int   cnt = p.stepCounts[s];
    int   d   = px < cnt - 1 - px ? px : cnt - 1 - px;
    float ea  = (d == 0) ? 1.f : (d == 1) ? 0.88f : (d == 2) ? 0.68f : (d == 3) ? 0.28f : 0.f;
    return {
      (uint8_t)(p.neutralR + (int)(p.edgeR - p.neutralR) * ea),
      (uint8_t)(p.neutralG + (int)(p.edgeG - p.neutralG) * ea),
      (uint8_t)(p.neutralB + (int)(p.edgeB - p.neutralB) * ea),
      (uint8_t)(p.neutralW + (int)(p.edgeW - p.neutralW) * ea)
    };
  }

  static RGBW_t mix(RGBW_t a, RGBW_t b, float t) {
    return {
      (uint8_t)(a.r + (int)(b.r - a.r) * t),
      (uint8_t)(a.g + (int)(b.g - a.g) * t),
      (uint8_t)(a.b + (int)(b.b - a.b) * t),
      (uint8_t)(a.w + (int)(b.w - a.w) * t)
    };
  }

public:
  void setParams(const WaveParams& params) { p = params; }

  void trigger() { running = true; startMs = millis(); }
  void stop()    { running = false; }
  bool isRunning() const { return running; }

  // Apply static rest colours — call when not animating
  void applyRest() const {
    float bri = p.restBri / 255.f;
    for (int s = 0; s < p.numSteps; s++)
      for (int px = 0; px < p.stepCounts[s]; px++) {
        RGBW_t c = restColour(s, px);
        strip.setPixelColor(ledIndex(s, px),
          RGBW32((uint8_t)(c.r*bri), (uint8_t)(c.g*bri),
                 (uint8_t)(c.b*bri), (uint8_t)(c.w*bri)));
      }
  }

  // Call each frame — returns false when animation finished
  bool tick() {
    if (!running) return false;
    uint32_t el = millis() - startMs;
    float    rb = p.restBri / 255.f;
    float    mb = p.bri     / 255.f;

    for (int s = 0; s < p.numSteps; s++)
      for (int px = 0; px < p.stepCounts[s]; px++) {
        float    n    = neutralAmount(s, px, el);
        RGBW_t   rc   = restColour(s, px);
        RGBW_t   nc   = neutralColour(s, px);
        RGBW_t   col  = mix(rc, nc, n);
        float    bri  = rb + (mb - rb) * n;
        strip.setPixelColor(ledIndex(s, px),
          RGBW32((uint8_t)(col.r*bri), (uint8_t)(col.g*bri),
                 (uint8_t)(col.b*bri), (uint8_t)(col.w*bri)));
      }

    if (el >= totalSeqMs()) { running = false; applyRest(); return false; }
    return true;
  }
};
