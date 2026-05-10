#pragma once
#include <stdint.h>
#include <math.h>

// ─────────────────────────────────────────────────────────
// StairDisco — Audio-Reactive VU-Meter Effect
// Audio split: Bass → steps 1-4 · Mids → 5-8 · Highs → 9-18
// Each step: meter expands from centre outward (baby blue → yellow)
// Rosa base colour shown on unlit pixels
// ─────────────────────────────────────────────────────────

struct DiscoParams {
  // VU Meter base colour (rosa background when step is quiet)
  uint8_t vuBaseR, vuBaseG, vuBaseB, vuBaseW;
  uint8_t vuBaseDim;   // dim factor 1-60 (%)

  // Fallback colours for beat/pulse effects
  uint8_t bassR, bassG, bassB, bassW;
  uint8_t midR,  midG,  midB,  midW;
  uint8_t highR, highG, highB, highW;

  uint8_t bri;         // overall brightness 0-255
  uint8_t gain;        // mic gain 0-255 (128 = 1x)
  uint8_t beatThresh;  // beat threshold 0-255

  uint8_t effectType;  // 0=vumeter, 1=beat, 2=freqBands, 3=colorPulse

  uint8_t numSteps;
  uint8_t stepCounts[SC_MAX_STEPS];
  bool    serpentine;
};

class StairDisco {
private:
  DiscoParams p;
  float       vuSmooth[SC_MAX_STEPS] = {};

  static float cf(float v) { return v < 0.f ? 0.f : v > 1.f ? 1.f : v; }

  int ledIndex(int s, int px) const {
    int base = 0;
    for (int i = 0; i < s; i++) base += p.stepCounts[i];
    int cnt = p.stepCounts[s];
    return (p.serpentine && (s & 1)) ? base + (cnt - 1 - px) : base + px;
  }

  // ── VU-Meter: centre-expand with blue→yellow gradient ─
  void applyVuMeter(const uint8_t* bands) {
    float gain = p.gain / 128.f;
    float bri  = p.bri  / 255.f;

    // Band averages → level 0-1
    float bassL = 0.f, midL = 0.f, highL = 0.f;
    for (int i = 0; i < 4;  i++) bassL += bands[i];
    for (int i = 4; i < 9;  i++) midL  += bands[i];
    for (int i = 9; i < 16; i++) highL += bands[i];
    bassL = cf(bassL / 4.f  / 255.f * gain);
    midL  = cf(midL  / 5.f  / 255.f * gain);
    highL = cf(highL / 7.f  / 255.f * gain);

    // Attack faster than release — like a real VU meter
    const float ATTACK  = 0.65f;
    const float RELEASE = 0.05f;

    for (int s = 0; s < p.numSteps; s++) {
      float rawLvl = (s < 4) ? bassL : (s < 8) ? midL : highL;
      float alpha  = (rawLvl > vuSmooth[s]) ? ATTACK : RELEASE;
      vuSmooth[s] += (rawLvl - vuSmooth[s]) * alpha;
      float lvl = vuSmooth[s];

      int   cnt     = p.stepCounts[s];
      float centre  = (cnt - 1) / 2.f;
      float activeR = lvl * centre;

      for (int px = 0; px < cnt; px++) {
        float   dist = fabsf((float)px - centre);
        uint8_t r, g, b, w;

        if (activeR > 0.001f && dist <= activeR) {
          // Lit zone: baby blue (centre) → yellow (edge)
          float t = dist / activeR;
          // Baby blue = (0,150,255) → Yellow = (255,200,0)
          r = (uint8_t)(255.f * t          * bri);
          g = (uint8_t)((150.f + 50.f * t) * bri);
          b = (uint8_t)(255.f * (1.f - t)  * bri);
          w = 0;
        } else {
          // Dim base colour (rosa background)
          float dimBri = bri * p.vuBaseDim / 100.f;
          r = (uint8_t)(p.vuBaseR * dimBri);
          g = (uint8_t)(p.vuBaseG * dimBri);
          b = (uint8_t)(p.vuBaseB * dimBri);
          w = (uint8_t)(p.vuBaseW * dimBri);
        }
        strip.setPixelColor(ledIndex(s, px), RGBW32(r, g, b, w));
      }
    }
  }

  // ── Beat flash: all LEDs on beat ─────────────────────
  void applyBeat(const uint8_t* bands) {
    float gain = p.gain / 128.f;
    float bri  = p.bri  / 255.f;
    float bassAvg = 0.f;
    for (int i = 0; i < 4; i++) bassAvg += bands[i];
    bassAvg /= 4.f;
    bool isBeat = (bassAvg * gain) > p.beatThresh;
    for (int s = 0; s < p.numSteps; s++)
      for (int px = 0; px < p.stepCounts[s]; px++) {
        if (isBeat)
          strip.setPixelColor(ledIndex(s, px),
            RGBW32((uint8_t)(p.bassR*bri),(uint8_t)(p.bassG*bri),
                   (uint8_t)(p.bassB*bri),(uint8_t)(p.bassW*bri)));
        else
          strip.setPixelColor(ledIndex(s, px), 0);
      }
  }

  // ── Frequency bands → step groups ────────────────────
  void applyFreqBands(const uint8_t* bands) {
    float bri  = p.bri  / 255.f;
    float gain = p.gain / 128.f;
    for (int s = 0; s < p.numSteps; s++) {
      int   bidx     = (int)((float)s * 16.f / p.numSteps);
      if (bidx > 15) bidx = 15;
      float intensity = cf(bands[bidx] / 255.f * gain);
      uint8_t r, g, b, w;
      if (bidx < 4)      { r=p.bassR; g=p.bassG; b=p.bassB; w=p.bassW; }
      else if (bidx < 9) { r=p.midR;  g=p.midG;  b=p.midB;  w=p.midW;  }
      else               { r=p.highR; g=p.highG;  b=p.highB; w=p.highW; }
      float f = intensity * bri;
      for (int px = 0; px < p.stepCounts[s]; px++)
        strip.setPixelColor(ledIndex(s, px),
          RGBW32((uint8_t)(r*f),(uint8_t)(g*f),(uint8_t)(b*f),(uint8_t)(w*f)));
    }
  }

  // ── Colour pulse on bass ──────────────────────────────
  void applyColorPulse(const uint8_t* bands) {
    float bri  = p.bri  / 255.f;
    float gain = p.gain / 128.f;
    float bassAvg=0.f, midAvg=0.f, highAvg=0.f;
    for(int i=0;i<4;i++)  bassAvg+=bands[i];
    for(int i=4;i<9;i++)  midAvg +=bands[i];
    for(int i=9;i<16;i++) highAvg+=bands[i];
    bassAvg = cf(bassAvg/4.f/255.f*gain);
    midAvg  = cf(midAvg /5.f/255.f*gain);
    highAvg = cf(highAvg/7.f/255.f*gain);
    float intensity = (bassAvg + midAvg + highAvg) / 3.f;
    float f = intensity * bri;
    for(int s=0;s<p.numSteps;s++)
      for(int px=0;px<p.stepCounts[s];px++)
        strip.setPixelColor(ledIndex(s,px),
          RGBW32((uint8_t)(p.bassR*f),(uint8_t)(p.bassG*f),
                 (uint8_t)(p.bassB*f),(uint8_t)(p.bassW*f)));
  }

public:
  void setParams(const DiscoParams& params) { p = params; }

  void resetSmoothing() {
    for (int i = 0; i < SC_MAX_STEPS; i++) vuSmooth[i] = 0.f;
  }

  // bands: 16 FFT values 0-255 from AudioReactive
  void tick(const uint8_t* bands) {
    switch (p.effectType) {
      case 0: applyVuMeter(bands);   break;
      case 1: applyBeat(bands);      break;
      case 2: applyFreqBands(bands); break;
      case 3: applyColorPulse(bands);break;
      default: applyVuMeter(bands);  break;
    }
  }

  void applyBlackout() {
    int total = 0;
    for (int s = 0; s < p.numSteps; s++) total += p.stepCounts[s];
    for (int i = 0; i < total; i++) strip.setPixelColor(i, 0);
  }
};
